# EHW-4.4 Results — Train-Unit Lamarckian GA Prep

Status: **HOST-PREP; no board claim.**

EHW-4.4 connects the EHW-4.3 board-verified memetic train unit to an actual
candidate-evaluation loop. The first scoped board-bound experiment is deliberately
small: Lamarckian mode only, `POP=16`, `GENS=8`, `adapt_epochs=1`, seed `3`.

Each candidate evaluation follows the hardware path:

```text
24-byte genome
  -> Q8.8 master load into train unit at 0xF0000800
  -> one shuffled fixed-point SGD epoch through the train-unit MMIO protocol
  -> adapted 24-byte genome
  -> firmware scores label/SSE fitness
  -> Lamarckian writeback uses the adapted genome
```

Host gate:

```sh
python3 tests/compare_memetic_ga_train.py
```

Verified output:

```text
PASS: EHW-4.4 train-unit Lamarckian GA curve is bit-exact vs memetic_eval
PASS: final best_correct=40 first_40=3 sse=6116
```

Isolated firmware build:

```text
Memory utilization: text=4096 data=0 bss=2560
[verify-image] OK: image == ELF load image
```

## What Is Proven

- `sw/ehw/memetic_ga_train_mbox.c` uses the same train-unit MMIO protocol as
  `sw/ehw/memetic_train_mbox.c`.
- The host stub models the train unit and emits a lamarckian per-generation curve.
- `tests/compare_memetic_ga_train.py` builds `sw/ehw/memetic_eval.c` as the
  software oracle, runs it with the EHW-4.4 board-scale parameters, filters the
  lamarckian rows, and byte-compares that curve against the firmware host stub.
- The selected short run reaches `40/40` by generation `3` and finishes at
  `40/40`, SSE `6116`, with no Lamarckian saturation in this short configuration.
- The firmware fits comfortably in the 16 KB NEORV32 DMEM budget (`.bss=2560`).

This is still a same-set deployment/adaptation metric, not a holdout
generalization claim.

## Firmware Parameters

```text
seed          = 3
population    = 16
generations   = 8
adapt_epochs  = 1
elites        = 2
tournament    = 3
crossover_ppm = 700000
mutation_ppm  = 30000
mutation_step = 8
init_span     = 32
```

These are intentionally smaller than the EHW-4.1 default `GENS=32,
adapt_epochs=2` run. The goal is a first board-verifiable GA x HW-SGD integration
loop, not a final tuning run.

## Mailbox Sketch

Board firmware publishes compact `0xF4xxxxxx` words:

```text
0xF4000044       reached EHW-4.4 firmware
0xF4400001       adapt_epochs = 1
0xF410ggcc       generation gg, best correct cc
0xF420ssss       best SSE low16
0xF430iicc       current generation top index ii, current top correct cc
0xF4F000cc       final steady word, cc = best correct
```

Expected final low byte for the current host-gated run is `0x28` (`40/40`).

## Next

Board run: build this firmware into the EHW-4.3 memetic-train RM bitstream, load via
U-Boot `fpga loadb`, poll the mailbox, and record exact words in
`docs/board_results.md`. If board time is too long, reduce `GENS` first while
keeping the host gate parameterized to match the board binary.
