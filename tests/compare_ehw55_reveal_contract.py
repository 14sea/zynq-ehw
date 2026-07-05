#!/usr/bin/env python3
"""EHW-5.5 ICAP-reveal host contract.

This is deliberately not a board claim. It freezes the structural champion that
an optional EHW-5.5 reveal should bake into a no-fault spare-route island and the
exact primitive INIT edits expected from the MS_SR_MAJORITY baseline.
"""

from __future__ import annotations


EHW_TEST_X = [
    [5, 12, 12, 6], [11, 18, 18, 16], [2, 13, 14, 2], [1, 7, 6, 2],
    [1, 6, 3, 4], [6, 3, 0, 7], [10, 15, 15, 11], [5, 8, 9, 7],
    [1, 11, 9, 3], [18, 15, 15, 16], [1, 11, 11, 1], [11, 10, 11, 13],
    [7, 12, 12, 9], [10, 9, 8, 9], [4, 3, 3, 6], [1, 12, 12, 2],
    [7, 6, 7, 7], [7, 12, 11, 10], [3, 8, 6, 5], [0, 11, 8, 4],
    [9, 12, 12, 10], [5, 4, 4, 6], [12, 11, 12, 13], [10, 6, 9, 9],
    [9, 9, 10, 9], [2, 6, 2, 7], [0, 9, 9, 2], [2, 5, 1, 7],
    [3, 3, 3, 4], [3, 6, 1, 9], [10, 13, 12, 14], [8, 14, 12, 12],
    [1, 8, 8, 2], [9, 10, 10, 9], [0, 6, 5, 3], [1, 12, 10, 3],
    [8, 10, 9, 8], [1, 10, 10, 2], [13, 10, 10, 13], [8, 16, 16, 15],
]

BASE = [0x0A, 0x0A, 0x0A, 0x00, 0xE8, 0, 0, 1, 1, 2, 2, 3, 3, 0, 1, 2]
CHAMP = [0x08, 0x00, 0x0A, 0x00, 0xE8, 0, 0, 3, 3, 2, 2, 3, 1, 0, 3, 2]
EXPECTED_CHANGED = {"g0", "g1", "g7", "g8", "g12", "g14"}
EXPECTED_TRUTH_MASK = 0xA0
EXPECTED_FEATURE_MASK = 0xD2C1D02A42
EXPECTED_FEATURE_ONES = 15


def lut2(init: int, in0: int, in1: int) -> int:
    return (init >> ((in1 << 1) | in0)) & 1


def lut3(init: int, in0: int, in1: int, in2: int) -> int:
    return (init >> ((in2 << 2) | (in1 << 1) | in0)) & 1


def decode_node_sel(raw: int) -> int:
    return raw if raw < 5 else 3


def decode_out_sel(raw: int) -> int:
    return raw if raw < 4 else 0


def pool_bit(row: int, sel: int) -> int:
    if sel == 0:
        return row & 1
    if sel == 1:
        return (row >> 1) & 1
    if sel == 2:
        return (row >> 2) & 1
    if sel == 4:
        return 1
    return 0


def eval_row(genome: list[int], row: int) -> int:
    nodes = []
    for node in range(4):
        s0 = decode_node_sel(genome[5 + 2 * node])
        s1 = decode_node_sel(genome[6 + 2 * node])
        nodes.append(lut2(genome[node], pool_bit(row, s0), pool_bit(row, s1)))
    o0 = nodes[decode_out_sel(genome[13])]
    o1 = nodes[decode_out_sel(genome[14])]
    o2 = nodes[decode_out_sel(genome[15])]
    return lut3(genome[4], o0, o1, o2)


def truth_mask(genome: list[int]) -> int:
    mask = 0
    for row in range(8):
        mask |= eval_row(genome, row) << row
    return mask


def feature_mask(genome: list[int]) -> int:
    mask = 0
    for i, x in enumerate(EHW_TEST_X):
        row = (x[0] >= 8) | ((x[1] >= 8) << 1) | ((x[2] >= 8) << 2)
        mask |= eval_row(genome, row) << i
    return mask


def pool_mux_init(sel: int) -> int:
    if sel == 0:
        return 0xAAAA_AAAA_AAAA_AAAA
    if sel == 1:
        return 0xCCCC_CCCC_CCCC_CCCC
    if sel == 2:
        return 0xF0F0_F0F0_F0F0_F0F0
    if sel == 4:
        return 0xFFFF_0000_FFFF_0000
    return 0


def out_mux_init(sel: int) -> int:
    if sel == 0:
        return 0xAAAA
    if sel == 1:
        return 0xCCCC
    if sel == 2:
        return 0xF0F0
    if sel == 3:
        return 0xFF00
    return 0xAAAA


def primitive_inits(genome: list[int]) -> dict[str, tuple[int, int]]:
    table: dict[str, tuple[int, int]] = {}
    for i in range(4):
        table[f"g{i}"] = (4, genome[i] & 0xF)
    table["g4"] = (8, genome[4] & 0xFF)
    for i in range(5, 13):
        table[f"g{i}"] = (64, pool_mux_init(genome[i]))
    for i in range(13, 16):
        table[f"g{i}"] = (16, out_mux_init(genome[i]))
    return table


def main() -> int:
    if truth_mask(CHAMP) != EXPECTED_TRUTH_MASK:
        raise SystemExit(f"truth mask changed: got 0x{truth_mask(CHAMP):02x}")
    fm = feature_mask(CHAMP)
    if fm != EXPECTED_FEATURE_MASK:
        raise SystemExit(f"feature mask changed: got 0x{fm:010x}")
    if fm.bit_count() != EXPECTED_FEATURE_ONES:
        raise SystemExit(f"feature ones changed: got {fm.bit_count()}")

    base = primitive_inits(BASE)
    champ = primitive_inits(CHAMP)
    changed = {name for name in base if base[name] != champ[name]}
    if changed != EXPECTED_CHANGED:
        raise SystemExit(f"INIT diff changed: got {sorted(changed)}, expected {sorted(EXPECTED_CHANGED)}")

    print("EHW-5.5 structural champion genome:", " ".join(f"{b:02x}" for b in CHAMP))
    print(f"PASS: truth_mask=0x{EXPECTED_TRUTH_MASK:02x} feature_mask=0x{EXPECTED_FEATURE_MASK:010x} ones={EXPECTED_FEATURE_ONES}")
    print("PASS: EHW-5.5 baseline->champion INIT diff cells:", " ".join(sorted(changed, key=lambda n: int(n[1:]))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
