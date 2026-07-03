#!/usr/bin/env python3
"""EHW-5.0 host gate: deterministic hybrid structure+weight oracle."""

from __future__ import annotations

import csv
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def read_rows(path: Path) -> dict[tuple[str, str], dict[str, str]]:
    with path.open(newline="") as f:
        return {(row["mode"], row["coupling"]): row for row in csv.DictReader(f)}


def main() -> int:
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    a_curve = out_dir / "ehw5_0_a_curves.csv"
    a_summary = out_dir / "ehw5_0_a_summary.csv"
    a_doc = out_dir / "ehw5_0_a.md"
    b_curve = out_dir / "ehw5_0_b_curves.csv"
    b_summary = out_dir / "ehw5_0_b_summary.csv"
    b_doc = out_dir / "ehw5_0_b.md"

    common = [
        "--seed", "3",
        "--population", "16",
        "--generations", "32",
        "--adapt-epochs", "1",
    ]
    run([sys.executable, "sim/oracle_memetic_struct.py", *common,
         "--curve-csv", str(a_curve), "--summary-csv", str(a_summary), "--doc", str(a_doc)])
    run([sys.executable, "sim/oracle_memetic_struct.py", *common,
         "--curve-csv", str(b_curve), "--summary-csv", str(b_summary), "--doc", str(b_doc)])

    if a_curve.read_bytes() != b_curve.read_bytes():
        print("FAIL: curve CSV is not deterministic", file=sys.stderr)
        return 1
    if a_summary.read_bytes() != b_summary.read_bytes():
        print("FAIL: summary CSV is not deterministic", file=sys.stderr)
        return 1

    rows = read_rows(a_summary)
    expected = {
        ("weight_only_lamarckian", "none"): ("40", "6116", "3"),
        ("hybrid_lamarckian", "bias_x3"): ("40", "5837", "5"),
        ("hybrid_lamarckian_pressure", "bias_x3"): ("40", "4513", "2"),
        ("hybrid_no_adapt", "gate_x3"): ("40", "4615", "11"),
    }
    for key, (correct, sse, first_40) in expected.items():
        row = rows.get(key)
        if row is None:
            print(f"FAIL: missing summary row {key}", file=sys.stderr)
            return 1
        got = (row["best_correct"], row["best_sse"], row["first_40"])
        want = (correct, sse, first_40)
        if got != want:
            print(f"FAIL: {key} expected {want}, got {got}", file=sys.stderr)
            return 1
    pressure = rows[("hybrid_lamarckian_pressure", "bias_x3")]
    if pressure["feature_ones"] != "15" or pressure["feature_penalty"] != "0":
        print("FAIL: expected pressure arm to use a non-constant zero-penalty feature", file=sys.stderr)
        return 1

    print("PASS: EHW-5.0 hybrid oracle is deterministic")
    print("PASS: baseline and hybrid known-value rows match")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
