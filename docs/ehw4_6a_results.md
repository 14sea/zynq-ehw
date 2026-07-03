# EHW-4.6a Results — Compile-Time Memetic Parameter Sweep

Status: **BOARD-VERIFIED on EBAZ4205 (2026-07-03).**

EHW-4.6a turns the single EHW-4.5 A/B point into a small deterministic parameter
sweep without changing the static design. One firmware image bakes a compile-time
table of parameter structs, runs every point sequentially on the same boot, and
publishes a compact result carousel after the sweep completes.

The firmware deliberately reads an `ehw46_param_t` for every run. EHW-4.6b can
replace the const table with the already board-proven `axil_framebuf` parameter
source later; the GA/evaluator path should not need a rewrite.

## Host Gate

```sh
python3 tests/compare_memetic_sweep.py
```

The gate builds:

- `sw/ehw/memetic_eval.c` as the reference C kernel.
- `sw/ehw/memetic_sweep_mbox.c -DMEMETIC_SWEEP_HOST_STUB` as the board-firmware
  host model.

For each sweep point, the reference executable runs the same parameters and emits
a summary row. The firmware host model emits one combined summary CSV. The gate
byte-compares the two files.

Verified output:

```text
PASS: EHW-4.6a compile-time sweep firmware summary is byte-exact vs memetic_eval
PASS: 12 points x 2 modes; 16 rows reached 40/40
```

## Sweep Table

All points use seed `3`, elites `2`, tournament `3`, init span `32`,
mutation step `8`, crossover `700000 ppm`, and mutation `30000 ppm`.

| Point | POP | GENS | adapt_epochs | Baldwinian | Lamarckian |
|---:|---:|---:|---:|---:|---:|
| 0 | 8 | 8 | 1 | 38/40, SSE 4866 | 37/40, SSE 4752 |
| 1 | 8 | 16 | 1 | 40/40 @ gen 15, SSE 4801 | 39/40, SSE 7664 |
| 2 | 8 | 32 | 1 | 40/40 @ gen 15, SSE 4719 | 40/40 @ gen 23, SSE 10097 |
| 3 | 16 | 8 | 1 | 38/40, SSE 4748 | 40/40 @ gen 3, SSE 6116 |
| 4 | 16 | 16 | 1 | 39/40, SSE 4712 | 40/40 @ gen 3, SSE 6116 |
| 5 | 16 | 32 | 1 | 40/40 @ gen 29, SSE 4678 | 40/40 @ gen 3, SSE 6116 |
| 6 | 32 | 8 | 1 | 40/40 @ gen 4, SSE 4628 | 40/40 @ gen 4, SSE 5144 |
| 7 | 32 | 16 | 1 | 40/40 @ gen 4, SSE 4518 | 40/40 @ gen 4, SSE 5144 |
| 8 | 32 | 32 | 1 | 40/40 @ gen 4, SSE 4025 | 40/40 @ gen 4, SSE 5144 |
| 9 | 16 | 8 | 2 | 39/40, SSE 4693 | 39/40, SSE 7357 |
| 10 | 16 | 16 | 2 | 39/40, SSE 4528 | 40/40 @ gen 13, SSE 9705 |
| 11 | 16 | 32 | 2 | 40/40 @ gen 30, SSE 4398 | 40/40 @ gen 13, SSE 9705 |

This remains a same-set deployment/adaptation metric, not a holdout
generalization claim.

## Firmware Build

Isolated NEORV32 build:

```text
Memory utilization: text=4792 data=0 bss=6080
[verify-image] OK: image == ELF load image
```

The maximum compiled population is `EHW46_MAX_POP=32`. `.bss=6080` plus text fits
comfortably under the 16 KB NEORV32 DMEM budget, leaving roughly 5.5 KB for stack
and runtime headroom. POP=64 is intentionally not included in this sweep.

## Mailbox Protocol

Board firmware publishes:

```text
0xF8000046       reached EHW-4.6a firmware
0xF830000c       sweep point count = 12
0xF8200000 | point<<8 | mode_mask
0xF8100000 | point<<16 | mode<<15 | gen<<8 | best_correct
0xF8F0000c       sweep complete, 12 points stored

0xF8000000 | point<<18 | mode<<17 | first_40<<8 | best_correct
0xF9000000 | point<<18 | mode<<17 | best_sse_low16
```

For `first_40`, the firmware encodes `0x3f` when the arm never reaches 40/40.
Mode `0` is Baldwinian and mode `1` is Lamarckian.

## Board Result

The board run used the same EHW-4.3 `rm_memetic_train` lineage. One firmware
image and one boot ran all 12 points × 2 modes in about two minutes, then
carouseled the packed results.

Observed on EBAZ4205:

```text
48/48 distinct carousel words collected
24/24 point/mode rows covered
0 mismatches vs host-golden CSV encoding
16/24 rows reached 40/40
```

Full board log: `docs/board_results.md` §EHW-4.6a.

EHW-4.6b is the optional static upgrade: attach the existing `rtl/axil_framebuf.vhd`
to `neorv32_soc_dfx` so PS can inject parameter structs without rebuilding the
firmware image.
