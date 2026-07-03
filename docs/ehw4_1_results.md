# EHW-4.1 Results — Memetic Py/C Twin

Status: **HOST-ONLY; no board claim.**

EHW-4.1 adds a portable-C twin for the EHW-4.0 GA x fixed-point SGD memetic
oracle. The gate is stricter than a final-score comparison: Python and C must emit
the same per-generation curve CSV byte-for-byte for all four modes.

Command:

```sh
python3 tests/compare_memetic_twin.py
```

Verified output:

```text
PASS: EHW-4.1 Py<->C memetic curves are bit-exact
PASS: Lamarckian sat_count=3
```

## Fixed Contract

- Genome: the existing EHW-0 24-byte INT8 weight genome.
- RNG: the same XorShift32 family used by the EHW host twins.
- Fitness: primary `label_correct`, secondary `-SSE`, encoded as
  `correct * 1000000 - sse`.
- Adaptation: Q8.8 master copy, INT8 forward view, signed rounding multiply,
  saturating arithmetic, leaky derivative, and `w -= grad >> lr_shift`.
- Modes:
  - pure GA: selection uses direct deployment score.
  - pure SGD: starts from the M7.5.3 seed and applies the same adaptation budget.
  - Baldwinian: selection uses post-adapt score but does not write adaptation back
    to the genome.
  - Lamarckian: post-adapt weights are written back before selection.

This remains a **same-set deployment/adaptation metric**, not a holdout
generalization result.

## Deterministic Result

Defaults: `seed=3`, `population=16`, `generations=32`, `adapt_epochs=2`.

| Mode | Correct | SSE | Fitness | First 40/40 | Saturated weights |
|---|---:|---:|---:|---:|---:|
| pure GA | 40/40 | 4652 | 39995348 | 8 | 0 |
| pure SGD | 37/40 | 4798 | 36995202 | - | 0 |
| Baldwinian | 40/40 | 4398 | 39995602 | 30 | 0 |
| Lamarckian | 40/40 | 9705 | 39990295 | 13 | 3 |

The Baldwinian result confirms that adaptation can improve candidate fitness
without modifying the selected genome. The Lamarckian result confirms writeback
semantics (`post_genome == genome` in the summary), but it also pushes three INT8
weights to the positive saturation boundary. That explains the high SSE despite
40/40 label correctness and should be treated as an EHW-4.2/4.x design concern,
not as a correctness failure of this host twin.

## Artifacts

- `sim/oracle_memetic.py` — Python golden oracle.
- `sw/ehw/memetic_kernel.h` — shared portable-C kernel for inference,
  fixed-point adaptation, RNG, mutation, and GA helpers.
- `sw/ehw/memetic_eval.c` — C twin executable.
- `tests/compare_memetic_twin.py` — host gate compiling and comparing both sides.

Generated comparison outputs are written under `runs/tests/` and are intentionally
gitignored.

## Next

EHW-4.2/4.3 prepared and board-verified the train-unit path. EHW-4.4 adds the
short-population Lamarckian GA firmware prep; EHW-4.5 is the board run for that
full GA x HW-SGD loop.
