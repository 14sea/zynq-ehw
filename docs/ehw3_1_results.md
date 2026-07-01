# EHW-3.1 Results — Spare-Routing Python/C Twin

Generated / verified by:

```bash
python3 tests/compare_spare_route_twin.py
```

Status: **PASS — host-only**. No board claim is made for this rung. Current
default population is `128`, chosen so the EHW-3.2 NEORV32 firmware fits the
fixed 16 KiB DMEM.

## Deliverables

- `sw/ehw/spare_route_kernel.h`: frozen 16-byte genome contract, LUT decode,
  validity layer, fault model, and evaluator.
- `sw/ehw/spare_route_eval.c`: portable C host twin with the same xorshift32 GA
  as `sim/oracle_spare_routing.py`.
- `tests/compare_spare_route_twin.py`: host gate comparing Python and C CSV
  curves byte-for-byte.

## Gate Coverage

The gate checks:

- representability contract: majority target `0xe8` is reachable with the LUT8
  output node;
- no-fault GA curve: Python vs C byte-identical per generation;
- post-fault recovery GA curve under `FAULT_DISABLE_NODE(A1)`: Python vs C
  byte-identical per generation;
- final repaired genome and repaired truth-table mask;
- direct fault-model masks for `FAULT_NONE`, `STUCK0`, `STUCK1`,
  `DISABLE_NODE`, and two `DISABLE_ROUTE` cases.

## Verified Output

```text
PASS: spare-routing Python oracle and C twin are bit-exact
fault masks:
FAULT_NONE,e8,8
FAULT_STUCK0(A1),c8,7
FAULT_STUCK1(A1),fa,6
FAULT_DISABLE_NODE(A1),c8,7
FAULT_DISABLE_ROUTE(O.in1),20,5
FAULT_DISABLE_ROUTE(A1.in0),c8,7
no-fault last: 21,21,8,e8,e8,7,1,0,1,0a 08 01 0f 32 01 04 00 02 02 00 04 01 01 02 00
recovery last: 17,17,8,e8,e8,8,0,1,1,0b 09 09 03 b1 00 04 04 01 02 00 00 01 02 03 00
repaired genome: 0b 09 09 03 b1 00 04 04 01 02 00 00 01 02 03 00
```

## Interpretation

EHW-3.1 restores the project rule that a hardware-bound substrate has two
independent host implementations before moving toward RTL or board work. Since
this spare-routing island is new and has no `zynq_xpart` golden oracle, the
golden cross-check for this rung is the bit-exact Python/C double implementation:
same decode, same fault model, same RNG, same GA curve, same repaired genome.
