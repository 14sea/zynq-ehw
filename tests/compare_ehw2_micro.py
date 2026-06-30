#!/usr/bin/env python3
"""EHW-2 host gate: Python oracle, C firmware stub, and framebank pack sanity."""

from __future__ import annotations

import os
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests" / "ehw2"
    out_dir.mkdir(parents=True, exist_ok=True)
    py_csv = out_dir / "ehw2_python.csv"
    c_csv = out_dir / "ehw2_c.csv"
    exe = out_dir / "ehw2_icap_micro"

    run([sys.executable, "sim/ehw2_micro_oracle.py", "--check-target"])
    run([sys.executable, "sim/ehw2_micro_oracle.py", "--csv", str(py_csv)])
    run([
        cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DEHW2_HOST_STUB",
        "-o", str(exe), "sw/ehw/ehw2_icap_micro.c",
    ])
    run([str(exe), "--check-target"])
    run([str(exe), "--csv", str(c_csv)])

    if py_csv.read_bytes() != c_csv.read_bytes():
        print(f"FAIL: CSV mismatch: {py_csv} != {c_csv}", file=sys.stderr)
        return 1

    seq_paths = []
    for idx, init in enumerate(("00", "80", "a8", "e8")):
        path = out_dir / f"fake_{init}.seq.bin"
        path.write_bytes(struct.pack(">3I", 0xAA995566, idx, int(init, 16)))
        seq_paths.append(f"{init}:{path}")
    bank = out_dir / "framebank.bin"
    run([sys.executable, "scripts/ehw2-framebank-pack.py", "--out", str(bank), *seq_paths])
    words = struct.unpack(">1024I", bank.read_bytes())
    if words[0] != 0x45485732 or words[1] != 4 or words[4] != 20 or words[5] != 3:
        print("FAIL: packed framebank header/descriptor is wrong", file=sys.stderr)
        return 1

    last = py_csv.read_text().splitlines()[-1]
    if last != "3,e8,e8,8,3,8":
        print(f"FAIL: unexpected final oracle row: {last}", file=sys.stderr)
        return 1

    print("PASS: EHW-2 micro oracle, C host stub, and framebank pack agree")
    print(f"last: {last}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
