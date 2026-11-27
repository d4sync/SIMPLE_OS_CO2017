/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "queue.h"
#include "sched.h"
#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>
static struct queue_t ready_queue;
static struct queue_t run_queue;
static pthread_mutex_t queue_lock;

static struct queue_t running_list;
#ifdef MLQ_SCHED
static struct queue_t mlq_ready_queue[MAX_PRIO];
static int slot[MAX_PRIO];
#endif

/* queue_empty — Kiểm tra các ready/run queue dưới khóa scheduler.
 * Tham số: Không có.
 * Trả về: 1 nếu rỗng, 0 nếu còn process chờ.
 */
int queue_empty(void) {
    pthread_mutex_lock(&queue_lock);
#ifdef MLQ_SCHED
	unsigned long prio;
	for (prio = 0; prio < MAX_PRIO; prio++)
		if(!empty(&mlq_ready_queue[prio])) 
			{ pthread_mutex_unlock(&queue_lock); return 0; }
#endif
	int rc=empty(&ready_queue) && empty(&run_queue);
    pthread_mutex_unlock(&queue_lock);return rc;
}

/* init_scheduler — Khởi tạo hàng đợi, quota MLQ và mutex scheduler.
 * Tham số: Không có.
 * Trả về: Không trả giá trị.
 */
void init_scheduler(void) {
#ifdef MLQ_SCHED
    int i ;

	for (i = 0; i < MAX_PRIO; i ++) {
		mlq_ready_queue[i].size = 0;
		slot[i] = MAX_PRIO - i; 
	}
#endif
	ready_queue.size = 0;
	run_queue.size = 0;
	running_list.size = 0;
	pthread_mutex_init(&queue_lock, NULL);
}




#ifdef MLQ_SCHED
/* 
 *  Stateful design for routine calling
 *  based on the priority and our MLQ policy
 *  We implement stateful here using transition technique
 *  State representation   prio = 0 .. MAX_PRIO, curr_slot = 0..(MAX_PRIO - prio)
 */
/* get_mlq_proc — Chọn process theo quota MLQ và đưa vào running list.
 * Tham số: Không có.
 * Trả về: PCB được chọn hoặc NULL nếu không còn process chờ.
 */
struct pcb_t * get_mlq_proc(void) {
	struct pcb_t * proc = NULL;

	pthread_mutex_lock(&queue_lock);
    /* Integration: MLQ weighted quota theo MAX_PRIO-prio, FIFO moi hang. */
    for(int round=0;round<2 && !proc;++round){
        for(int i=0;i<MAX_PRIO;++i)if(slot[i]>0 && !empty(&mlq_ready_queue[i])){
            proc=dequeue(&mlq_ready_queue[i]);--slot[i];break;
        }
        if(!proc)for(int i=0;i<MAX_PRIO;++i)slot[i]=MAX_PRIO-i;
    }
    if(proc)enqueue(&running_list,proc);
    pthread_mutex_unlock(&queue_lock);
    return proc;
}

/* put_mlq_proc — Xóa process khỏi running list; requeue nếu chưa chạy hết code.
 * Tham số: proc: PCB vừa dùng hết time slice hoặc kết thúc.
 * Trả về: Không trả giá trị.
 */
void put_mlq_proc(struct pcb_t * proc) {
	

	/* Requeue a running process after its time slice. */

	pthread_mutex_lock(&queue_lock);
	purgequeue(&running_list,proc);
	if(proc->pc<proc->code->size)enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);
}

/* add_mlq_proc — Đưa process mới vào hàng đợi theo độ ưu tiên.
 * Tham số: proc: PCB mới.
 * Trả về: Không trả giá trị.
 */
void add_mlq_proc(struct pcb_t * proc) {
	

	/* Publish a new process to its priority queue. */
       
	pthread_mutex_lock(&queue_lock);
    proc->krnl->ready_queue=&ready_queue;
    proc->krnl->mlq_ready_queue=mlq_ready_queue;
    proc->krnl->running_list=&running_list;
	enqueue(&mlq_ready_queue[proc->prio], proc);
	pthread_mutex_unlock(&queue_lock);	
}

/* get_proc — Giao diện scheduler lấy process. Cấu hình hiện tại dùng MLQ.
 * Tham số: Không có.
 * Trả về: PCB được chọn hoặc NULL.
 */
struct pcb_t * get_proc(void) {
	return get_mlq_proc();
}

/* put_proc — Giao diện scheduler trả process sau time slice hoặc khi kết thúc (MLQ).
 * Tham số: proc: PCB.
 * Trả về: Không trả giá trị.
 */
void put_proc(struct pcb_t * proc) {
	return put_mlq_proc(proc);
}

/* add_proc — Giao diện scheduler thêm process mới.
 * Tham số: proc: PCB.
 * Trả về: Không trả giá trị.
 */
void add_proc(struct pcb_t * proc) {
	return add_mlq_proc(proc);
}
#else
/* get_proc — Giao diện scheduler lấy process. Cấu hình hiện tại dùng MLQ.
 * Tham số: Không có.
 * Trả về: PCB được chọn hoặc NULL.
 */
struct pcb_t * get_proc(void) {
	struct pcb_t * proc = NULL;

	pthread_mutex_lock(&queue_lock);
	/*TODO: get a process from [ready_queue].
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_unlock(&queue_lock);

	return proc;
}

/* put_proc — Giao diện scheduler trả process sau time slice hoặc khi kết thúc (MLQ).
 * Tham số: proc: PCB.
 * Trả về: Không trả giá trị.
 */
void put_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&run_queue, proc);
	pthread_mutex_unlock(&queue_lock);
}

/* add_proc — Giao diện scheduler thêm process mới.
 * Tham số: proc: PCB.
 * Trả về: Không trả giá trị.
 */
void add_proc(struct pcb_t * proc) {
	proc->krnl->ready_queue = &ready_queue;
	proc->krnl->running_list = &running_list;

	/* TODO: put running proc to running_list 
	 *       It worth to protect by a mechanism.
	 * 
	 */

	pthread_mutex_lock(&queue_lock);
	enqueue(&ready_queue, proc);
	pthread_mutex_unlock(&queue_lock);	
}
#endif


