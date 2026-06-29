#!/usr/bin/env python3
"""Build and compare the EHW-1.0 CGP Python oracle against the C twin."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--population", type=int, default=64)
    ap.add_argument("--generations", type=int, default=200)
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args()

    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / "cgp_eval"
    py_csv = out_dir / "cgp_python.csv"
    c_csv = out_dir / "cgp_c.csv"

    run([sys.executable, "sim/oracle_cgp.py", "--check-golden"])
    run([args.cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-o", str(exe), "sw/ehw/cgp_eval.c"])
    run([str(exe), "--check-golden"])
    common = [
        "--seed", str(args.seed),
        "--population", str(args.population),
        "--generations", str(args.generations),
        "--quiet",
    ]
    run([sys.executable, "sim/oracle_cgp.py", *common, "--csv", str(py_csv)])
    run([str(exe), *common, "--csv", str(c_csv)])

    if py_csv.read_bytes() != c_csv.read_bytes():
        print(f"FAIL: CSV mismatch: {py_csv} != {c_csv}", file=sys.stderr)
        return 1

    lines = py_csv.read_text().splitlines()
    last = lines[-1] if len(lines) > 1 else "<empty>"
    if ",64,16," not in last:
        print(f"FAIL: CGP did not reach 64/64 and 16/16 rows: {last}", file=sys.stderr)
        return 1

    print(f"PASS: CGP Python oracle and C twin are bit-exact ({len(lines) - 1} generations logged)")
    print(f"last: {last}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
