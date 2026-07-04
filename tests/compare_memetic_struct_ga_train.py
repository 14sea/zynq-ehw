#!/usr/bin/env python3
"""EHW-5.3 host gate: hybrid structure GA firmware stub vs C twin curve."""

from __future__ import annotations

import csv
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODE = "hybrid_lamarckian_pressure"
COUPLING = "bias_x3"


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def filter_curve(src: Path, dst: Path) -> None:
    with src.open(newline="") as f:
        rows = list(csv.reader(f))
    header, body = rows[0], rows[1:]
    keep = [r for r in body if r[0] == MODE and r[1] == COUPLING]
    with dst.open("w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerow(header)
        w.writerows(keep)


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    ref_exe = out_dir / "memetic_struct_eval"
    fw_exe = out_dir / "memetic_struct_ga_mbox"
    ref_curve_all = out_dir / "ehw5_3_ref_all_curves.csv"
    ref_summary = out_dir / "ehw5_3_ref_summary.csv"
    ref_curve = out_dir / "ehw5_3_ref_curve.csv"
    fw_curve = out_dir / "ehw5_3_fw_curve.csv"

    common = [
        "--seed", "3",
        "--population", "16",
        "--generations", "32",
        "--adapt-epochs", "1",
    ]
    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-I", "sw/ehw",
         "-o", str(ref_exe), "sw/ehw/memetic_struct_eval.c"])
    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DMEMETIC_STRUCT_GA_HOST_STUB",
         "-I", "sw/ehw", "-o", str(fw_exe), "sw/ehw/memetic_struct_ga_mbox.c"])
    run([str(ref_exe), *common, "--curve-csv", str(ref_curve_all), "--summary-csv", str(ref_summary)])
    filter_curve(ref_curve_all, ref_curve)
    run([str(fw_exe), "--curve-csv", str(fw_curve)])

    if ref_curve.read_bytes() != fw_curve.read_bytes():
        print(f"FAIL: EHW-5.3 curve mismatch: {ref_curve} != {fw_curve}", file=sys.stderr)
        return 1

    with ref_summary.open(newline="") as f:
        rows = list(csv.DictReader(f))
    target = [r for r in rows if r["mode"] == MODE and r["coupling"] == COUPLING][0]
    checks = {
        "best_correct": "40",
        "best_sse": "4513",
        "first_40": "2",
        "feature_ones": "15",
        "feature_penalty": "0",
    }
    for key, expected in checks.items():
        if target[key] != expected:
            print(f"FAIL: {key} got {target[key]} expected {expected}", file=sys.stderr)
            return 1

    print("PASS: EHW-5.3 hybrid GA firmware stub curve is byte-exact")
    print("PASS: EHW-5.3 expected summary fields match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
