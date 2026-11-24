/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DMA C Testbench 
 */

#include "dma.h"
#include "dpi_rtl.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if DMA_MAIN
/* Compare the entire bank, including bytes beside the destination. */
static bool check_copy(uint64_t src, uint64_t dst, uint64_t bytes)
{
    const size_t limit = memory_bank_get_size();
    auto *ram = static_cast<uint8_t *>(memory_bank_get_ptr(0));
    for (size_t i = 0; i < limit; i++)
        ram[i] = static_cast<uint8_t>((i * 31 + i / 251) ^ 0xa5);

    bool valid = bytes != 0 && src < limit && dst < limit &&
                 bytes <= limit - src && bytes <= limit - dst &&
                 (src == dst || (src > dst ? src - dst : dst - src) >= bytes);
    std::vector<uint8_t> expected(ram, ram + limit);
    if (valid)
        for (uint64_t i = 0; i < bytes; i++)
            expected[dst + i] = ram[src + i];

    int status = dpi_rtl_dma_copy(src, dst, bytes);
    bool ok = (status == 0) == valid && std::memcmp(ram, expected.data(), limit) == 0;
    std::printf("%s src=%llu dst=%llu size=%llu result=%s\n",
                ok ? "PASS" : "FAIL", (unsigned long long)src,
                (unsigned long long)dst, (unsigned long long)bytes,
                status == 0 ? "copied" : "rejected");
    return ok;
}

static bool number(const char *s, uint64_t &value)
{
    char *end;
    errno = 0;
    value = std::strtoull(s, &end, 0);
    return *s && *s != '-' && !*end && !errno;
}

int main(int argc, char **argv)
{
    const size_t capacity = 1024 * 1024;
    uint64_t src = 0x1000, dst = 0x2000, bytes = 512;
    if (argc != 1 && (argc != 4 || !number(argv[1], src) ||
        !number(argv[2], dst) || !number(argv[3], bytes))) {
        std::fprintf(stderr, "Usage: %s [src dst bytes]\n", argv[0]);
        return 2;
    }
    if (memory_bank_init(capacity) == MEMORY_BANK_INVALID)
        return 1;
    rtl_dma_init_simulation();
    bool ok = check_copy(src, dst, bytes);
    if (argc == 1) {
        for (unsigned n = 1; n <= 9; n++)
            ok = check_copy(17, capacity - n, n) && ok;
        ok = check_copy(capacity - 3, 2, 3) && ok;
        ok = check_copy(5, 6, 4) && ok;
        ok = check_copy(0, 4, 0) && ok;
        ok = check_copy(UINT64_MAX, 8, 1) && ok;
        ok = check_copy(0, capacity - 1, 2) && ok;
        ok = check_copy(16, 16, 7) && ok;
    }
    rtl_dma_cleanup_simulation();
    memory_bank_cleanup();
    return ok ? 0 : 1;
}
#endif
