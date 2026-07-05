# EHW-5.4 Results - Same-Boot Hybrid Ablation

Status: **EHW-5.4a BOARD-VERIFIED on EBAZ4205 at FCLK0=50 MHz
(2026-07-05, first roll). EHW-5.4b param-window host prep is complete; board
staging is pending.**
Steady mailbox carousel matched the host golden for all four arms:
`f5400028 / f55017e4 / f5600003 / f5700000`,
`f5400128 / f55111a1 / f5600102 / f5710f00`,
`f5400228 / f5521207 / f560020b / f5722700`,
`f5400328 / f55316cd / f5600305 / f5730000`, plus `f54f0004`
and final `f5f40000`. Full evidence is in `docs/board_results.md`.

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
- `scripts/ehw54-param-pack.py`
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
PASS: EHW-5.4b staged default param block matches the built-in table
PASS: EHW-5.4b staged short scan changes the run and remains byte-exact
```

The gate compares the full per-generation curve for all four arms against
`sw/ehw/memetic_struct_eval.c`. Arm 0 is now emitted in the C/Python twin curve
CSV as `weight_only_lamarckian,none`, so the baseline is no longer checked only
by final summary.

For EHW-5.4b, the same gate also exercises the 4.6b parameter-window path in
host-stub mode:

- a staged block containing the same four arms must byte-match the built-in
  5.4a curve exactly;
- a staged single-arm short scan (`hybrid_lamarckian_pressure/bias_x3`,
  `generations=4`) must change the run and still byte-match the C reference
  curve for that staged parameter block.

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

The `.bss` footprint stayed well under the 16 KiB NEORV32 DMEM limit while
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
0xF54E0000 | (staged << 8) | valid
0xF54F0000 | arm_count
0xF5F40000   PASS
0xF5F40001   FAIL
```

For arm 0, `feature_ones=0` and `penalty_bucket=0` are placeholders; the
structural fields are not part of that arm's scientific claim.

## EHW-5.4b Parameter Window

The firmware now supports the board-verified 4.6b parameter window without
changing the RM:

```text
PS AXI write/read:  0x40000000
NEORV32 XBUS read: 0xF5000000 + 4*word
capacity:          2048 words / 8 KiB
```

Param block, little-endian words:

```text
word0  magic = 0xE5400001
word1  n_arms (1..8)
word2  seed
word3  population (2..16; current firmware buffer ceiling and elite count)
word4  generations (0..64)
word5  adapt_epochs (0..8)
word6  feature_min_balance
word7  feature_penalty
word8+ arm descriptor = mode | (coupling << 8) | (flags << 16)
```

Descriptor mode values:

```text
0 hybrid_lamarckian
1 hybrid_lamarckian_pressure
2 hybrid_no_adapt
3 weight_only_lamarckian
```

Descriptor coupling values:

```text
0 replace_x3
1 gate_x3
2 bias_x3
3 none
```

Descriptor flags:

```text
bit0 uses_structure
bit1 uses_adapt
bit2 uses_pressure
```

If magic is absent, firmware runs the board-verified built-in 5.4a four-arm
table. If magic is present but invalid, it reports FAIL rather than silently
falling back. `scripts/ehw54-param-pack.py` generates staged images, for
example:

```bash
python3 scripts/ehw54-param-pack.py --preset default \
  --out runs/ehw54/param_default.bin
python3 scripts/ehw54-param-pack.py --preset pressure-short --generations 4 \
  --out runs/ehw54/param_pressure_short.bin
```

Board staging should reuse the existing loader:

```bash
python3 scripts/ehw2-framebank-load.py runs/ehw54/param_default.bin 0x40000000
```

EHW-5.4b does not yet have a board claim. The board acceptance item is to stage
one of these blocks while the 5.4 firmware is running, confirm the steady
carousel source word changes to staged/valid (`0xF54E0101`), and confirm the arm
rows match the host-golden fields for the staged block without rebuilding or
reloading the bitstream.

## Board Verification

Before board load, Claude ran:

```bash
python3 scripts/board-set-fclk50.py --port /dev/ebaz-uart
```

Acceptance evidence:

- `tests/run_host_gates.sh`: 19/19 PASS;
- firmware `verify-image` PASS and 16 KiB DMEM fit (`text=7472 data=0 bss=6240`);
- clean `ws_54` build, WNS +1.026, 0 errors;
- FCLK0 readback `0x00200a00` immediately before `fpga loadb`;
- same firmware image and same boot for all four arms;
- 70 mailbox samples collected all 18 expected steady words with no strays.

Observed steady carousel:

| Arm | Correct | SSE | First 40/40 | Feature / penalty |
|---:|---|---|---|---|
| 0 | `f5400028` = 40 | `f55017e4` = 6116 | `f5600003` = 3 | `f5700000` = 0/0 |
| 1 | `f5400128` = 40 | `f55111a1` = 4513 | `f5600102` = 2 | `f5710f00` = 15/0 |
| 2 | `f5400228` = 40 | `f5521207` = 4615 | `f560020b` = 11 | `f5722700` = 39/0 |
| 3 | `f5400328` = 40 | `f55316cd` = 5837 | `f5600305` = 5 | `f5730000` = 0/0 |

Final words: `f54f0004` (arm count 4), `f5f40000` (PASS).

This remains the same 40-sample deployment/adaptation metric as EHW-5.0-5.3,
not a holdout generalization claim.

## Closeout

EHW-5.4a satisfies the EHW-5 stop rule. The line was closed after the same-boot
ablation: structure+pressure, HW-SGD adaptation, and pressure-vs-degeneration
were all tested without cross-build or cross-boot confounders. EHW-5.4b is
additional parameter-window polish and remains outside the main EHW-5 claim
until Claude performs the board staging check above.
