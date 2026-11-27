/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System OS module
 */
#include "cpu.h"
#include "timer.h"
#include "sched.h"
#include "loader.h"
#include "mm.h"
#ifdef MM64
#include "mm64.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>

#include "sysbus.h"
#include "dpi_rtl.h"

static int time_slot;
static int num_cpus;
static atomic_int done = 0;
static struct krnl_t os;

#ifdef MM_PAGING
static unsigned long memramsz;
static unsigned long memswpsz[PAGING_MAX_MMSWP];

struct mmpaging_ld_args {
	/* A dispatched argument struct to compact many-fields passing to loader */
	int vmemsz;
	struct memphy_struct *mram;
	struct memphy_struct **mswp;
	struct memphy_struct *active_mswp;
	int active_mswp_id;
	struct timer_id_t  *timer_id;
};
#endif

static struct ld_args{
	char ** path;
	unsigned long * start_time;
#ifdef MLQ_SCHED
	unsigned long * prio;
#endif
} ld_processes;
int num_processes;

struct cpu_args {
	struct timer_id_t * timer_id;
	int id;
};

static void * cpu_routine(void * args) {
	struct timer_id_t * timer_id = ((struct cpu_args*)args)->timer_id;
	int id = ((struct cpu_args*)args)->id;
	/* Check for new process in ready queue */
	int time_left = 0;
	struct pcb_t * proc = NULL;
	while (1) {
		/* Check the status of current process */
		if (proc == NULL) {
			/* No process is running, the we load new process from
		 	* ready queue */
			proc = get_proc();
			time_left = 0;
		}else if (proc->pc == proc->code->size) {
			/* The porcess has finish it job */
			printf("\tCPU %d: Processed %2d has finished\n",
				id ,proc->pid);
			put_proc(proc); /* Scheduler chi xoa process da ket thuc khoi running list. */
            free_pcb_memph(proc);
            free(proc->code->text);free(proc->code);free(proc->page_table);free(proc);
			proc = get_proc();
			time_left = 0;
		}else if (time_left == 0) {
			/* The process has done its job in current time slot */
			printf("\tCPU %d: Put process %2d to run queue\n",
				id, proc->pid);
			put_proc(proc);
			proc = get_proc();
		}
		
		/* Recheck process status after loading new process */
		if (proc == NULL && done) {
			/* No process to run, exit */
			printf("\tCPU %d stopped\n", id);
			break;
		}else if (proc == NULL) {
			/* There may be new processes to run in
			 * next time slots, just skip current slot */
			next_slot(timer_id);
			continue;
		}else if (time_left == 0) {
			printf("\tCPU %d: Dispatched process %2d\n",
				id, proc->pid);
			time_left = time_slot;
		}
		
		/* Run current process */
		if(proc->pc < proc->code->size){
            int rc=run(proc);
            if(rc)printf("[INSTRUCTION-ERROR] PID=%u PC=%u opcode=%d rc=%d\n",proc->pid,proc->pc-1,proc->code->text[proc->pc-1].opcode,rc);
        }
		time_left--;
		next_slot(timer_id);
	}
	detach_event(timer_id);
	pthread_exit(NULL);
}


#ifndef OSSIM_NO_DMA
static void * co_processor(void *arg) {
    (void)arg;
    /* TODO: Initialize the internal Verilator RTL simulation.
	 *       This is necessary to ensure that the RTL simulation is ready for DPI calls.
      */
    if (rtl_dma_init_simulation() != 0) {
        printf("Cannot initialize RTL simulation\n");
        return (void*)1;
    }
	
	/* TODO: This asynchronous implementation of DMA
     *        DMA completion callback is not yet implemented in this testbench
     */
    // Install callback handler
    //dma_done_callback();  // Call the callback to signal DMA completion
    return NULL;
}

#endif

static void * ld_routine(void * args) {
#ifdef MM_PAGING
	struct memphy_struct* mram = ((struct mmpaging_ld_args *)args)->mram;
	struct memphy_struct** mswp = ((struct mmpaging_ld_args *)args)->mswp;
	struct memphy_struct* active_mswp = ((struct mmpaging_ld_args *)args)->active_mswp;
	struct timer_id_t * timer_id = ((struct mmpaging_ld_args *)args)->timer_id;
#else
	struct timer_id_t * timer_id = (struct timer_id_t*)args;
#endif
	int i = 0;
    os.mram=mram;os.mswp=mswp;os.active_mswp=active_mswp;
    os.active_mswp_id=((struct mmpaging_ld_args *)args)->active_mswp_id;
    os.mm=calloc(1,sizeof(*os.mm));
    if(init_mm(os.mm,NULL)){fprintf(stderr,"Kernel MM init failed\n");exit(1);}
    os.mm->mmap->vm_start=os.mm->mmap->vm_end=os.mm->mmap->sbrk=os.kernel_base;
    os.krnl_pgd=os.mm->pgd;
	while (i < num_processes) {
		struct pcb_t * proc = load(ld_processes.path[i]);
		proc->krnl = &os;

#ifdef MLQ_SCHED
	proc->prio = ld_processes.prio[i];
#endif
	while (current_time() < ld_processes.start_time[i]) {
		next_slot(timer_id);
	}
#ifdef MM_PAGING
	proc->mm = (struct mm_struct*)malloc(sizeof(struct mm_struct));
	if(init_mm(proc->mm,proc)){fprintf(stderr,"Process MM init failed\n");exit(1);}
    pthread_mutex_lock(&memory_lock);
    proc->registry_next=os.processes;os.processes=proc;
    pthread_mutex_unlock(&memory_lock);
#endif
		printf("\tLoaded a process at %s, PID: %d PRIO: %ld\n",
			ld_processes.path[i], proc->pid, ld_processes.prio[i]);
		add_proc(proc);
		free(ld_processes.path[i]);
		i++;
		next_slot(timer_id);
	}
	free(ld_processes.path);
	free(ld_processes.start_time);
    free(ld_processes.prio);
    free(args);
	done = 1;
	detach_event(timer_id);
	pthread_exit(NULL);
}

/* Chap nhan 3 format goc: legacy khong RAM, dong 5 size, hoac
 * them kernel base truoc dong 5 size. Khong sua input goc. */


static void read_config(const char *path)
{
    FILE *f=fopen(path,"r");char line[512],extra,proc[100];
    if(!f||!(fscanf(f," %511[^\n]",line)==1)||sscanf(line,"%d %d %d",&time_slot,&num_cpus,&num_processes)!=3||time_slot<=0||num_cpus<=0||num_cpus>50||num_processes<0)goto bad;
    ld_processes.path=calloc(num_processes,sizeof(char*));
    ld_processes.start_time=calloc(num_processes,sizeof(unsigned long));
    ld_processes.prio=calloc(num_processes,sizeof(unsigned long));
    os.kernel_base=KERNEL_BASE_DEFAULT;
    memramsz=1048576;memswpsz[0]=16777216;
    int have=(fscanf(f," %511[^\n]",line)==1);
    unsigned long a[5];char *end;
    if(have){
        errno=0;uint64_t base=strtoull(line,&end,0);
        while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n')++end;
        if(!errno && end!=line && !*end){
            if(base<KERNEL_BASE_DEFAULT||(base&4095)||base>UINT64_MAX-4096)goto bad;
            os.kernel_base=base;have=(fscanf(f," %511[^\n]",line)==1);
        }
        if(have && sscanf(line,"%lu %lu %lu %lu %lu %c",a,a+1,a+2,a+3,a+4,&extra)==5){
            if(!a[0]||!(a[1]||a[2]||a[3]||a[4]))goto bad;
            for(int i=0;i<5;++i)if(a[i]>INT_MAX)goto bad;
            memramsz=a[0];for(int i=0;i<4;++i)memswpsz[i]=a[i+1];have=(fscanf(f," %511[^\n]",line)==1);
        }
    }
    for(int i=0;i<num_processes;++i){
        if(!have||sscanf(line,"%lu %99s %lu %c",&ld_processes.start_time[i],proc,&ld_processes.prio[i],&extra)!=3||ld_processes.prio[i]>=MAX_PRIO)goto bad;
        ld_processes.path[i]=malloc(112);snprintf(ld_processes.path[i],112,"input/proc/%s",proc);
        have=(fscanf(f," %511[^\n]",line)==1);
    }
    fclose(f);return;
bad:fprintf(stderr,"Invalid config: %s\n",path);if(f)fclose(f);exit(1);
}

int main(int argc, char * argv[]) {
	/* Read config */
	if (argc != 2) {
		printf("Usage: os [path to configure file]\n");
		return 1;
	}
	char path[512];
    if(snprintf(path,sizeof(path),"input/%s",argv[1])>=(int)sizeof(path))return 1;
	read_config(path);

	pthread_t * cpu = (pthread_t*)malloc(num_cpus * sizeof(pthread_t));
	#ifndef OSSIM_NO_DMA
    pthread_t dma;
#endif
	struct cpu_args * args =
		(struct cpu_args*)malloc(sizeof(struct cpu_args) * num_cpus);
	pthread_t ld;
	
	/* Init timer */
	int i;
	for (i = 0; i < num_cpus; i++) {
		args[i].timer_id = attach_event();
		args[i].id = i;
	}
	struct timer_id_t * ld_event = attach_event();
	start_timer();

#ifdef MM_PAGING
	/* Init all MEMPHY include 1 MEMRAM and n of MEMSWP */
	int rdmflag = 1; /* By default memphy is RANDOM ACCESS MEMORY */

	struct memphy_struct mram;
	struct memphy_struct mswp[PAGING_MAX_MMSWP];

	/* Create MEM RAM */
	if(init_memphy(&mram,memramsz,rdmflag)){fprintf(stderr,"RAM init failed\n");return 1;}
    printf("[MM-CONFIG] RAM=%lu page=4096 usable_frames=%lu kernel_base=%016"PRIx64"\n",memramsz,memramsz/4096>8192?8192:memramsz/4096,os.kernel_base);

        /* Create all MEM SWAP */ 
	int sit;
	struct memphy_struct *swaps[PAGING_MAX_MMSWP];
    int active=-1;
    for(sit=0;sit<PAGING_MAX_MMSWP;++sit){
        if(init_memphy(&mswp[sit],memswpsz[sit],rdmflag))return 1;
        swaps[sit]=&mswp[sit];if(active<0 && memswpsz[sit])active=sit;
    }

	/* In Paging mode, it needs passing the system mem to each PCB through loader*/
	struct mmpaging_ld_args *mm_ld_args = (struct mmpaging_ld_args*)malloc(sizeof(struct mmpaging_ld_args));

	mm_ld_args->timer_id = ld_event;
	mm_ld_args->mram = (struct memphy_struct *) &mram;
	mm_ld_args->mswp = swaps;
	mm_ld_args->active_mswp = active>=0 ? swaps[active]:NULL;
        mm_ld_args->active_mswp_id = active>=0?active:0;


#endif

	/* Init scheduler */
	init_scheduler();

	/* Run CPU and loader */
#ifdef MM_PAGING
	pthread_create(&ld, NULL, ld_routine, (void*)mm_ld_args);
#else
	pthread_create(&ld, NULL, ld_routine, (void*)ld_event);
#endif
	for (i = 0; i < num_cpus; i++) {
		pthread_create(&cpu[i], NULL,
			cpu_routine, (void*)&args[i]);
	}

	/* Run co-processor */
	#ifndef OSSIM_NO_DMA
    pthread_create(&dma,NULL,co_processor,NULL);
#endif

	/* Wait for CPU and loader finishing */
	for (i = 0; i < num_cpus; i++) {
		pthread_join(cpu[i], NULL);
	}
	pthread_join(ld, NULL);

	/* Stop timer */
	stop_timer();
#ifndef OSSIM_NO_DMA
    pthread_join(dma,NULL);
#endif
    struct pcb_t kernel_owner={.krnl=&os,.mm=os.mm};
    free_pcb_memph(&kernel_owner);os.mm=NULL;os.krnl_pgd=NULL;
    for(int j=-1;j<PAGING_MAX_MMSWP;++j){
        struct memphy_struct *mp=j<0?&mram:&mswp[j];
        while(mp->free_fp_list){struct framephy_struct *f=mp->free_fp_list;mp->free_fp_list=f->fp_next;free(f);}
        while(mp->used_fp_list){struct framephy_struct *f=mp->used_fp_list;mp->used_fp_list=f->fp_next;free(f);}
        free(mp->storage);
    }
    free(cpu);free(args);

	return 0;

}



