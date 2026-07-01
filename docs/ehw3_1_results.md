# EHW-3.1 Results — Spare-Routing Python/C Twin

Generated / verified by:

```bash
python3 tests/compare_spare_route_twin.py
```

Status: **PASS — host-only**. No board claim is made for this rung.

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
FAULT_STUCK0(A1),88,6
FAULT_STUCK1(A1),ee,6
FAULT_DISABLE_NODE(A1),88,6
FAULT_DISABLE_ROUTE(O.in1),cc,6
FAULT_DISABLE_ROUTE(A1.in0),88,6
no-fault last: 16,16,8,e8,e8,6,1,1,1,0e 0a 0c 04 e8 01 03 02 02 02 03 03 00 03 01 00
recovery last: 19,19,8,e8,e8,8,0,1,1,0e 06 0e 01 68 00 01 04 01 02 00 01 02 00 03 02
repaired genome: 0e 06 0e 01 68 00 01 04 01 02 00 01 02 00 03 02
```

## Interpretation

EHW-3.1 restores the project rule that a hardware-bound substrate has two
independent host implementations before moving toward RTL or board work. Since
this spare-routing island is new and has no `zynq_xpart` golden oracle, the
golden cross-check for this rung is the bit-exact Python/C double implementation:
same decode, same fault model, same RNG, same GA curve, same repaired genome.

