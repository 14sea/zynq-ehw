# Release Notes — v1.0.0

`v1.0.0` freezes the first complete `zynq_ehw` release: the full EHW-0→EHW-3.4 ladder is
implemented, host-gated, documented, and board-verified on the EBAZ4205 / XC7Z010.

## What is included

- EHW-0: board-resident GA for INT8 weights, plus live ICAP bake into LUT-KCM fabric.
- EHW-1: CGP logic evolution for a 2-bit multiplier, including true fabric VRC evaluation and
  live ICAP rewrite of baked CGP LUTs.
- EHW-2: per-eval internal-ICAPE2 LUT-INIT evolution, where every fitness candidate is a live
  bitstream edit driven by NEORV32 through `rtl/xbus_icap.v`.
- EHW-3: fixed-route spare-routing island with evolved LUT truth tables and safe local path-select
  fields, including VRC recovery, live ICAP baked repair, and per-eval internal-ICAPE2 spare-route
  evolution.

## Board evidence

Exact mailbox observations are in `docs/board_results.md`; reproduction steps are in
`docs/BOARD_REPRO.md`.

Key final endpoints:

- EHW-0.5: `0x80AF7FF2`.
- EHW-1.2: broken CGP multiplier `7/16` → repaired `16/16`, live.
- EHW-2: `0xeb0308e8` = candidate `e8`, fitness `8/8`, mask `0xe8`.
- EHW-3.4: `0xec0308e8` = repair candidate, fitness `8/8`, mask `0xe8`.

## Reproducibility gates

- `tests/run_host_gates.sh` runs all host gates.
- Board flows require Vivado 2025.2, prjxray + `xc7z010clg400-1` DB, the RISC-V toolchain,
  and the EBAZ4205 board setup described in `README.md` and `docs/hw_notes.md`.
- Internal-ICAPE2 builds intentionally have no PS-HWICAP path; do not issue PS-HWICAP
  `readreg` / `writeseq` commands against those bitstreams.

## Scope and limits

- Mutations are restricted to LUT-INIT bits, local select functions, and register config; this
  release does not evolve arbitrary Xilinx routing bits.
- EHW-0.4 is a same-set deployment metric, not a held-out generalization result.
- The demonstrated search spaces are intentionally small. The result is a working, safe,
  reproducible 7-series intrinsic-EHW ladder, not a claim of large-scale FPGA synthesis.
