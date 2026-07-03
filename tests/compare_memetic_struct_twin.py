#!/usr/bin/env python3
"""EHW-5.1 host gate: compare hybrid structure+weight Python oracle and C twin."""

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


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / "memetic_struct_eval"
    py_curve = out_dir / "ehw5_struct_python_curves.csv"
    py_summary = out_dir / "ehw5_struct_python_summary.csv"
    py_doc = out_dir / "ehw5_struct_python_results.md"
    c_curve = out_dir / "ehw5_struct_c_curves.csv"
    c_summary = out_dir / "ehw5_struct_c_summary.csv"

    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-I", "sw/ehw",
         "-o", str(exe), "sw/ehw/memetic_struct_eval.c"])
    common = [
        "--seed", "3",
        "--population", "16",
        "--generations", "32",
        "--adapt-epochs", "1",
    ]
    run([sys.executable, "sim/oracle_memetic_struct.py", *common,
         "--curve-csv", str(py_curve), "--summary-csv", str(py_summary), "--doc", str(py_doc)])
    run([str(exe), *common, "--curve-csv", str(c_curve), "--summary-csv", str(c_summary)])

    if py_curve.read_bytes() != c_curve.read_bytes():
        print(f"FAIL: curve CSV mismatch: {py_curve} != {c_curve}", file=sys.stderr)
        return 1
    if py_summary.read_bytes() != c_summary.read_bytes():
        print(f"FAIL: summary CSV mismatch: {py_summary} != {c_summary}", file=sys.stderr)
        return 1

    with py_summary.open(newline="") as f:
        rows = list(csv.DictReader(f))
    pressure_bias = [
        r for r in rows
        if r["mode"] == "hybrid_lamarckian_pressure" and r["coupling"] == "bias_x3"
    ][0]
    if pressure_bias["feature_ones"] != "15" or pressure_bias["feature_penalty"] != "0":
        print("FAIL: pressured bias_x3 feature pressure semantics changed", file=sys.stderr)
        return 1
    pressure_gate = [
        r for r in rows
        if r["mode"] == "hybrid_lamarckian_pressure" and r["coupling"] == "gate_x3"
    ][0]
    if pressure_gate["feature_penalty"] != "400000":
        print("FAIL: pressured gate_x3 penalty was not cross-checked", file=sys.stderr)
        return 1

    print("PASS: EHW-5.1 Py<->C hybrid structure curves are byte-exact")
    print("PASS: structural pressure penalties are byte-exact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
