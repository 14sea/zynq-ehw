#!/usr/bin/env python3
"""EHW-4.6a host gate: compile-time memetic sweep firmware vs C reference."""

from __future__ import annotations

import csv
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HEADER = [
    "point",
    "mode",
    "population",
    "generations",
    "adapt_epochs",
    "seed",
    "best_correct",
    "best_sse",
    "best_fitness",
    "first_40",
    "sat_count",
]
TABLE = [
    (0, 8, 8, 1),
    (1, 8, 16, 1),
    (2, 8, 32, 1),
    (3, 16, 8, 1),
    (4, 16, 16, 1),
    (5, 16, 32, 1),
    (6, 32, 8, 1),
    (7, 32, 16, 1),
    (8, 32, 32, 1),
    (9, 16, 8, 2),
    (10, 16, 16, 2),
    (11, 16, 32, 2),
]
MODES = ("baldwinian", "lamarckian")


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def summary_rows(path: Path) -> dict[str, dict[str, str]]:
    with path.open(newline="") as f:
        rows = {row["mode"]: row for row in csv.DictReader(f)}
    missing = [mode for mode in MODES if mode not in rows]
    if missing:
        raise AssertionError(f"missing reference modes: {missing}")
    return rows


def write_reference(ref_exe: Path, out_dir: Path, dst: Path) -> None:
    with dst.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=HEADER, lineterminator="\n")
        writer.writeheader()
        for point, population, generations, adapt_epochs in TABLE:
            curve = out_dir / f"ehw4_6a_ref_p{point}_curves.csv"
            summary = out_dir / f"ehw4_6a_ref_p{point}_summary.csv"
            run([
                str(ref_exe),
                "--seed", "3",
                "--population", str(population),
                "--generations", str(generations),
                "--adapt-epochs", str(adapt_epochs),
                "--curve-csv", str(curve),
                "--summary-csv", str(summary),
            ])
            rows = summary_rows(summary)
            for mode in MODES:
                row = rows[mode]
                writer.writerow({
                    "point": point,
                    "mode": mode,
                    "population": population,
                    "generations": generations,
                    "adapt_epochs": adapt_epochs,
                    "seed": 3,
                    "best_correct": row["best_correct"],
                    "best_sse": row["best_sse"],
                    "best_fitness": row["best_fitness"],
                    "first_40": row["first_40"],
                    "sat_count": row["sat_count"],
                })


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    ref_exe = out_dir / "memetic_eval_ref"
    fw_exe = out_dir / "memetic_sweep_mbox"
    ref_csv = out_dir / "ehw4_6a_ref_sweep.csv"
    fw_csv = out_dir / "ehw4_6a_fw_sweep.csv"

    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-I", "sw/ehw",
         "-o", str(ref_exe), "sw/ehw/memetic_eval.c"])
    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DMEMETIC_SWEEP_HOST_STUB",
         "-I", "sw/ehw", "-o", str(fw_exe), "sw/ehw/memetic_sweep_mbox.c"])
    write_reference(ref_exe, out_dir, ref_csv)
    run([str(fw_exe), "--summary-csv", str(fw_csv)])

    if ref_csv.read_bytes() != fw_csv.read_bytes():
        print(f"FAIL: sweep CSV mismatch: {ref_csv} != {fw_csv}", file=sys.stderr)
        return 1

    rows = list(csv.DictReader(fw_csv.open(newline="")))
    if len(rows) != len(TABLE) * len(MODES):
        print(f"FAIL: expected {len(TABLE) * len(MODES)} rows, got {len(rows)}", file=sys.stderr)
        return 1
    if max(int(row["population"]) for row in rows) != 32:
        print("FAIL: expected POP=32 points in sweep table", file=sys.stderr)
        return 1
    finals_40 = sum(1 for row in rows if row["best_correct"] == "40")
    if finals_40 == 0:
        print("FAIL: no sweep point reached 40/40", file=sys.stderr)
        return 1

    print("PASS: EHW-4.6a compile-time sweep firmware summary is byte-exact vs memetic_eval")
    print(f"PASS: {len(TABLE)} points x {len(MODES)} modes; {finals_40} rows reached 40/40")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
