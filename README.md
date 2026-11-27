# demacOprx - OS Simulation for CO2018

Operating System Simulation with DMA (Verilator RTL) for HCMC University of Technology Course CO2018.

## Build full OS simulation - The output file ./os

```bash
make os    # Build full OS simulation. The output file ./os
```
Run os simulation
```bash
# OS simulation
./os [configuration_file]
```

## Build only DMA module - The output file dma_testbench

```bash
make dma 
```
Run DMA module:
```bash
./dma_testbench
./dma_testbench 0x1000 0x2000 1024
```
## demacOprx environment
Ubuntu Virtual Machine image for Intel-x86 and AARCH64 (required HCMUT.SSO system)
[Course CO2018 Documentation](https://docs.google.com/document/d/1vCxl0sBotFjONqRJBAlnkTVYtlL1XZQrTyVl_dO0zxg/edit?usp=sharing "Course CO2018 Documentation - HCMC University of Technology")

## License

See [LICENSE](LICENSE) — educational use only for CO2018.

## MM64 / System Call / USR v3 — Student Friendly Implementation

Build CPU-only with `make os-mm`; run `make test-mm`. See `README_MM_VI.md`, `FUNCTION_COMPARISON.md`, `TEST_RESULTS.md` and `docs/report_mm.tex` for scope and verification.

V3 preserves the v2 function interfaces and tests, with Vietnamese function-level documentation and explicit control flow. The report in `docs/` is the retained v2 design report; see the new `TEST_RESULTS.md` for v3 verification.
