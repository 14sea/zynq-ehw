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

    seq_paths = ["00:-"]
    for idx, init in enumerate(("00", "80", "a8", "e8")):
        path = out_dir / f"fake_{init}.seq.bin"
        path.write_bytes(struct.pack(">3I", 0xAA995566, idx, int(init, 16)))
        if init == "80":
            seq_paths.append(f"{init}:{path}")
        elif init in ("a8", "e8"):
            path2 = out_dir / f"fake_{init}_2.seq.bin"
            path2.write_bytes(struct.pack(">3I", 0xAA995566, idx + 16, int(init, 16)))
            seq_paths.append(f"{init}:{path},{path2}")
    bank = out_dir / "framebank.bin"
    run([sys.executable, "scripts/ehw2-framebank-pack.py", "--out", str(bank), *seq_paths])
    words = struct.unpack(">2048I", bank.read_bytes())
    if (
        words[0] != 0x45485732 or words[1] != 4 or words[3] != 6 or
        words[4] != 0x00 or words[5] != 0 or
        words[10] != 0x80 or words[11] != 1 or words[12] != 28 or words[13] != 3 or
        words[16] != 0xA8 or words[17] != 2 or words[18] != 31 or words[20] != 34 or
        words[22] != 0xE8 or words[23] != 2 or words[24] != 37 or words[26] != 40
    ):
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
