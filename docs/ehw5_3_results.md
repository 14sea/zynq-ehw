# EHW-5.3 Results — Board Hybrid Memetic Loop

Status: **BOARD-VERIFIED on EBAZ4205 at FCLK0=50 MHz (2026-07-04, first roll).**
Steady mailbox carousel matched the host golden on every acceptance field:
`0xf5302028` (gen=32 completion replay tag, best_correct=40) /
`0xf53111a1` (SSE 4513) / `0xf5320f00` (feature_ones=15, penalty=0) /
`0xf53f0002` (first_40=2) / `0xf5f30000` (final PASS). FCLK0 preflight
captured in-session; full evidence and build manifest trail in
`docs/board_results.md`. Same-set deployment/adaptation metric — no holdout
generalization claim.

EHW-5.3 connects the board-verified EHW-5.2 combined RM to the first full hybrid
GA loop. The selected first arm is intentionally narrow:

```text
mode      = hybrid_lamarckian_pressure
coupling  = bias_x3
seed      = 3
POP       = 16
GENS      = 32
ADAPT     = 1 epoch
```

The firmware candidate evaluation uses both hardware-facing paths:

- loads each candidate's 16-byte spare-route genome into the VRC window at
  `0xF0000400`;
- reads live `SR_OUTPUT` for each dataset sample and applies `bias_x3`;
- loads the 24-byte weight genome into the lite train-unit at `0xF0000800`;
- runs one HW-SGD epoch through the train-unit MMIO protocol, including
  `TU_BUSY` waits;
- reads adapted weights back for Lamarckian inheritance.

Host-stub mode models the same MMIO protocol and is byte-compared against
`sw/ehw/memetic_struct_eval.c`.

## Deliverables

- `sw/ehw/memetic_struct_ga_mbox.c`
- `tests/compare_memetic_struct_ga_train.py`

## Host Gate

```bash
python3 tests/compare_memetic_struct_ga_train.py
```

Result:

```text
PASS: EHW-5.3 hybrid GA firmware stub curve is byte-exact
PASS: EHW-5.3 expected summary fields match
```

`tests/run_host_gates.sh` includes this gate.

The gate compares the entire per-generation curve, not just the final row. The
expected host-golden summary is:

```text
best_correct = 40/40
best_sse     = 4513
first_40     = 2
feature_ones = 15
penalty      = 0
```

Local isolated firmware build passes the image tripwire:

```text
text=5580 data=0 bss=3648
verify-image OK
```

The `.bss` footprint leaves wide headroom under the 16 KiB NEORV32 DMEM limit.

## Board Verification

Before board load, Claude ran:

```bash
python3 scripts/board-set-fclk50.py --port /dev/ebaz-uart
```

Acceptance evidence:

- `tests/run_host_gates.sh` PASS;
- firmware `verify-image` PASS and 16 KiB DMEM fit;
- FCLK0 readback `0x00200a00` immediately before `fpga loadb`;
- final mailbox carousel matches the host-golden fields above.

Observed steady final words:

```text
0xF5302028   final generation replay tag (gen 32, correct 40)
0xF53111a1   SSE low word for 4513
0xF5320f00   feature_ones=15, penalty_bucket=0
0xF53F0002   first_40=2
0xF5F30000   PASS
```

The board run sampled the carousel 60 times over about 2.5 minutes; all five
words were steady and matched host golden. See `docs/board_results.md` for the
full build and board evidence chain.

This remains the same 40-sample deployment/adaptation metric as EHW-5.0/5.1,
not a holdout generalization claim.
