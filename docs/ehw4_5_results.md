# EHW-4.5 Results — Same-Boot Baldwinian vs Lamarckian Prep

Status: **HOST-PREP; no board claim.**

EHW-4.5 prepares the clean board A/B comparison for the memetic line. One firmware
image runs both arms on the same boot and same bitstream:

1. Baldwinian GA: post-adapt fitness drives selection, but the original genome is
   kept for reproduction.
2. Lamarckian GA: post-adapt fitness drives selection, and the adapted genome is
   written back.

Both arms use the EHW-4.3 board-verified train-unit MMIO path at `0xF0000800`.

Host gate:

```sh
python3 tests/compare_memetic_ab_train.py
```

Verified output:

```text
PASS: EHW-4.5 same-boot A/B firmware curves are bit-exact vs memetic_eval
PASS: Baldwinian first_40=29 sse=4678; Lamarckian first_40=3 sse=6116
```

Isolated firmware build:

```text
Memory utilization: text=4232 data=0 bss=2560
[verify-image] OK: image == ELF load image
```

The `.bss` does not double because the two arms run sequentially and reuse the same
population/evaluation buffers.

## Parameters

```text
seed          = 3
population    = 16
generations   = 32
adapt_epochs  = 1
elites        = 2
tournament    = 3
crossover_ppm = 700000
mutation_ppm  = 30000
mutation_step = 8
init_span     = 32
```

## Expected Host Results

| Arm | Final correct | First 40/40 | Final SSE |
|---|---:|---:|---:|
| Baldwinian | 40/40 | 29 | 4678 |
| Lamarckian | 40/40 | 3 | 6116 |

This remains a same-set deployment/adaptation metric, not a holdout
generalization claim.

## Mailbox

Board firmware publishes:

```text
0xF4000045       reached EHW-4.5 firmware
0xF5400001       adapt_epochs = 1

0xF51ggcc        Baldwinian generation gg, best correct cc
0xF52ssss        Baldwinian best SSE low16
0xF53iicc        Baldwinian current top index ii, current top correct cc
0xF54...         Baldwinian arm metadata / adapt_epochs
0xF5F00028       Baldwinian arm final 40/40

0xF61ggcc        Lamarckian generation gg, best correct cc
0xF62ssss        Lamarckian best SSE low16
0xF63iicc        Lamarckian current top index ii, current top correct cc
0xF64...         Lamarckian arm metadata / adapt_epochs
0xF6F00028       Lamarckian arm final 40/40

0xF7F02828       final packed A/B result: Baldwinian 0x28, Lamarckian 0x28
```

Expected steady final word: **`0xF7F02828`**.

## Next

Board run: build `sw/ehw/memetic_ab_train_mbox.c` into the EHW-4.3
`rm_memetic_train` bitstream, load via U-Boot `fpga loadb`, poll the mailbox, and
record exact observations in `docs/board_results.md`.
