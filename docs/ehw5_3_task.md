# EHW-5.3 Task — Board Hybrid Memetic Loop

Status: **NEXT. Board-bound.** EHW-5.2 proved the combined spare-route VRC +
lite train-unit RM on silicon at FCLK0=50 MHz. EHW-5.3 now runs the actual
hybrid GA loop on NEORV32.

## Goal

Run the EHW-5.1 hybrid structure+weight contract on board:

- structure genome: EHW-3 spare-route VRC, evaluated in fabric at `0xF0000400`;
- weight genome: 24-byte INT8 folded network;
- adaptation: HW-SGD through the EHW-5.2 lite train-unit at `0xF0000800`;
- selection: Lamarckian with feature-balance pressure.

First board arm is intentionally narrow:

```text
mode      = hybrid_lamarckian_pressure
coupling  = bias_x3
seed      = 3
POP       = 16
GENS      = 32
ADAPT     = 1 epoch
```

Expected host-golden result from EHW-5.1:

```text
best_correct = 40/40
best_sse     = 4513
first_40     = 2
feature_ones = 15
penalty      = 0
```

This is still the 40-sample deployment/adaptation metric, not a holdout
generalization claim.

## Deliverables

- `sw/ehw/memetic_struct_ga_mbox.c`
- `tests/compare_memetic_struct_ga_train.py`
- `docs/ehw5_3_results.md`
- optional `vivado/dfx/m53_*` integration only if the existing `impl_12`
  build hook cannot bake the new firmware cleanly.

Do not change the EHW-5.2 RM unless the firmware gate exposes a real register
contract gap. `rtl/dfx/tpu_rp_rm_memetic_struct.v` is board-verified.

## Firmware Contract

Mirror `sw/ehw/memetic_struct_eval.c` for exactly one arm:
`MS_MODE_LAMARCKIAN_PRESSURE` + `MS_COUPLING_BIAS_X3`.

Candidate evaluation must use live board hardware for the two hardware paths:

1. Load candidate `sr[16]` into the VRC window.
2. For every dataset sample, write `SR_INPUT`, read `SR_OUTPUT`, and apply
   `bias_x3`: `x3 += phi ? 8 : -8`, clamped to INT8.
3. Load candidate weight genome into train-unit master registers.
4. Run one SGD epoch through the train-unit MMIO protocol, including `TU_BUSY`
   waits after serialized updates.
5. Read adapted weights back and use them for Lamarckian inheritance.
6. Compute post-score and feature-balance penalty with the same integer rules as
   `memetic_struct_eval.c`.

The host-stub mode must model the same MMIO protocol, not call a shortcut oracle
for candidate evaluation.

## Host Gate

`tests/compare_memetic_struct_ga_train.py` must:

1. build `sw/ehw/memetic_struct_eval.c`;
2. build `sw/ehw/memetic_struct_ga_mbox.c` with
   `-DMEMETIC_STRUCT_GA_HOST_STUB`;
3. run both with the frozen parameters above;
4. byte-compare the per-generation curve for the
   `hybrid_lamarckian_pressure,bias_x3` arm;
5. assert the final summary fields:
   `40/40`, SSE `4513`, first_40 `2`, feature_ones `15`, penalty `0`.

Golden obligation is byte-exact curve equality, not just matching the final
score.

## Board Gate

Before any board load:

```sh
python3 scripts/board-set-fclk50.py --port /dev/ebaz-uart
```

Claude-side required checks:

- `tests/run_host_gates.sh` PASS;
- firmware `verify-image` PASS and `.bss`/stack fit in 16 KiB DMEM;
- OOC/place checks only need to confirm no RTL/resource regression if the RM is
  unchanged;
- FCLK0 readback must be `0x00200a00` immediately before `fpga loadb`;
- board carousel must match the host-golden final fields.

Suggested mailbox protocol:

```text
0xF5300000 | (gen << 8) | best_correct     per-generation heartbeat
0xF5310000 | (best_sse & 0xffff)           per-generation SSE low word
0xF5320000 | (feature_ones << 8) | penalty_bucket
0xF53F0000 | first_40                      final first_40, or 0xffff if none
0xF5F30000                                  final PASS
0xF5F30001                                  final FAIL
```

If the exact encoding changes, document it in `docs/ehw5_3_results.md` before
board handoff.

## Acceptance

EHW-5.3 is accepted only when:

- host stub curve is byte-exact against `memetic_struct_eval.c`;
- board final result matches the host-golden summary;
- board evidence includes FCLK0=50 MHz verification;
- `docs/board_results.md` records the exact mailbox words.

If the board diverges while host gates pass, do not re-open the clock question
until FCLK0 readback has been captured in the same session.
