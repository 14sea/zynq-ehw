#!/usr/bin/env python3
"""EHW-2 micro oracle: staged LUT-INIT candidates, per-eval ICAP model.

The board firmware evaluates the same candidate bank, except each candidate's
truth table is materialized by an ICAPE2 frame write before scoring. The target
function is 3-input majority over rows 0..7, encoded as low byte 0xe8.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path


TARGET_INIT = 0xE8
CANDIDATE_INITS = [0x00, 0x80, 0xA8, 0xE8]


def majority3_mask() -> int:
    mask = 0
    for row in range(8):
        bits = ((row >> 0) & 1) + ((row >> 1) & 1) + ((row >> 2) & 1)
        if bits >= 2:
            mask |= 1 << row
    return mask


def fitness(observed: int) -> int:
    return 8 - ((observed ^ TARGET_INIT) & 0xFF).bit_count()


def rows() -> list[dict[str, int]]:
    out: list[dict[str, int]] = []
    best_index = 0
    best_fit = -1
    for idx, init in enumerate(CANDIDATE_INITS):
        observed = init
        fit = fitness(observed)
        if fit > best_fit:
            best_fit = fit
            best_index = idx
        out.append({
            "eval": idx,
            "candidate_init": init,
            "observed": observed,
            "fitness": fit,
            "best_index": best_index,
            "best_fitness": best_fit,
        })
    return out


def write_csv(path: str | None) -> None:
    fp = open(path, "w", newline="") if path else sys.stdout
    try:
        writer = csv.DictWriter(
            fp,
            fieldnames=["eval", "candidate_init", "observed", "fitness", "best_index", "best_fitness"],
            lineterminator="\n",
        )
        writer.writeheader()
        for row in rows():
            writer.writerow({
                **row,
                "candidate_init": f"{row['candidate_init']:02x}",
                "observed": f"{row['observed']:02x}",
            })
    finally:
        if path:
            fp.close()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv")
    ap.add_argument("--check-target", action="store_true")
    args = ap.parse_args()

    if args.check_target:
        got = majority3_mask()
        print(f"target=0x{got:02x}")
        return 0 if got == TARGET_INIT else 1

    write_csv(args.csv)
    best = rows()[-1]
    print(
        f"EHW2 micro best init=0x{best['candidate_init']:02x} "
        f"fitness={best['best_fitness']}/8 csv={args.csv or '<stdout>'}"
    )
    return 0 if best["best_fitness"] == 8 else 1


if __name__ == "__main__":
    raise SystemExit(main())
