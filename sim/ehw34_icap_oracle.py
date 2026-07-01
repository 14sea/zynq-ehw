#!/usr/bin/env python3
"""EHW-3.4 micro oracle: per-eval ICAPE2 spare-routing candidate bank.

This is the host model for the stretch flow that combines EHW-2's per-eval
internal-ICAPE2 loop with the EHW-3 spare-routing genome. The board version
materializes each candidate by streaming that candidate's frame sequences through
rtl/xbus_icap.v, then sweeps the live island's 8 input rows.

The oracle keeps the candidate bank deliberately tiny. It does not claim that the
NEORV32 can synthesize arbitrary new frame sequences on board; the framebank still
comes from fresh same-route bitstream diffs.
"""

from __future__ import annotations

import argparse
import csv
import sys

import oracle_spare_routing as sr


CANDIDATES: list[tuple[str, list[int]]] = [
    ("base",   [0x0A, 0x08, 0x01, 0x0F, 0x32, 0x01, 0x04, 0x00,
                0x02, 0x02, 0x00, 0x04, 0x01, 0x01, 0x02, 0x00]),
    ("logic",  [0x0B, 0x09, 0x09, 0x03, 0xB1, 0x01, 0x04, 0x00,
                0x02, 0x02, 0x00, 0x04, 0x01, 0x01, 0x02, 0x00]),
    ("route",  [0x0A, 0x08, 0x01, 0x0F, 0x32, 0x00, 0x04, 0x04,
                0x01, 0x02, 0x00, 0x00, 0x01, 0x02, 0x03, 0x00]),
    ("repair", [0x0B, 0x09, 0x09, 0x03, 0xB1, 0x00, 0x04, 0x04,
                0x01, 0x02, 0x00, 0x00, 0x01, 0x02, 0x03, 0x00]),
]


def genome_hex(genome: list[int]) -> str:
    return "".join(f"{b:02x}" for b in genome)


def rows() -> list[dict[str, int | str]]:
    out: list[dict[str, int | str]] = []
    best_index = 0
    best_fit = -1
    for idx, (label, genome) in enumerate(CANDIDATES):
        observed = sr.truth_mask(genome, sr.FAULT_DISABLE_A1)
        fit = sr.fitness(genome, sr.FAULT_DISABLE_A1)
        if fit > best_fit:
            best_fit = fit
            best_index = idx
        out.append({
            "eval": idx,
            "label": label,
            "genome": genome_hex(genome),
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
            fieldnames=["eval", "label", "genome", "observed", "fitness", "best_index", "best_fitness"],
            lineterminator="\n",
        )
        writer.writeheader()
        for row in rows():
            writer.writerow({
                **row,
                "observed": f"{int(row['observed']):02x}",
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
        target = sr.TARGET_MASK
        got = rows()[-1]
        print(f"target=0x{target:02x} final={got['label']} observed=0x{int(got['observed']):02x}")
        return 0 if int(got["observed"]) == target and int(got["fitness"]) == sr.FITNESS_MAX else 1

    write_csv(args.csv)
    best = rows()[-1]
    print(
        f"EHW3.4 micro best={best['label']} observed=0x{int(best['observed']):02x} "
        f"fitness={best['best_fitness']}/8 csv={args.csv or '<stdout>'}"
    )
    return 0 if int(best["best_fitness"]) == sr.FITNESS_MAX else 1


if __name__ == "__main__":
    raise SystemExit(main())
