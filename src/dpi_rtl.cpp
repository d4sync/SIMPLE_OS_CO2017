/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DPI RTL C++ interface to  DMA RTL
 */

#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vdma_top.h"
#include "sysbus.h"
#include "dpi_rtl.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>

// The DMA worker (or the standalone test's main thread) owns all RTL state.
static Vdma_top *top;
static VerilatedContext *context;
static VerilatedVcdC *tracer;
static uint64_t sim_time;
static bool saw_completion;

/* Called by the supplied RTL on the rising edge of ch_done. */
extern "C" void dma_done_callback(void)
{
    saw_completion = true;
}

extern "C" int rtl_dma_init_simulation(void)
{
    if (top)
        return 0;
    context = new VerilatedContext;
    context->threads(1);
    context->traceEverOn(true);
    top = new Vdma_top(context, "dma_top");
    if (std::getenv("DMA_TRACE")) {
        tracer = new VerilatedVcdC;
        top->trace(tracer, 99);
        tracer->open("rtl_trace.vcd");
    }
    return 0;
}

extern "C" void rtl_dma_cleanup_simulation(void)
{
    if (top)
        top->final();
    if (tracer)
        tracer->close();
    delete tracer;
    delete top;
    delete context;
    tracer = nullptr;
    top = nullptr;
    context = nullptr;
    sim_time = 0;
}

/* One complete clock cycle. Evaluate inputs before the rising edge. */
static void tick()
{
    for (unsigned level = 0; level < 2; level++) {
        top->clk = level;
        top->eval();
        if (tracer)
            tracer->dump(sim_time);
        sim_time++;
    }
}

extern "C" int dpi_rtl_dma_copy(uint64_t src, uint64_t dst, uint64_t bytes)
{
    if (!top || dma_bus_begin(src, dst, bytes) != 0)
        return -1;

    top->ch_enable = 0;
    top->ch_pause = 0;
    top->desc_valid = 0;
    top->rst_n = 0;
    tick();
    saw_completion = false;
    top->rst_n = 1;
    top->desc_data_0 = src;
    top->desc_data_1 = dst;
    top->desc_data_2 = bytes;
    top->desc_data_3 = 0;
    top->desc_valid = 1;
    top->ch_enable = 1;
    tick();
    top->desc_valid = 0;

    // Allow startup plus enough clocks for the read/write handshake per beat.
    const uint64_t limit = 32 + 32 * ((bytes + 3) / 4);
    for (uint64_t i = 0; i < limit && !saw_completion && !top->ch_error; i++)
        tick();

    bool ok = saw_completion && !top->ch_error && top->ch_bytes_transferred == bytes;
    top->ch_enable = 0;
    tick();
    return dma_bus_end() == 0 && ok ? 0 : -1;
}

extern "C" int dpi_rtl_dma_is_available(void)
{
    return top != nullptr;
}
