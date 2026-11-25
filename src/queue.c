#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q,struct pcb_t *p)
{
    if(!q||!p||q->size>=MAX_QUEUE_SIZE){fprintf(stderr,"Queue overflow/invalid\n");abort();}
    q->proc[q->size++]=p;
}
struct pcb_t *dequeue(struct queue_t *q)
{
    if(empty(q))return NULL;
    struct pcb_t *p=q->proc[0];
    for(int i=1;i<q->size;++i)q->proc[i-1]=q->proc[i];
    q->proc[--q->size]=NULL;return p;
}
struct pcb_t *purgequeue(struct queue_t *q,struct pcb_t *p)
{
    if(!q)return NULL;
    for(int i=0;i<q->size;++i)if(q->proc[i]==p){
        for(int j=i+1;j<q->size;++j)q->proc[j-1]=q->proc[j];
        q->proc[--q->size]=NULL;return p;
    }return NULL;
}
