#!/usr/bin/env python3
"""Pack EHW-2 candidate ICAP frame sequences into one 8KB framebank image.

Layout in 32-bit words:
  0      magic "EHW2" (written last by the loader as the ready flag)
  1      candidate count
  2      descriptor base word offset
  3      descriptor words per candidate
  4..    descriptors: candidate_init, nseq, seq0_offset, seq0_len, seq1_offset, seq1_len
  ...    big-endian ICAP sequence words copied from *.seq.bin

Example:
  ehw2-framebank-pack.py --out runs/ehw2/framebank.bin \\
    00:- \\
    80:seq_80_d23.bin \\
    a8:seq_a8_d22.bin,seq_a8_d23.bin \\
    e8:seq_e8_d22.bin,seq_e8_d23.bin
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


MAGIC = 0x45485732
DESC_BASE = 4
DESC_WORDS = 6
MAX_WORDS = 2048
MAX_CAND = 4
MAX_SEQ_PER_CAND = 2
MAX_SEQ_WORDS = 255


def read_words(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) % 4:
        raise ValueError(f"{path}: length is not a multiple of 4")
    if not data:
        raise ValueError(f"{path}: empty sequence")
    return list(struct.unpack(f">{len(data) // 4}I", data))


def parse_candidate(spec: str) -> tuple[int, list[Path]]:
    try:
        init_s, path_s = spec.split(":", 1)
    except ValueError as exc:
        raise ValueError(f"candidate must be INIT_HEX:path[,path2] or INIT_HEX:-, got {spec!r}") from exc
    init = int(init_s, 16)
    if init < 0 or init > 0xFF:
        raise ValueError(f"{spec}: INIT must fit in one byte")
    if path_s in ("", "-"):
        return init, []
    paths = [Path(p) for p in path_s.split(",") if p]
    if len(paths) > MAX_SEQ_PER_CAND:
        raise ValueError(f"{spec}: at most {MAX_SEQ_PER_CAND} sequences per candidate")
    return init, paths


def pack(candidates: list[tuple[int, list[Path]]]) -> list[int]:
    if not candidates or len(candidates) > MAX_CAND:
        raise ValueError(f"expected 1..{MAX_CAND} candidates")

    words = [0] * (DESC_BASE + len(candidates) * DESC_WORDS)
    words[0] = MAGIC
    words[1] = len(candidates)
    words[2] = DESC_BASE
    words[3] = DESC_WORDS

    for idx, (init, paths) in enumerate(candidates):
        desc = DESC_BASE + idx * DESC_WORDS
        words[desc + 0] = init
        words[desc + 1] = len(paths)
        for seq_idx, path in enumerate(paths):
            seq = read_words(path)
            if len(seq) > MAX_SEQ_WORDS:
                raise ValueError(f"{path}: {len(seq)} words exceeds xbus_icap 255-word burst limit")
            seq_off = len(words)
            words[desc + 2 + seq_idx * 2] = seq_off
            words[desc + 3 + seq_idx * 2] = len(seq)
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
        seqs = []
        for seq_idx in range(words[desc + 1]):
            seqs.append(f"off={words[desc + 2 + seq_idx * 2]} len={words[desc + 3 + seq_idx * 2]}")
        print(
            f"  cand{idx}: init=0x{words[desc + 0]:02x} "
            f"nseq={words[desc + 1]} {'; '.join(seqs)}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
