#!/usr/bin/env python3
"""EHW-4.4 host gate: train-unit Lamarckian GA firmware stub vs C oracle."""

from __future__ import annotations

import csv
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def filter_lamarckian(src: Path, dst: Path) -> None:
    with src.open(newline="") as inf, dst.open("w", newline="") as outf:
        reader = csv.reader(inf)
        writer = csv.writer(outf, lineterminator="\n")
        header = next(reader)
        writer.writerow(header)
        for row in reader:
            if row and row[0] == "lamarckian":
                writer.writerow(row)


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    ref_exe = out_dir / "memetic_eval_ref"
    fw_exe = out_dir / "memetic_ga_train_mbox"
    ref_all = out_dir / "ehw4_4_ref_all_curves.csv"
    ref_summary = out_dir / "ehw4_4_ref_summary.csv"
    ref_lam = out_dir / "ehw4_4_ref_lamarckian.csv"
    fw_curve = out_dir / "ehw4_4_fw_lamarckian.csv"

    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-I", "sw/ehw",
         "-o", str(ref_exe), "sw/ehw/memetic_eval.c"])
    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DMEMETIC_GA_HOST_STUB",
         "-I", "sw/ehw", "-o", str(fw_exe), "sw/ehw/memetic_ga_train_mbox.c"])
    common = ["--population", "16", "--generations", "8", "--adapt-epochs", "1"]
    run([str(ref_exe), *common, "--curve-csv", str(ref_all), "--summary-csv", str(ref_summary)])
    filter_lamarckian(ref_all, ref_lam)
    run([str(fw_exe), "--curve-csv", str(fw_curve)])

    if ref_lam.read_bytes() != fw_curve.read_bytes():
        print(f"FAIL: firmware train-GA curve mismatch: {ref_lam} != {fw_curve}", file=sys.stderr)
        return 1

    rows = list(csv.DictReader(fw_curve.open(newline="")))
    if len(rows) != 9:
        print(f"FAIL: expected 9 lamarckian rows, got {len(rows)}", file=sys.stderr)
        return 1
    final = rows[-1]
    if final["best_correct"] != "40":
        print(f"FAIL: expected final best_correct=40, got {final['best_correct']}", file=sys.stderr)
        return 1
    first_40 = next((int(r["gen"]) for r in rows if r["best_correct"] == "40"), None)
    if first_40 != 3:
        print(f"FAIL: expected first_40=3, got {first_40}", file=sys.stderr)
        return 1
    print("PASS: EHW-4.4 train-unit Lamarckian GA curve is bit-exact vs memetic_eval")
    print(f"PASS: final best_correct={final['best_correct']} first_40={first_40} sse={final['best_sse']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
