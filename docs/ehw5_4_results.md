# EHW-5.4 Results - Same-Boot Hybrid Ablation Prep

Status: **HOST-PREP COMPLETE.** No board claim is made here.

EHW-5.4a extends the board-verified EHW-5.3 single hybrid arm into a fixed
same-boot ablation table. The board firmware runs every arm sequentially in one
image and then repeats a steady carousel with every arm's final fields.

## Arms

Parameters are frozen to the EHW-5.3 run:

```text
seed=3
population=16
generations=32
adapt_epochs=1
```

| Arm | Mode | Coupling | Correct | SSE | First 40/40 | Feature ones | Penalty | Sat |
|---:|---|---|---:|---:|---:|---:|---:|---:|
| 0 | weight_only_lamarckian | none | 40/40 | 6116 | 3 | n/a | 0 | 0 |
| 1 | hybrid_lamarckian_pressure | bias_x3 | 40/40 | 4513 | 2 | 15 | 0 | 0 |
| 2 | hybrid_no_adapt | gate_x3 | 40/40 | 4615 | 11 | 39 | 0 | 0 |
| 3 | hybrid_lamarckian | bias_x3 | 40/40 | 5837 | 5 | 0 | 0 | 0 |

Interpretation remains conservative: arm 1 is the EHW-5.3 success arm; arm 0 is
the EHW-4-style weight-only baseline; arm 2 keeps structure but removes HW-SGD
adaptation; arm 3 shows the unpressured `bias_x3` degenerates to an all-zero
feature channel even though it still reaches 40/40.

## Deliverables

- `sw/ehw/memetic_struct_ab_mbox.c`
- `tests/compare_memetic_struct_ab_train.py`
- `docs/ehw5_4_results.md`

The RM is unchanged: the board path must reuse the EHW-5.2/EHW-5.3 combined
spare-route VRC + lite train-unit RM.

## Host Gate

```bash
python3 tests/compare_memetic_struct_ab_train.py
```

Result:

```text
PASS: EHW-5.4a same-boot A/B firmware stub curves are byte-exact
PASS: EHW-5.4a four-arm expected summary fields match
```

The gate compares the full per-generation curve for all four arms against
`sw/ehw/memetic_struct_eval.c`. Arm 0 is now emitted in the C/Python twin curve
CSV as `weight_only_lamarckian,none`, so the baseline is no longer checked only
by final summary.

`tests/run_host_gates.sh` includes this gate.

## Firmware Build Check

Isolated NEORV32 build:

```bash
make -C runs/fw_ehw54 APP_SRC=memetic_struct_ab_mbox.c \
  NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
  RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
  clean install verify-image
```

Result:

```text
text=7472 data=0 bss=6240
verify-image OK
```

The `.bss` footprint stays well under the 16 KiB NEORV32 DMEM limit while
running all four arms sequentially with shared buffers.

## Firmware Protocol

Board firmware start word:

```text
0xF5000004
```

Run-progress heartbeat:

```text
0xF5100000 | (arm << 8) | gen
0xF5200000 | (arm << 8) | best_correct
```

Steady carousel after all arms finish:

```text
0xF5400000 | (arm << 8)  | correct
0xF5500000 | (arm << 16) | (sse & 0xffff)
0xF5600000 | (arm << 8)  | first_40_or_0xff
0xF5700000 | (arm << 16) | (feature_ones << 8) | penalty_bucket
0xF54F0004
0xF5F40000   PASS
0xF5F40001   FAIL
```

For arm 0, `feature_ones=0` and `penalty_bucket=0` are placeholders; the
structural fields are not part of that arm's scientific claim.

## Board Handoff

Before any board load, Claude must run:

```bash
python3 scripts/board-set-fclk50.py --port /dev/ebaz-uart
```

Acceptance requires:

- `tests/run_host_gates.sh` PASS;
- firmware `verify-image` PASS and 16 KiB DMEM fit;
- FCLK0 readback `0x00200a00` immediately before `fpga loadb`;
- same firmware image and same boot for all four arms;
- steady carousel contains every arm row and matches the host-golden fields
  above.

This remains the same 40-sample deployment/adaptation metric as EHW-5.0-5.3,
not a holdout generalization claim.
