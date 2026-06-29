#!/usr/bin/env python3
"""Build and compare the EHW-0 Python oracle against the C twin."""

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


def check_python_golden() -> None:
    sys.path.insert(0, str(ROOT / "sim"))
    import oracle_evolve as oracle

    mismatches = oracle.golden_mismatches(oracle.M753_TRAINED_GENOME)
    label_correct = oracle.evaluate(oracle.M753_TRAINED_GENOME).correct
    print(f"python golden check: mismatches={mismatches} label_correct={label_correct}/40", flush=True)
    if mismatches != 0:
        raise AssertionError("EHW forward model drifted from M7.5.3 golden classifications")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--generations", type=int, default=64)
    ap.add_argument("--population", type=int, default=32)
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args()

    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / "ehw_ga_eval"
    mbox_exe = out_dir / "ehw_ga_mbox"
    py_csv = out_dir / "ehw0_python.csv"
    c_csv = out_dir / "ehw0_c.csv"
    mbox_csv = out_dir / "ehw0_ga_mbox.csv"

    check_python_golden()
    run([args.cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-o", str(exe), "sw/ehw/ga_eval.c"])
    run([str(exe), "--check-golden"])
    run([args.cc, "-std=c99", "-Wall", "-Wextra", "-DEHW_HOST_STUB", "-I", "sw/ehw",
         f"-DEHW_GA_POP={args.population}", f"-DEHW_GA_GENS={args.generations}",
         "-o", str(mbox_exe), "sw/ehw/ehw_ga_mbox.c"])
    common = [
        "--rng", "xorshift",
        "--generations", str(args.generations),
        "--population", str(args.population),
        "--quiet",
    ]
    run([sys.executable, "sim/oracle_evolve.py", *common, "--csv", str(py_csv)])
    run([str(exe), *common, "--csv", str(c_csv)])
    run([str(mbox_exe), str(mbox_csv)])

    py_data = py_csv.read_bytes()
    c_data = c_csv.read_bytes()
    if py_data != c_data:
        print(f"FAIL: CSV mismatch: {py_csv} != {c_csv}", file=sys.stderr)
        return 1
    mbox_data = mbox_csv.read_bytes()
    if py_data != mbox_data:
        print(f"FAIL: GA mailbox stub mismatch: {py_csv} != {mbox_csv}", file=sys.stderr)
        return 1

    lines = py_csv.read_text().splitlines()
    last = lines[-1] if len(lines) > 1 else "<empty>"
    print(f"PASS: Python oracle, C twin, and GA mailbox stub are bit-exact ({len(lines) - 1} generations logged)")
    print(f"last: {last}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
