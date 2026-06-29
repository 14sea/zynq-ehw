#!/usr/bin/env python3
"""EHW-0.0 host oracle: evolve the M7.5.3-lite 4-4-2 INT8 weight genome.

This is the first executable rung of the EHW ladder. It deliberately mirrors the
fixed-point forward path from zynq_xpart's M7.5.3-lite firmware/oracle, but swaps
gradient training for a small deterministic GA.

Genome layout, 24 signed bytes:
  W1[4][4] followed by W2[2][4]

The board-side VRC and later ICAP reveal can consume the same genome: W1 is one
4x4 tile, W2 is a second 4x4 tile with rows 2 and 3 padded to zero.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable
import random


K = 2
WSHIFT = 2
XSHIFT = 2
XSHIFT_H = 3
NIN = 4
NH = 4
NOUT = 2
NTEST = 40
GENOME_LEN = NH * NIN + NOUT * NH

M753_B1 = [10, 8, -12, -11]
M753_B2 = [111, 136]

M753_TEST_X = [
    [5, 12, 12, 6],
    [11, 18, 18, 16],
    [2, 13, 14, 2],
    [1, 7, 6, 2],
    [1, 6, 3, 4],
    [6, 3, 0, 7],
    [10, 15, 15, 11],
    [5, 8, 9, 7],
    [1, 11, 9, 3],
    [18, 15, 15, 16],
    [1, 11, 11, 1],
    [11, 10, 11, 13],
    [7, 12, 12, 9],
    [10, 9, 8, 9],
    [4, 3, 3, 6],
    [1, 12, 12, 2],
    [7, 6, 7, 7],
    [7, 12, 11, 10],
    [3, 8, 6, 5],
    [0, 11, 8, 4],
    [9, 12, 12, 10],
    [5, 4, 4, 6],
    [12, 11, 12, 13],
    [10, 6, 9, 9],
    [9, 9, 10, 9],
    [2, 6, 2, 7],
    [0, 9, 9, 2],
    [2, 5, 1, 7],
    [3, 3, 3, 4],
    [3, 6, 1, 9],
    [10, 13, 12, 14],
    [8, 14, 12, 12],
    [1, 8, 8, 2],
    [9, 10, 10, 9],
    [0, 6, 5, 3],
    [1, 12, 10, 3],
    [8, 10, 9, 8],
    [1, 10, 10, 2],
    [13, 10, 10, 13],
    [8, 16, 16, 15],
]

M753_TEST_Y = [
    0, 0, 1, 1, 1, 1, 0, 0, 1, 0,
    1, 0, 0, 0, 1, 1, 0, 0, 1, 1,
    0, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    0, 0, 1, 0, 1, 1, 0, 1, 0, 0,
]

M753_GOLD_CLS = [
    0, 0, 0, 1, 1, 1, 0, 1, 1, 0,
    1, 0, 0, 0, 1, 1, 1, 0, 1, 1,
    0, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    0, 0, 1, 0, 1, 1, 0, 1, 0, 0,
]

M753_TRAINED_GENOME = [
    3, -3, -3, -2,
    13, 19, 21, 18,
    -3, -3, -1, 0,
    1, 0, -2, -3,
    0, 27, 3, 0,
    14, -14, 5, 13,
]


class PythonRng:
    def __init__(self, seed: int):
        self._rng = random.Random(seed)

    def randint(self, lo: int, hi: int) -> int:
        return self._rng.randint(lo, hi)

    def randrange(self, hi: int) -> int:
        return self._rng.randrange(hi)

    def chance_ppm(self, ppm: int) -> bool:
        return self._rng.randrange(1_000_000) < ppm

    def choice(self, items):
        return self._rng.choice(items)


class XorShift32:
    """Small deterministic RNG that is trivial to mirror in C/NEORV32."""

    def __init__(self, seed: int):
        self.state = seed & 0xFFFFFFFF or 0x6D2B79F5

    def next_u32(self) -> int:
        x = self.state
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.state = x & 0xFFFFFFFF
        return self.state

    def randrange(self, hi: int) -> int:
        if hi <= 0:
            raise ValueError("empty range")
        return self.next_u32() % hi

    def randint(self, lo: int, hi: int) -> int:
        return lo + self.randrange(hi - lo + 1)

    def chance_ppm(self, ppm: int) -> bool:
        return self.randrange(1_000_000) < ppm

    def choice(self, items):
        return items[self.randrange(len(items))]


def make_rng(kind: str, seed: int):
    if kind == "python":
        return PythonRng(seed)
    if kind == "xorshift":
        return XorShift32(seed)
    raise ValueError(f"unknown RNG kind: {kind}")


def rate_to_ppm(rate: float) -> int:
    ppm = int(round(rate * 1_000_000))
    return 0 if ppm < 0 else 1_000_000 if ppm > 1_000_000 else ppm


def clamp_i8(v: int) -> int:
    return -128 if v < -128 else 127 if v > 127 else v


def sat16(v: int) -> int:
    return -32768 if v < -32768 else 32767 if v > 32767 else v


def leaky(v: int) -> int:
    return v if v >= 0 else v >> K


def q8(v: int, shift: int) -> int:
    return clamp_i8((v + (1 << (shift - 1))) >> shift)


def unpack(genome: list[int]) -> tuple[list[list[int]], list[list[int]]]:
    w1_flat = genome[: NH * NIN]
    w2_flat = genome[NH * NIN :]
    w1 = [w1_flat[r * NIN : (r + 1) * NIN] for r in range(NH)]
    w2 = [w2_flat[r * NH : (r + 1) * NH] for r in range(NOUT)]
    return w1, w2


def dot(row: Iterable[int], x: Iterable[int]) -> int:
    return sum(int(a) * int(b) for a, b in zip(row, x))


def forward(genome: list[int], x: list[int]) -> tuple[int, list[int]]:
    w1, w2 = unpack(genome)
    hidden_i8 = []
    for r in range(NH):
        raw = dot(w1[r], x)
        hp = (raw + (1 << 3)) >> 4
        hidden_i8.append(q8(leaky(sat16(hp + M753_B1[r])), XSHIFT_H))

    y = []
    for r in range(NOUT):
        raw = dot(w2[r], hidden_i8)
        yp = (raw + (1 << 2)) >> 3
        y.append(leaky(sat16(yp + M753_B2[r])))
    return (1 if y[1] > y[0] else 0), y


@dataclass(frozen=True)
class Score:
    fitness: int
    correct: int
    sse: int

    @property
    def accuracy(self) -> float:
        return self.correct / NTEST


def evaluate(genome: list[int]) -> Score:
    correct = 0
    sse = 0
    for x, label in zip(M753_TEST_X, M753_TEST_Y):
        pred, y = forward(genome, x)
        correct += int(pred == label)
        for k in range(NOUT):
            target = 256 if k == label else 0
            err = y[k] - target
            sse += (err * err + 128) >> 8
    # Correct classifications dominate; SSE is a deterministic tie-break.
    return Score(correct * 1_000_000 - sse, correct, sse)


def predictions(genome: list[int]) -> list[int]:
    return [forward(genome, x)[0] for x in M753_TEST_X]


def golden_mismatches(genome: list[int]) -> int:
    return sum(int(a != b) for a, b in zip(predictions(genome), M753_GOLD_CLS))


def random_genome(rng, span: int) -> list[int]:
    return [rng.randint(-span, span) for _ in range(GENOME_LEN)]


def mutate(genome: list[int], rng, rate_ppm: int, step: int) -> list[int]:
    out = genome[:]
    changed = False
    for i, value in enumerate(out):
        if rng.chance_ppm(rate_ppm):
            if rng.chance_ppm(250_000):
                value ^= 1 << rng.randrange(8)
                if value >= 128:
                    value -= 256
            else:
                value += rng.randint(-step, step)
            out[i] = clamp_i8(value)
            changed = True
    if not changed:
        i = rng.randrange(len(out))
        out[i] = clamp_i8(out[i] + rng.choice((-1, 1)) * rng.randint(1, step))
    return out


def crossover(a: list[int], b: list[int], rng) -> list[int]:
    return [a[i] if rng.chance_ppm(500_000) else b[i] for i in range(GENOME_LEN)]


def tournament(scored: list[tuple[Score, list[int]]], rng, k: int) -> list[int]:
    best = scored[rng.randrange(len(scored))]
    for _ in range(1, k):
        item = scored[rng.randrange(len(scored))]
        if item[0].fitness > best[0].fitness:
            best = item
    return best[1]


def seed_population(rng, pop_size: int, init_span: int, mutation_rate_ppm: int) -> list[list[int]]:
    pop = [random_genome(rng, init_span) for _ in range(pop_size)]
    # Include the known M7.5.3 trained tile as a positive control. The GA still has
    # to preserve or improve it under the same scoring function.
    pop[0] = M753_TRAINED_GENOME[:]
    for i in range(1, min(8, pop_size)):
        pop[i] = mutate(M753_TRAINED_GENOME, rng, rate_ppm=250_000, step=4)
    return pop


def run_ga(args: argparse.Namespace) -> tuple[list[int], Score]:
    rng = make_rng(args.rng, args.seed)
    mutation_rate_ppm = rate_to_ppm(args.mutation_rate)
    crossover_rate_ppm = rate_to_ppm(args.crossover_rate)
    pop = seed_population(rng, args.population, args.init_span, mutation_rate_ppm)
    best_genome = pop[0][:]
    best_score = evaluate(best_genome)

    writer = None
    csv_file = None
    if args.csv:
        csv_path = Path(args.csv)
        csv_path.parent.mkdir(parents=True, exist_ok=True)
        csv_file = csv_path.open("w", newline="")
        writer = csv.DictWriter(
            csv_file,
            fieldnames=["gen", "best_correct", "best_acc", "best_sse", "best_fitness", "genome"],
            lineterminator="\n",
        )
        writer.writeheader()

    try:
        for gen in range(args.generations + 1):
            scored = sorted(((evaluate(g), g) for g in pop), key=lambda item: item[0].fitness, reverse=True)
            if scored[0][0].fitness > best_score.fitness:
                best_score, best_genome = scored[0][0], scored[0][1][:]

            if writer:
                writer.writerow({
                    "gen": gen,
                    "best_correct": best_score.correct,
                    "best_acc": f"{best_score.accuracy:.4f}",
                    "best_sse": best_score.sse,
                    "best_fitness": best_score.fitness,
                    "genome": " ".join(str(v) for v in best_genome),
                })

            if args.verbose and (gen == 0 or gen == args.generations or gen % args.report_every == 0):
                print(f"gen {gen:4d}: acc={best_score.correct:2d}/{NTEST} sse={best_score.sse:6d} fitness={best_score.fitness}")
            if best_score.correct >= args.target_correct:
                break

            next_pop = [g[:] for _, g in scored[: args.elites]]
            while len(next_pop) < args.population:
                p1 = tournament(scored, rng, args.tournament)
                if rng.chance_ppm(crossover_rate_ppm):
                    p2 = tournament(scored, rng, args.tournament)
                    child = crossover(p1, p2, rng)
                else:
                    child = p1[:]
                next_pop.append(mutate(child, rng, mutation_rate_ppm, args.mutation_step))
            pop = next_pop
    finally:
        if csv_file:
            csv_file.close()

    return best_genome, best_score


def format_tile(genome: list[int]) -> str:
    w1, w2 = unpack(genome)
    w2_pad = w2 + [[0, 0, 0, 0], [0, 0, 0, 0]]
    lines = ["W1 tile:"]
    lines += ["  " + " ".join(f"{v:4d}" for v in row) for row in w1]
    lines.append("W2 tile (padded):")
    lines += ["  " + " ".join(f"{v:4d}" for v in row) for row in w2_pad]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--rng", choices=("xorshift", "python"), default="xorshift")
    ap.add_argument("--population", type=int, default=64)
    ap.add_argument("--generations", type=int, default=200)
    ap.add_argument("--target-correct", type=int, default=40)
    ap.add_argument("--init-span", type=int, default=32)
    ap.add_argument("--elites", type=int, default=2)
    ap.add_argument("--tournament", type=int, default=3)
    ap.add_argument("--crossover-rate", type=float, default=0.70)
    ap.add_argument("--mutation-rate", type=float, default=0.03)
    ap.add_argument("--mutation-step", type=int, default=8)
    ap.add_argument("--report-every", type=int, default=10)
    ap.add_argument("--csv", default="runs/ehw0_oracle.csv")
    ap.add_argument("--quiet", dest="verbose", action="store_false")
    args = ap.parse_args()

    best, score = run_ga(args)
    print("\n== EHW-0.0 best ==")
    print(f"accuracy={score.correct}/{NTEST} ({score.accuracy:.3f}) sse={score.sse} fitness={score.fitness}")
    print(format_tile(best))
    if args.csv:
        print(f"csv={args.csv}")
    return 0 if score.correct >= args.target_correct else 1


if __name__ == "__main__":
    raise SystemExit(main())
