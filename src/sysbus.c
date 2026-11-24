/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * system bus - Only for dma_testbech simulation access
 */

#include "sysbus.h"
#include <stdlib.h>
#include <pthread.h>

/* Protects RAM bytes, not page ownership. Always acquired last. */
static pthread_mutex_t ram_lock = PTHREAD_MUTEX_INITIALIZER;
void sysbus_lock_ram(void) { pthread_mutex_lock(&ram_lock); }
void sysbus_unlock_ram(void) { pthread_mutex_unlock(&ram_lock); }

/* Bank zero is the address space used by the supplied RTL. Setup/cleanup
 * run before/after the worker. Only that worker uses the descriptor below.
 */
static struct {
    uint8_t *data;
    size_t size;
    int owned;
} banks[MAX_MEMORY_BANKS];

static struct {
    uint64_t src, dst, bytes;
    int fault;
} transfer;

memory_bank_t memory_bank_init(size_t bytes)
{
    if (!bytes)
        return MEMORY_BANK_INVALID;
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (!banks[i].data) {
            banks[i].data = calloc(bytes, 1);
            if (!banks[i].data)
                return MEMORY_BANK_INVALID;
            banks[i].size = bytes;
            banks[i].owned = 1;
            return i;
        }
    }
    return MEMORY_BANK_INVALID;
}

int memory_bank_attach(uint8_t *storage, size_t bytes)
{
    if (!storage || !bytes || banks[0].data)
        return -1;
    banks[0].data = storage;
    banks[0].size = bytes;
    return 0;
}

void memory_bank_cleanup(void)
{
    for (int i = 0; i < MAX_MEMORY_BANKS; i++) {
        if (banks[i].owned)
            free(banks[i].data);
        banks[i].data = NULL;
        banks[i].size = 0;
        banks[i].owned = 0;
    }
}

int memory_bank_is_available(void)
{
    return banks[0].data != NULL;
}

size_t memory_bank_get_size(void)
{
    return banks[0].size;
}

void *memory_bank_get_ptr(uint64_t offset)
{
    if (offset >= banks[0].size)
        return NULL;
    return banks[0].data + offset;
}

int dma_bus_valid(uint64_t src, uint64_t dst, uint64_t bytes)
{
    size_t limit = banks[0].size;
    if (!bytes || bytes > UINT32_MAX || src >= limit || dst >= limit ||
        bytes > limit - src || bytes > limit - dst)
        return 0;
    /* memcpy semantics. Disallow partially overlapping buffers. */
    if (src != dst && (src > dst ? src - dst : dst - src) < bytes)
        return 0;
    return 1;
}

int dma_bus_begin(uint64_t src, uint64_t dst, uint64_t bytes)
{
    if (!dma_bus_valid(src, dst, bytes))
        return -1;
    transfer.src = src;
    transfer.dst = dst;
    transfer.bytes = bytes;
    transfer.fault = 0;
    return 0;
}

int dma_bus_end(void)
{
    transfer.bytes = 0;
    return transfer.fault ? -1 : 0;
}

/* The final 32-bit bus beat may have only 1, 2 or 3 valid byte lanes. */
static unsigned valid_lanes(uint64_t addr, uint64_t base)
{
    if (addr < base || addr - base >= transfer.bytes)
        return 0;
    uint64_t left = transfer.bytes - (addr - base);
    return left < 4 ? (unsigned)left : 4;
}

int dpi_bank_read32_legacy(long long offset)
{
    uint64_t addr = (uint64_t)offset;
    unsigned lanes = valid_lanes(addr, transfer.src);
    if (!lanes) /* The RTL also reads back its destination after a write. */
        lanes = valid_lanes(addr, transfer.dst);
    if (!lanes)
        transfer.fault = 1;
    uint32_t word = 0;
    sysbus_lock_ram();
    for (unsigned i = 0; i < lanes; i++)
        word |= (uint32_t)banks[0].data[addr + i] << (8 * i);
    sysbus_unlock_ram();
    return (int)word;
}

int dpi_bank_write32_legacy(long long offset, int value)
{
    uint64_t addr = (uint64_t)offset;
    unsigned lanes = valid_lanes(addr, transfer.dst);
    if (!lanes) {
        transfer.fault = 1;
        return -1;
    }
    sysbus_lock_ram();
    for (unsigned i = 0; i < lanes; i++)
        banks[0].data[addr + i] = (uint8_t)((uint32_t)value >> (8 * i));
    sysbus_unlock_ram();
    return 0;
}
