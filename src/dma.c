#include "dma.h"
#include "dpi_rtl.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* This lock protects the single channel and its mailbox. Never take a
 * memory/scheduler lock while holding it. RTL and finish() run unlocked.
 */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static pthread_t worker;
static int ready, stopping, init_status;
static struct {
    struct dma_span *spans;
    size_t count;
    uint32_t pid;
    void (*finish)(void *, int);
    void *cookie;
} job;

static void *dma_worker(void *unused)
{
    (void)unused;
    int status = rtl_dma_init_simulation();
    pthread_mutex_lock(&lock);
    init_status = status;
    ready = 1;
    pthread_cond_broadcast(&changed);
    while (status == 0) {
        while (!job.spans && !stopping)
            pthread_cond_wait(&changed, &lock);
        if (!job.spans)
            break;
        pthread_mutex_unlock(&lock);

        int result = 0;
        for (size_t i = 0; i < job.count && !result; i++) {
            const struct dma_span *s = &job.spans[i];
            if (getenv("DMA_TRACE"))
                printf("DMA BUS pid=%u src=%llu dst=%llu bytes=%llu\n", job.pid,
                       (unsigned long long)s->src, (unsigned long long)s->dst,
                       (unsigned long long)s->bytes);
            result = dpi_rtl_dma_copy(s->src, s->dst, s->bytes);
        }
        if (!result && getenv("DMA_TRACE"))
            printf("DMA COMPLETE pid=%u\n", job.pid);
        if (job.finish)
            job.finish(job.cookie, result);
        if (result)
            fprintf(stderr, "DMA PID %u: transfer failed\n", job.pid);

        pthread_mutex_lock(&lock);
        free(job.spans);
        job.spans = NULL;
        pthread_cond_broadcast(&changed);
    }
    pthread_mutex_unlock(&lock);
    rtl_dma_cleanup_simulation();
    return NULL;
}

/* Startup/shutdown belong to the simulation main thread. */
int dma_start(void)
{
    if (!memory_bank_is_available())
        return -ENODEV;
    pthread_mutex_lock(&lock);
    if (ready) {
        pthread_mutex_unlock(&lock);
        return -EBUSY;
    }
    stopping = 0;
    int rc = pthread_create(&worker, NULL, dma_worker, NULL);
    if (!rc) {
        while (!ready)
            pthread_cond_wait(&changed, &lock);
        rc = init_status;
    }
    pthread_mutex_unlock(&lock);
    if (rc && ready)
        dma_stop();
    return rc > 0 ? -rc : rc;
}

void dma_stop(void)
{
    pthread_mutex_lock(&lock);
    if (!ready) {
        pthread_mutex_unlock(&lock);
        return;
    }
    stopping = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    pthread_join(worker, NULL);
    pthread_mutex_lock(&lock);
    ready = 0;
    pthread_mutex_unlock(&lock);
}

int dma_pending(uint32_t pid)
{
    pthread_mutex_lock(&lock);
    int pending = job.spans && job.pid == pid;
    pthread_mutex_unlock(&lock);
    return pending;
}

/* Half-open ranges; dma_bus_valid has already checked both ends. */
static int overlaps(uint64_t a, uint64_t an, uint64_t b, uint64_t bn)
{
    return a < b + bn && b < a + an;
}

int dma_submit(uint32_t pid, const struct dma_span *spans, size_t count,
               void (*finish)(void *, int), void *cookie)
{
    if (!spans || !count || count > SIZE_MAX / sizeof(*spans))
        return -EINVAL;
    /* Validate every physical run before publishing any work. */
    for (size_t i = 0; i < count; i++)
        if (!dma_bus_valid(spans[i].src, spans[i].dst, spans[i].bytes))
            return -EINVAL;

    /* Reject aliasing between runs too: an early write must not destroy a
     * later source. The simulator has small requests, so a pairwise check
     * keeps the rule explicit. */
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < count; j++) {
            if (i == j)
                continue;
            if (overlaps(spans[i].dst, spans[i].bytes, spans[j].src, spans[j].bytes) ||
                overlaps(spans[i].dst, spans[i].bytes, spans[j].dst, spans[j].bytes))
                return -EINVAL;
        }
    }

    pthread_mutex_lock(&lock);
    int rc = 0;
    if (!ready || stopping)
        rc = -ENODEV;
    else if (job.spans)
        rc = -EBUSY;
    else {
        job.spans = malloc(count * sizeof(*spans));
        if (!job.spans)
            rc = -ENOMEM;
        else {
            memcpy(job.spans, spans, count * sizeof(*spans));
            job.pid = pid;
            job.count = count;
            job.finish = finish;
            job.cookie = cookie;
            pthread_cond_signal(&changed);
        }
    }
    pthread_mutex_unlock(&lock);
    return rc;
}

int do_dma(struct pcb_t *caller, uint64_t src, uint64_t dst, uint64_t bytes)
{
    struct dma_span span = {src, dst, bytes};
    if (!caller)
        return -EINVAL;
    return dma_submit(caller->pid, &span, 1, NULL, NULL);
}

/* Installed by the memory team at boot, then read-only during simulation. */
static dma_mapper map_regions;

void dma_set_mapper(dma_mapper map)
{
    map_regions = map;
}

int dma_copy_regions(struct krnl_t *kernel, uint32_t pid, uint32_t src,
                     uint32_t dst, uint32_t offset, uint32_t bytes)
{
    if (!kernel || !bytes || src >= PAGING_MAX_SYMTBL_SZ || dst >= PAGING_MAX_SYMTBL_SZ)
        return -EINVAL;
    if (!map_regions)
        return -ENOSYS;

    struct dma_mapping m = {0};
    int rc = map_regions(kernel, pid, src, dst, offset, bytes, &m);
    if (rc)
        return rc;
    uint64_t remaining = bytes;
    if (!m.spans || !m.count || m.count > bytes)
        rc = -EINVAL;
    for (size_t i = 0; i < m.count && !rc; i++) {
        if (m.spans[i].bytes > remaining)
            rc = -EINVAL;
        else
            remaining -= m.spans[i].bytes;
    }
    if (!rc && remaining == 0)
        rc = dma_submit(pid, m.spans, m.count, m.release, m.cookie);
    else
        rc = -EINVAL;
    if (rc && m.release)
        m.release(m.cookie, rc);
    return rc;
}
