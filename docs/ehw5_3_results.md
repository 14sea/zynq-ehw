# EHW-5.3 Results — Board Hybrid Memetic Loop Prep

Status: **HOST-PREP COMPLETE.** No board claim is made here.

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

Local isolated firmware build also passes the image tripwire:

```text
text=5580 data=0 bss=3648
verify-image OK
```

The `.bss` footprint leaves wide headroom under the 16 KiB NEORV32 DMEM limit.
Claude should still rerun the same check in the board build workspace before
loading.

## Board Handoff

Before any board load, Claude must run:

```bash
python3 scripts/board-set-fclk50.py --port /dev/ebaz-uart
```

Acceptance requires:

- `tests/run_host_gates.sh` PASS;
- firmware `verify-image` PASS and 16 KiB DMEM fit;
- FCLK0 readback `0x00200a00` immediately before `fpga loadb`;
- final mailbox carousel matches the host-golden fields above.

Suggested final words from `memetic_struct_ga_mbox.c`:

```text
0xF53020xx   final generation heartbeat (gen 32, correct xx)
0xF5311191   SSE low word for 4513
0xF5320f00   feature_ones=15, penalty_bucket=0
0xF53F0002   first_40=2
0xF5F30000   PASS
```

This remains the same 40-sample deployment/adaptation metric as EHW-5.0/5.1,
not a holdout generalization claim.
