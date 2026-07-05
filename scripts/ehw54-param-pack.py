#!/usr/bin/env python3
"""Pack an EHW-5.4b parameter-window block.

The binary output is a little-endian word image for the board-verified 4.6b
parameter window:

  PS writes 0x40000000, NEORV32 reads 0xF5000000 + 4*word.

Stage it with:

  python3 scripts/ehw2-framebank-load.py runs/ehw54/param.bin 0x40000000

word0 is the magic, so stage the complete file after the firmware is running or
rewrite word0 last if doing a live update by hand.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


MAGIC = 0xE5400001
DESC = {
    "weight_only_lamarckian:none": (3, 3, 0x02),
    "hybrid_lamarckian_pressure:bias_x3": (1, 2, 0x07),
    "hybrid_no_adapt:gate_x3": (2, 1, 0x01),
    "hybrid_lamarckian:bias_x3": (0, 2, 0x03),
}
PRESETS = {
    "default": [
        "weight_only_lamarckian:none",
        "hybrid_lamarckian_pressure:bias_x3",
        "hybrid_no_adapt:gate_x3",
        "hybrid_lamarckian:bias_x3",
    ],
    "pressure-short": [
        "hybrid_lamarckian_pressure:bias_x3",
    ],
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True, type=Path)
    p.add_argument("--preset", choices=sorted(PRESETS), default="default")
    p.add_argument("--arm", action="append",
                   help="override preset; repeat mode:coupling descriptors")
    p.add_argument("--seed", type=int, default=3)
    p.add_argument("--population", type=int, default=16)
    p.add_argument("--generations", type=int, default=32)
    p.add_argument("--adapt-epochs", type=int, default=1)
    p.add_argument("--feature-min-balance", type=int, default=8)
    p.add_argument("--feature-penalty", type=int, default=50000)
    return p.parse_args()


def main() -> int:
    args = parse_args()
    arms = args.arm if args.arm else PRESETS[args.preset]
    if not 1 <= len(arms) <= 8:
        raise SystemExit("arm count must be 1..8")
    if not 2 <= args.population <= 16:
        raise SystemExit("population must be 2..16 for the current firmware buffers and elite count")
    if not 0 <= args.generations <= 64:
        raise SystemExit("generations must be 0..64")
    if not 0 <= args.adapt_epochs <= 8:
        raise SystemExit("adapt epochs must be 0..8")

    words = [
        MAGIC,
        len(arms),
        args.seed,
        args.population,
        args.generations,
        args.adapt_epochs,
        args.feature_min_balance,
        args.feature_penalty,
    ]
    for arm in arms:
        if arm not in DESC:
            raise SystemExit(f"unknown arm descriptor: {arm}")
        mode, coupling, flags = DESC[arm]
        words.append(mode | (coupling << 8) | (flags << 16))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(b"".join(struct.pack("<I", w & 0xffffffff) for w in words))
    print(f"wrote {len(words)} words -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
