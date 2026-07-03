# EHW-5.1 Results — Portable-C Twin For Hybrid Structure + Weights

Status: **HOST-ONLY.** No board claim is made here.

EHW-5.1 mirrors the EHW-5.0/5.0b Python hybrid oracle in portable C:

- `sw/ehw/memetic_struct_kernel.h`
- `sw/ehw/memetic_struct_eval.c`
- `tests/compare_memetic_struct_twin.py`

The frozen first contract remains a 40-byte candidate genome:

```text
bytes  0..15  EHW-3 spare-route feature genome
bytes 16..39  EHW-4 INT8 seed-weight genome
```

The C twin covers the full selection semantics, not only final scores:
spare-route decode, feature coupling, structure mutation/crossover, weight
mutation/crossover, fixed-point SGD adaptation, Lamarckian writeback, no-adapt
ablation, and feature-balance pressure.

## Gate

```bash
python3 tests/compare_memetic_struct_twin.py
```

Result:

```text
PASS: EHW-5.1 Py<->C hybrid structure curves are byte-exact
PASS: structural pressure penalties are byte-exact
```

`tests/run_host_gates.sh` now runs 16 host gates and passes.

## Summary

Parameters:

```text
seed=3
population=16
generations=32
adapt_epochs=1
lr_shift=7
```

| Mode | Coupling | Correct | SSE | First 40/40 | Feature ones | Penalty | Sat |
|---|---|---:|---:|---:|---:|---:|---:|
| weight_only_lamarckian | none | 40/40 | 6116 | 3 | n/a | n/a | 0 |
| hybrid_lamarckian | replace_x3 | 40/40 | 7177 | 20 | 15 | 0 | 3 |
| hybrid_lamarckian | gate_x3 | 40/40 | 7888 | 6 | 40 | 0 | 0 |
| hybrid_lamarckian | bias_x3 | 40/40 | 5837 | 5 | 0 | 0 | 0 |
| hybrid_lamarckian_pressure | replace_x3 | 38/40 | 7633 | none | 15 | 0 | 6 |
| hybrid_lamarckian_pressure | gate_x3 | 40/40 | 8439 | 24 | 40 | 400000 | 6 |
| hybrid_lamarckian_pressure | bias_x3 | 40/40 | 4513 | 2 | 15 | 0 | 0 |
| hybrid_no_adapt | gate_x3 | 40/40 | 4615 | 11 | 39 | 0 | 0 |

The key pressure result from EHW-5.0b is preserved exactly in C:
`hybrid_lamarckian_pressure / bias_x3` reaches `40/40`, SSE `4513`, first_40 `2`,
with a non-constant `15/40` feature mask and zero pressure penalty.

## Notes For EHW-5.2

- The C twin is now the firmware-facing golden for the combined VRC + train-unit
  RM.
- The pressure penalty is part of selection fitness and must remain in the host
  gate for any firmware/RTL port.
- This is still the same 40-sample deployment/adaptation metric. It is not a
  holdout generalization claim.
