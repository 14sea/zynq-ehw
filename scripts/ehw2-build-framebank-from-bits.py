#!/usr/bin/env python3
"""Build an EHW-2 multi-sequence framebank from candidate .bit/.bits files.

This automates the error-prone hand step after bitread:
  init_00.bits vs init_X.bits -> set/clr lists -> m75-build-frameseqs.py ->
  ehw2-framebank-pack.py.

Example:
  ehw2-build-framebank-from-bits.py --out-dir runs/ehw2_seqs \\
    --bit-template 'vivado/icap_ehw2/build/ehw2_init_{init}.bit' \\
    --bits-template 'vivado/icap_ehw2/ehw2_seqs2/init_{init}.bits' \\
    00 80 a8 e8
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read_lines(path: Path) -> set[str]:
    return set(path.read_text().splitlines())


def write_diff(path: Path, lines: set[str]) -> None:
    path.write_text("\n".join(sorted(lines)) + ("\n" if lines else ""))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--base-init", default="00")
    ap.add_argument("--bit-template", required=True, help="format string with {init}")
    ap.add_argument("--bits-template", required=True, help="format string with {init}")
    ap.add_argument("init", nargs="+", help="candidate INIT bytes, e.g. 00 80 a8 e8")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    base_init = args.base_init.lower()
    inits = [x.lower() for x in args.init]
    if base_init not in inits:
        raise SystemExit(f"base init {base_init} must be one of the candidates")

    base_bit = Path(args.bit_template.format(init=base_init))
    base_bits = read_lines(Path(args.bits_template.format(init=base_init)))
    specs = [f"{base_init}:-"]

    for init in inits:
        if init == base_init:
            continue
        cand_bits = read_lines(Path(args.bits_template.format(init=init)))
        set_path = out_dir / f"set_{init}.txt"
        clr_path = out_dir / f"clr_{init}.txt"
        write_diff(set_path, cand_bits - base_bits)
        write_diff(clr_path, base_bits - cand_bits)

        cand_dir = out_dir / f"cand_{init}"
        cand_dir.mkdir(exist_ok=True)
        subprocess.run([
            sys.executable,
            str(ROOT / "scripts" / "m75-build-frameseqs.py"),
            str(base_bit),
            str(Path(args.bit_template.format(init=init))),
            str(set_path),
            str(clr_path),
            str(cand_dir),
        ], cwd=ROOT, check=True)

        seqs = sorted(cand_dir.glob("m75_frame_*.seq.bin"))
        if not seqs:
            specs.append(f"{init}:-")
        else:
            specs.append(f"{init}:" + ",".join(str(p) for p in seqs))

    subprocess.run([
        sys.executable,
        str(ROOT / "scripts" / "ehw2-framebank-pack.py"),
        "--out",
        str(out_dir / "framebank.bin"),
        *specs,
    ], cwd=ROOT, check=True)

    print("candidate specs:")
    for spec in specs:
        print(" ", spec)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
