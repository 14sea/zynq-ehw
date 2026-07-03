#!/usr/bin/env python3
"""EHW-4.1 host gate: compare memetic Python oracle and portable-C twin."""

from __future__ import annotations

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
    exe = out_dir / "memetic_eval"
    py_curve = out_dir / "ehw4_python_curves.csv"
    py_summary = out_dir / "ehw4_python_summary.csv"
    py_doc = out_dir / "ehw4_python_results.md"
    c_curve = out_dir / "ehw4_c_curves.csv"
    c_summary = out_dir / "ehw4_c_summary.csv"

    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-I", "sw/ehw",
         "-o", str(exe), "sw/ehw/memetic_eval.c"])
    common = [
        "--seed", "3",
        "--population", "16",
        "--generations", "32",
        "--adapt-epochs", "2",
    ]
    run([sys.executable, "sim/oracle_memetic.py", *common,
         "--curve-csv", str(py_curve), "--summary-csv", str(py_summary), "--doc", str(py_doc)])
    run([str(exe), *common, "--curve-csv", str(c_curve), "--summary-csv", str(c_summary)])

    if py_curve.read_bytes() != c_curve.read_bytes():
        print(f"FAIL: curve CSV mismatch: {py_curve} != {c_curve}", file=sys.stderr)
        return 1

    # Python EHW-4.0 summary intentionally has no sat_count; compare the common prefix
    # and then verify the C saturation count for the reviewer-noted Lamarckian boundary case.
    py_lines = py_summary.read_text().splitlines()
    c_lines = c_summary.read_text().splitlines()
    py_header = py_lines[0] + ",sat_count"
    if c_lines[0] != py_header:
        print("FAIL: summary header mismatch", file=sys.stderr)
        return 1
    for py, c in zip(py_lines[1:], c_lines[1:]):
        if not c.startswith(py + ","):
            print("FAIL: summary row mismatch", file=sys.stderr)
            print("PY:", py, file=sys.stderr)
            print("C :", c, file=sys.stderr)
            return 1
    lamarck = [line for line in c_lines[1:] if line.startswith("lamarckian,")][0]
    sat_count = int(lamarck.rsplit(",", 1)[1])
    if sat_count <= 0:
        print("FAIL: expected Lamarckian boundary saturation to be tracked", file=sys.stderr)
        return 1

    print("PASS: EHW-4.1 Py<->C memetic curves are bit-exact")
    print(f"PASS: Lamarckian sat_count={sat_count}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
