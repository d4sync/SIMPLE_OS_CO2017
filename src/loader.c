
#include "loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t avail_pid = 1;

#define OPT_CALC	"calc"
#define OPT_ALLOC	"alloc"
#define OPT_FREE	"free"
#define OPT_READ	"read"
#define OPT_WRITE	"write"
#define OPT_SYSCALL	"syscall"
#define OPT_KMALLOC	            "kmalloc"
#define OPT_DMA_MEMCPY	        "dma_memcpy"
#define OPT_COPY_FROM_USER	    "copy_from_user"
#define OPT_COPY_TO_USER	    "copy_to_user"

static enum ins_opcode_t get_opcode(char * opt) {
	if (!strcmp(opt, OPT_CALC)) {
		return CALC;
	}else if (!strcmp(opt, OPT_ALLOC)) {
		return ALLOC;
	}else if (!strcmp(opt, OPT_FREE)) {
		return FREE;
	}else if (!strcmp(opt, OPT_READ)) {
		return READ;
	}else if (!strcmp(opt, OPT_WRITE)) {
		return WRITE;
	}else if (!strcmp(opt, OPT_KMALLOC)) {
		return KMALLOC;
	}else if (!strcmp(opt, OPT_COPY_FROM_USER)) {
		return COPY_FROM_USER;
	}else if (!strcmp(opt, OPT_COPY_TO_USER)) {
		return COPY_TO_USER;
	}else if (!strcmp(opt, OPT_DMA_MEMCPY)) {
		return DMA_MEMCPY;
	}else if (!strcmp(opt, OPT_SYSCALL)) {
		return SYSCALL;
	}else{
		printf("get_opcode return Opcode: %s\n", opt);
		exit(1);
	}
}

struct pcb_t * load(const char * path) {
	/* Create new PCB for the new process */
	struct pcb_t * proc = (struct pcb_t * )calloc(1,sizeof(struct pcb_t));
	if (!proc) { perror("PCB calloc"); exit(1); }
	proc->pid = avail_pid;
	avail_pid++;
	#ifndef MM_PAGING
	proc->page_table = calloc(1,sizeof(struct page_table_t));
#endif
	proc->bp = PAGE_SIZE;
	proc->pc = 0;

	/* Read process code from file */
	FILE * file;
	if ((file = fopen(path, "r")) == NULL) {
		printf("Cannot find process description at '%s'\n", path);
		exit(1);		
	}
	snprintf(proc->path, sizeof(proc->path), "%s", path);
    char line[256], opcode[32], extra;
    proc->code = calloc(1,sizeof(*proc->code));
    if (!proc->code || !fgets(line,sizeof(line),file) ||
        sscanf(line,"%u %u %c",&proc->priority,&proc->code->size,&extra)!=2)
        goto invalid;
    proc->code->text=calloc(proc->code->size,sizeof(struct inst_t));
    if (proc->code->size && !proc->code->text)goto invalid;
    for (uint32_t i=0;i<proc->code->size;++i) {
        struct inst_t *ins=&proc->code->text[i];
        do {
            if (!fgets(line,sizeof(line),file))goto invalid;
        } while (strspn(line," \t\r\n")==strlen(line));
        int n=sscanf(line,"%31s " FORMAT_ARG " " FORMAT_ARG " " FORMAT_ARG " " FORMAT_ARG " %c",
                     opcode,&ins->arg_0,&ins->arg_1,&ins->arg_2,&ins->arg_3,&extra);
        if (n<1)goto invalid;
        ins->opcode=get_opcode(opcode);
        switch (ins->opcode) {
        case CALC:if(n!=1)goto invalid;break;
        case FREE:if(n!=2)goto invalid;break;
        case ALLOC:case KMALLOC:if(n!=3)goto invalid;break;
        case READ:case WRITE:if(n!=4)goto invalid;break;
        case COPY_FROM_USER:case COPY_TO_USER:case DMA_MEMCPY:if(n!=5)goto invalid;break;
        case SYSCALL:if(n<2||n>5)goto invalid;break;
        default:goto invalid;
        }
    }
    fclose(file);
    return proc;
invalid:
    fprintf(stderr,"Invalid process description: %s\n",path);
    fclose(file);
    exit(1);
}
