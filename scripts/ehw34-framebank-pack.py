#!/usr/bin/env python3
"""Pack EHW-3.4 spare-route genome candidates into one 8KB framebank image.

Layout in 32-bit words:
  0      magic "EH34" (written last by the loader as the ready flag)
  1      candidate count
  2      descriptor base word offset
  3      descriptor words per candidate
  4..    descriptors:
           genome_word0..3, nseq,
           seq0_offset, seq0_len, ... seq15_offset, seq15_len
  ...    big-endian ICAP sequence words copied from *.seq.bin

Candidate spec:
  label=16BYTE_HEX:path/to/seq.bin[,path2]
  label=16BYTE_HEX:-       # base/no-op candidate
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


MAGIC = 0x45483334  # "EH34"
DESC_BASE = 4
MAX_SEQ_PER_CAND = 16
DESC_WORDS = 5 + MAX_SEQ_PER_CAND * 2
MAX_WORDS = 2048
MAX_CAND = 4
MAX_SEQ_WORDS = 255
GENOME_LEN = 16


def read_words(path: Path) -> list[int]:
    data = path.read_bytes()
    if len(data) % 4:
        raise ValueError(f"{path}: length is not a multiple of 4")
    if not data:
        raise ValueError(f"{path}: empty sequence")
    return list(struct.unpack(f">{len(data) // 4}I", data))


def parse_genome_hex(text: str) -> list[int]:
    compact = "".join(ch for ch in text if ch not in " _,-:")
    if len(compact) != GENOME_LEN * 2:
        raise ValueError(f"genome must be {GENOME_LEN} bytes / {GENOME_LEN * 2} hex chars, got {text!r}")
    return [int(compact[i:i + 2], 16) for i in range(0, len(compact), 2)]


def genome_words(genome: list[int]) -> list[int]:
    out = []
    for i in range(0, GENOME_LEN, 4):
        out.append((genome[i] << 24) | (genome[i + 1] << 16) | (genome[i + 2] << 8) | genome[i + 3])
    return out


def parse_candidate(spec: str) -> tuple[str, list[int], list[Path]]:
    try:
        head, path_s = spec.split(":", 1)
        label, genome_s = head.split("=", 1)
    except ValueError as exc:
        raise ValueError(f"candidate must be label=16BYTE_HEX:path[,path2] or label=16BYTE_HEX:-, got {spec!r}") from exc
    if not label:
        raise ValueError(f"{spec}: empty label")
    genome = parse_genome_hex(genome_s)
    if path_s in ("", "-"):
        return label, genome, []
    paths = [Path(p) for p in path_s.split(",") if p]
    if len(paths) > MAX_SEQ_PER_CAND:
        raise ValueError(f"{spec}: at most {MAX_SEQ_PER_CAND} sequences per candidate")
    return label, genome, paths


def pack(candidates: list[tuple[str, list[int], list[Path]]]) -> list[int]:
    if not candidates or len(candidates) > MAX_CAND:
        raise ValueError(f"expected 1..{MAX_CAND} candidates")

    words = [0] * (DESC_BASE + len(candidates) * DESC_WORDS)
    words[0] = MAGIC
    words[1] = len(candidates)
    words[2] = DESC_BASE
    words[3] = DESC_WORDS

    for idx, (_label, genome, paths) in enumerate(candidates):
        desc = DESC_BASE + idx * DESC_WORDS
        for j, word in enumerate(genome_words(genome)):
            words[desc + j] = word
        words[desc + 4] = len(paths)
        for seq_idx, path in enumerate(paths):
            seq = read_words(path)
            if len(seq) > MAX_SEQ_WORDS:
                raise ValueError(f"{path}: {len(seq)} words exceeds xbus_icap 255-word burst limit")
            seq_off = len(words)
            words[desc + 5 + seq_idx * 2] = seq_off
            words[desc + 6 + seq_idx * 2] = len(seq)
            words.extend(seq)

    if len(words) > MAX_WORDS:
        raise ValueError(f"framebank uses {len(words)} words, exceeds {MAX_WORDS}")
    words.extend([0] * (MAX_WORDS - len(words)))
    return words


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("candidate", nargs="+", help="label=16BYTE_HEX:path/to/frame.seq.bin")
    args = ap.parse_args()

    candidates = [parse_candidate(spec) for spec in args.candidate]
    words = pack(candidates)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(struct.pack(f">{len(words)}I", *words))
    print(f"packed {len(candidates)} candidates, {len(words)} words -> {out}")
    for idx, (label, genome, _paths) in enumerate(candidates):
        desc = DESC_BASE + idx * DESC_WORDS
        seqs = []
        for seq_idx in range(words[desc + 4]):
            seqs.append(f"off={words[desc + 5 + seq_idx * 2]} len={words[desc + 6 + seq_idx * 2]}")
        print(
            f"  cand{idx}:{label} genome={''.join(f'{b:02x}' for b in genome)} "
            f"nseq={words[desc + 4]} {'; '.join(seqs)}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
