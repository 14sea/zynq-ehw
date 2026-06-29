# EHW-1.0 Results — CGP 2-bit Multiplier Oracle

Generated / verified by:

```bash
python3 tests/compare_cgp_twin.py --seed 3 --population 64 --generations 200
```

## Substrate

- Grid: `3 x 4` LUT4 nodes.
- Genome: `12` LUT4 INIT words = `192` bits.
- Routing: fixed and legal by construction.
  - column 0 sees primary inputs `[a0,a1,b0,b1]`;
  - column 1 sees column 0 outputs;
  - column 2 sees column 1 outputs and drives `[p0,p1,p2,p3]`.
- First oracle uses col0/col1 as a pass-through scaffold and evolves the four
  output LUTs in col2. The full 192-bit genome is still evaluated and logged.

## Result

| Item | Value |
|---|---:|
| Fitness | `64/64` bits |
| Truth-table rows | `16/16` |
| Convergence | generation `46` |
| Population | `64` |
| Seed | `3` |
| Active LUTs | `12` |

Final genome:

```text
aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0 4c00 8000
```

Truth table:

```text
a b | p  gold
0 0 | 00 00
1 0 | 00 00
2 0 | 00 00
3 0 | 00 00
0 1 | 00 00
1 1 | 01 01
2 1 | 02 02
3 1 | 03 03
0 2 | 00 00
1 2 | 02 02
2 2 | 04 04
3 2 | 06 06
0 3 | 00 00
1 3 | 03 03
2 3 | 06 06
3 3 | 09 09
```

## Host Gate

`tests/compare_cgp_twin.py` verifies:

- Python golden genome scores `64/64`.
- C golden genome scores `64/64`.
- Python GA and C GA CSV curves are byte-for-byte identical.
- Final row reaches `64/64` bits and `16/16` truth-table rows.

This is host-only. The EHW-1.1 board step still needs `rtl/cgp_vrc.v` and a
board-side evaluator.
