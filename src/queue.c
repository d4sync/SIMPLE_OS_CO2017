#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

/* empty — Kiểm tra hàng đợi có rỗng không.
 * Tham số: q: hàng đợi.
 * Trả về: 1 nếu rỗng hoặc NULL, 0 nếu có phần tử.
 */
int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

/* enqueue — Thêm PCB vào cuối hàng đợi.
 * Tham số: q: hàng đợi; p: PCB.
 * Trả về: Không trả giá trị; abort nếu tham số lỗi hoặc hàng đầy. Caller giữ queue_lock.
 */
void enqueue(struct queue_t *q,struct pcb_t *p)
{
    if(!q||!p||q->size>=MAX_QUEUE_SIZE){fprintf(stderr,"Queue overflow/invalid\n");abort();}
    q->proc[q->size++]=p;
}
/* dequeue — Lấy PCB đầu hàng đợi, dồn phần tử còn lại.
 * Tham số: q: hàng đợi.
 * Trả về: PCB lấy ra hoặc NULL nếu rỗng.
 */
struct pcb_t *dequeue(struct queue_t *q)
{
    if(empty(q))return NULL;
    struct pcb_t *p=q->proc[0];
    for(int i=1;i<q->size;++i)q->proc[i-1]=q->proc[i];
    q->proc[--q->size]=NULL;return p;
}
/* purgequeue — Xóa một PCB cụ thể khỏi hàng đợi.
 * Tham số: q: hàng đợi; p: PCB cần xóa.
 * Trả về: PCB đã xóa hoặc NULL nếu không tìm thấy.
 */
struct pcb_t *purgequeue(struct queue_t *q,struct pcb_t *p)
{
    if(!q)return NULL;
    for(int i=0;i<q->size;++i)if(q->proc[i]==p){
        for(int j=i+1;j<q->size;++j)q->proc[j-1]=q->proc[j];
        q->proc[--q->size]=NULL;return p;
    }return NULL;
}
