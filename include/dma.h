/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DMA Interface Header
 */

#ifndef __DMA_H__
#define __DMA_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct pcb_t;
struct krnl_t;

/* One physical run. Split at either page boundary when frames are scattered. */
struct dma_span {
    uint64_t src, dst, bytes;
};

/* Filled by the MMU adapter after resolving and pinning BOTH ranges.
 * Spans stay valid until release(cookie, status), including submit failure.
 * A failed map must clean up its own partial pins. Use a cookie independent
 * of the address of this stack-allocated mapping structure.
 */
struct dma_mapping {
    const struct dma_span *spans;
    size_t count;
    void (*release)(void *, int);
    void *cookie;
};
typedef int (*dma_mapper)(struct krnl_t *, uint32_t pid, uint32_t src_region,
                          uint32_t dst_region, uint32_t offset, uint32_t bytes,
                          struct dma_mapping *mapping);

/* Register at boot before CPUs start. NULL means the MMU is unavailable. */
void dma_set_mapper(dma_mapper map);
/* Kernel side of memmap: convert PID + region operands into pinned runs. */
int dma_copy_regions(struct krnl_t *kernel, uint32_t pid, uint32_t src,
                     uint32_t dst, uint32_t offset, uint32_t bytes);

/* Start after binding RAM. Stop after producers exit; drains accepted work. */
int dma_start(void);
void dma_stop(void);

/* Try one request on the shared channel. Copies spans before returning.
 * Return 0 (accepted), -EBUSY (retry), or another negative errno.
 * On acceptance, finish(cookie, status) runs once on the worker, without
 * holding the channel lock. The owner keeps all frames pinned until then.
 * On rejection, the caller still owns the pin and must release it.
 */
int dma_submit(uint32_t pid, const struct dma_span *spans, size_t count,
               void (*finish)(void *, int), void *cookie);

/* True while this PID still owns a transfer (including its finish callback). */
int dma_pending(uint32_t pid);

/*
 * do_dma - perform DMA function
 * caller: process control block
 * source: physical RAM byte offset
 * destination: physical RAM byte offset
 * length: number of bytes to copy; returns the same status as dma_submit.
 * Kernel-only convenience wrapper for already stable physical ranges.
 */
int do_dma(struct pcb_t *caller, uint64_t source, uint64_t destination, uint64_t length);

#ifdef __cplusplus
}
#endif

#endif /* __DMA_H__ */