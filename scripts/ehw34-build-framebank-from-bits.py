#!/usr/bin/env python3
"""Build an EHW-3.4 framebank from same-route spare-route candidate bitstreams."""

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


def parse_candidate(spec: str) -> tuple[str, str]:
    try:
        label, genome = spec.split("=", 1)
    except ValueError as exc:
        raise ValueError(f"candidate must be label=16bytehex, got {spec!r}") from exc
    if not label:
        raise ValueError(f"{spec}: empty label")
    compact = "".join(ch for ch in genome if ch not in " _,-:")
    if len(compact) != 32:
        raise ValueError(f"{spec}: genome must be 16 bytes / 32 hex chars")
    return label, compact.lower()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--base-label", default="base")
    ap.add_argument("--bit-template", required=True, help="format string with {label}")
    ap.add_argument("--bits-template", required=True, help="format string with {label}")
    ap.add_argument("--candidate", action="append", required=True, help="label=16bytehex")
    args = ap.parse_args()

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    candidates = [parse_candidate(c) for c in args.candidate]
    by_label = {label: genome for label, genome in candidates}
    if args.base_label not in by_label:
        raise SystemExit(f"base label {args.base_label!r} must be one of the candidates")

    base_bit = Path(args.bit_template.format(label=args.base_label))
    base_bits = read_lines(Path(args.bits_template.format(label=args.base_label)))
    specs = [f"{args.base_label}={by_label[args.base_label]}:-"]

    for label, genome in candidates:
        if label == args.base_label:
            continue
        cand_bits = read_lines(Path(args.bits_template.format(label=label)))
        set_path = out_dir / f"set_{label}.txt"
        clr_path = out_dir / f"clr_{label}.txt"
        write_diff(set_path, cand_bits - base_bits)
        write_diff(clr_path, base_bits - cand_bits)

        cand_dir = out_dir / f"cand_{label}"
        cand_dir.mkdir(exist_ok=True)
        subprocess.run([
            sys.executable,
            str(ROOT / "scripts" / "m75-build-frameseqs.py"),
            str(base_bit),
            str(Path(args.bit_template.format(label=label))),
            str(set_path),
            str(clr_path),
            str(cand_dir),
        ], cwd=ROOT, check=True)

        seqs = sorted(cand_dir.glob("m75_frame_*.seq.bin"))
        specs.append(f"{label}={genome}:" + (",".join(str(p) for p in seqs) if seqs else "-"))

    subprocess.run([
        sys.executable,
        str(ROOT / "scripts" / "ehw34-framebank-pack.py"),
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
