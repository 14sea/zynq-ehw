# EHW-5.4 Task - Same-Boot Hybrid Ablation + Param-Window Scan

Status: **BOARD-VERIFIED. EHW-5 closed here.** EHW-5.3 proved one full hybrid
structure+weight+HW-SGD Lamarckian-pressure arm on silicon. EHW-5.4 should turn
that single-arm proof into a same-boot ablation so the structural contribution is
visible without cross-build or cross-boot confounders.

## Decision

Do **not** close EHW-5 yet. EHW-5.3 is the core success point, but it is one
selected arm. EHW-5.4 is the right final evidence rung before either closing the
line or moving to an optional ICAP reveal:

- one firmware image;
- one boot;
- one FCLK0=50 MHz preflight;
- same seed and dataset;
- multiple arms whose only intended differences are structure/adaptation
  semantics.

This remains the 40-sample deployment/adaptation metric. It is not a holdout
generalization claim.

## Goal

Run a same-boot A/B table with at least these arms:

| arm | mode | coupling | purpose | host-golden summary |
|---:|---|---|---|---|
| 0 | `weight_only_lamarckian` | `none` | EHW-4-style weight-only baseline with HW-SGD | `40/40`, SSE `6116`, first_40 `3`, sat_count `0` |
| 1 | `hybrid_lamarckian_pressure` | `bias_x3` | EHW-5.3 successful hybrid arm | `40/40`, SSE `4513`, first_40 `2`, feature_ones `15`, penalty `0`, sat_count `0` |
| 2 | `hybrid_no_adapt` | `gate_x3` | structure+weights without HW-SGD adaptation | `40/40`, SSE `4615`, first_40 `11`, feature_ones `39`, penalty `0`, sat_count `0` |

Preferred fourth arm:

| arm | mode | coupling | purpose | host-golden summary |
|---:|---|---|---|---|
| 3 | `hybrid_lamarckian` | `bias_x3` | unpressured hybrid degeneration check | `40/40`, SSE `5837`, first_40 `5`, feature_ones `0`, penalty `0`, sat_count `0` |

These goldens are from `sw/ehw/memetic_struct_eval.c` with:

```text
seed=3
population=16
generations=32
adapt_epochs=1
```

The scientific readout should be conservative:

- arm 1 vs arm 0: structure+pressure improves both first_40 and SSE in this
  same-set metric;
- arm 1 vs arm 2: HW-SGD adaptation is still important for fast convergence;
- arm 1 vs arm 3, if included: pressure prevents the degenerate all-zero feature
  solution and gives the best SSE.

## 5.4a Deliverables - Fixed Same-Boot A/B

Suggested files:

- `sw/ehw/memetic_struct_ab_mbox.c`
- `tests/compare_memetic_struct_ab_train.py`
- `docs/ehw5_4_results.md`

Reuse the board-verified EHW-5.2/EHW-5.3 RM. Do **not** change
`rtl/dfx/tpu_rp_rm_memetic_struct.v` unless a new host gate proves a real
register-contract gap. The VRC window remains `0xF0000400`; the lite train-unit
window remains `0xF0000800`.

Firmware must run all arms in one boot and then carousel all final rows forever.
Do not rely on transient one-shot telemetry as the only evidence.

The host-stub mode must model the same MMIO protocol as the board firmware. It
must compare full per-generation curves for every enabled arm, not only final
summary rows.

## 5.4b Deliverables - 4.6b Param-Window Scan

Implementation note: host prep is complete in
`sw/ehw/memetic_struct_ab_mbox.c`, `tests/compare_memetic_struct_ab_train.py`,
and `scripts/ehw54-param-pack.py`. Board staging/live-update acceptance remains
pending.

After 5.4a is green, add a parameter-source switch:

- default source: built-in arm table, so the firmware is still self-contained;
- optional source: the board-verified 4.6b parameter window.

The 4.6b contract is:

```text
PS AXI write/read:  0x40000000
NEORV32 XBUS read: 0xF5000000 + 4*word
capacity:          2048 words / 8 KiB
ack discipline:    existing 1-cycle registered framebuf path only
```

Recommended param-block shape, little-endian words:

```text
word0  magic = 0xE5400001
word1  n_arms
word2  seed
word3  population (2..16 for current firmware)
word4  generations
word5  adapt_epochs
word6  feature_min_balance
word7  feature_penalty
word8+ packed arm descriptors: mode, coupling, flags, reserved
```

If `magic` is absent, firmware should run the built-in 5.4a table. If `magic` is
present, firmware should run the staged table. This makes a single bitstream
usable for interactive sweeps without rebuilding IMEM.

## Host Gate

`tests/compare_memetic_struct_ab_train.py` should:

1. build `sw/ehw/memetic_struct_eval.c`;
2. build the A/B firmware with a host-stub define;
3. run both with the frozen 5.4a arm table;
4. byte-compare the full per-generation curve for every enabled arm;
5. assert the summary fields above;
6. if 5.4b is implemented, run the same comparison once with a staged param
   block image and once with the built-in table.

Golden obligation is curve equality per arm. Matching only the final `40/40`
rows is not enough.

## Board Gate

Before every board load:

```sh
python3 scripts/board-set-fclk50.py --port /dev/ebaz-uart
```

Claude-side required checks:

- `tests/run_host_gates.sh` PASS;
- isolated firmware build with `verify-image` PASS and 16 KiB DMEM fit;
- no RTL/resource gate is needed if the RM is unchanged, but the build log must
  show the expected RM lineage;
- FCLK0 readback must be `0x00200a00` immediately before `fpga loadb`;
- board carousel must contain every enabled arm's result and match host-golden
  summary fields.

Suggested mailbox family:

```text
0xF5400000 | (arm << 8)  | correct               arm final correct
0xF5500000 | (arm << 16) | (sse & 0xffff)        arm final SSE low16
0xF5600000 | (arm << 8)  | first_40_or_0xff      arm first_40
0xF5700000 | (arm << 16) | (feature_ones << 8)
           | penalty_bucket                      arm feature/penalty
0xF54F0000 | arm_count                           final arm count
0xF5F40000                                        final PASS
0xF5F40001                                        final FAIL
```

If the exact encoding changes, document it in `docs/ehw5_4_results.md` before
board handoff. The important part is that arm identity and all acceptance fields
are recoverable from the steady carousel.

## Acceptance

EHW-5.4a is accepted when:

- host stub curves are byte-exact against `memetic_struct_eval.c` for every arm;
- board final rows match the host-golden summaries;
- all rows come from one firmware image and one boot;
- board evidence includes FCLK0=50 MHz verification;
- `docs/board_results.md` records the exact mailbox words.

EHW-5.4b is accepted when, in addition:

- PS can stage a param block at `0x40000000`;
- NEORV32 reads the matching block through `0xF5000000`;
- changing a staged scan parameter changes the subsequent run without rebuilding
  or reloading the bitstream.

## Stop Rule

EHW-5.4a passed, so the EHW-5 line is closed without 5.5. EHW-5.5 ICAP reveal
is optional presentation polish, not required for the structure+weight+HW-SGD
claim.
