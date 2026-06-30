#!/usr/bin/env python3
"""Pack EHW-2 candidate ICAP frame sequences into one 4KB framebank image.

Layout in 32-bit words:
  0      magic "EHW2" (written last by the loader as the ready flag)
  1      candidate count
  2      descriptor base word offset
  3      descriptor words per candidate
  4..    descriptors: seq_offset, seq_len, candidate_init, reserved
  ...    big-endian ICAP sequence words copied from *.seq.bin

Example:
  ehw2-framebank-pack.py --out runs/ehw2/framebank.bin \\
    00:seq_00.bin 80:seq_80.bin a8:seq_a8.bin e8:seq_e8.bin
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


MAGIC = 0x45485732
DESC_BASE = 4
DESC_WORDS = 4
MAX_WORDS = 1024
MAX_CAND = 4
MAX_SEQ_WORDS = 255


def read_words(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) % 4:
        raise ValueError(f"{path}: length is not a multiple of 4")
    if not data:
        raise ValueError(f"{path}: empty sequence")
    return list(struct.unpack(f">{len(data) // 4}I", data))


def parse_candidate(spec: str) -> tuple[int, Path]:
    try:
        init_s, path_s = spec.split(":", 1)
    except ValueError as exc:
        raise ValueError(f"candidate must be INIT_HEX:path, got {spec!r}") from exc
    init = int(init_s, 16)
    if init < 0 or init > 0xFF:
        raise ValueError(f"{spec}: INIT must fit in one byte")
    return init, Path(path_s)


def pack(candidates: list[tuple[int, Path]]) -> list[int]:
    if not candidates or len(candidates) > MAX_CAND:
        raise ValueError(f"expected 1..{MAX_CAND} candidates")

    words = [0] * (DESC_BASE + len(candidates) * DESC_WORDS)
    words[0] = MAGIC
    words[1] = len(candidates)
    words[2] = DESC_BASE
    words[3] = DESC_WORDS

    for idx, (init, path) in enumerate(candidates):
        seq = read_words(path)
        if len(seq) > MAX_SEQ_WORDS:
            raise ValueError(f"{path}: {len(seq)} words exceeds xbus_icap 255-word burst limit")
        seq_off = len(words)
        desc = DESC_BASE + idx * DESC_WORDS
        words[desc + 0] = seq_off
        words[desc + 1] = len(seq)
        words[desc + 2] = init
        words[desc + 3] = 0
        words.extend(seq)

    if len(words) > MAX_WORDS:
        raise ValueError(f"framebank uses {len(words)} words, exceeds {MAX_WORDS}")
    words.extend([0] * (MAX_WORDS - len(words)))
    return words


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("candidate", nargs="+", help="INIT_HEX:path/to/frame.seq.bin")
    args = ap.parse_args()

    candidates = [parse_candidate(spec) for spec in args.candidate]
    words = pack(candidates)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(struct.pack(f">{len(words)}I", *words))
    print(f"packed {len(candidates)} candidates, {len(words)} words -> {out}")
    for idx in range(len(candidates)):
        desc = DESC_BASE + idx * DESC_WORDS
        print(
            f"  cand{idx}: init=0x{words[desc + 2]:02x} "
            f"off={words[desc + 0]} len={words[desc + 1]}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
