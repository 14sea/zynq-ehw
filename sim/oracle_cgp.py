#!/usr/bin/env python3
"""EHW-1.0 host oracle: CGP-style LUT-INIT evolution for a 2-bit multiplier.

Substrate:
  - 3 columns x 4 rows of LUT4 nodes.
  - Fixed routing, no evolved wires:
      col0 sees primary inputs [a0,a1,b0,b1]
      col1 sees col0 outputs
      col2 sees col1 outputs and drives [p0,p1,p2,p3]
  - Genome is 12 LUT4 INIT words (12 * 16 = 192 bits).

For the first oracle, col0/col1 are a pass-through scaffold and the GA mutates the
four output LUTs in col2. The full 192-bit genome is still evaluated and logged;
EHW-1.1 can decide whether to expose the scaffold INITs to board-side mutation.
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


ROWS = 4
COLS = 3
NODES = ROWS * COLS
GENOME_LEN = NODES
TRUTH_ROWS = 16
FITNESS_MAX = 64


class XorShift32:
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
        return self.next_u32() % hi

    def randint(self, lo: int, hi: int) -> int:
        return lo + self.randrange(hi - lo + 1)

    def chance_ppm(self, ppm: int) -> bool:
        return self.randrange(1_000_000) < ppm


def lut_input_init(input_idx: int) -> int:
    init = 0
    for idx in range(16):
        if (idx >> input_idx) & 1:
            init |= 1 << idx
    return init


PASS_THROUGH = [lut_input_init(i) for i in range(4)]


def golden_output_bit(bit: int, idx: int) -> int:
    a0 = (idx >> 0) & 1
    a1 = (idx >> 1) & 1
    b0 = (idx >> 2) & 1
    b1 = (idx >> 3) & 1
    product = (a0 | (a1 << 1)) * (b0 | (b1 << 1))
    return (product >> bit) & 1


def golden_lut(bit: int) -> int:
    init = 0
    for idx in range(16):
        if golden_output_bit(bit, idx):
            init |= 1 << idx
    return init


GOLDEN_GENOME = PASS_THROUGH + PASS_THROUGH + [golden_lut(i) for i in range(4)]


def lut_eval(init: int, inputs: list[int]) -> int:
    idx = inputs[0] | (inputs[1] << 1) | (inputs[2] << 2) | (inputs[3] << 3)
    return (init >> idx) & 1


def eval_grid(genome: list[int], idx: int) -> list[int]:
    signals = [
        (idx >> 0) & 1,
        (idx >> 1) & 1,
        (idx >> 2) & 1,
        (idx >> 3) & 1,
    ]
    for col in range(COLS):
        base = col * ROWS
        signals = [lut_eval(genome[base + row], signals) for row in range(ROWS)]
    return signals


def evaluate(genome: list[int]) -> int:
    correct = 0
    for idx in range(TRUTH_ROWS):
        out = eval_grid(genome, idx)
        for bit in range(4):
            correct += int(out[bit] == golden_output_bit(bit, idx))
    return correct


def rows_correct(genome: list[int]) -> int:
    return sum(int(eval_grid(genome, idx) == [golden_output_bit(bit, idx) for bit in range(4)]) for idx in range(16))


def active_nodes(genome: list[int]) -> int:
    return sum(int(v != 0 and v != 0xFFFF) for v in genome)


def random_genome(rng: XorShift32) -> list[int]:
    return PASS_THROUGH + PASS_THROUGH + [rng.randrange(65536) for _ in range(4)]


def mutate(genome: list[int], rng: XorShift32, rate_ppm: int) -> list[int]:
    out = genome[:]
    changed = False
    for node in range(8, 12):
        value = out[node]
        for bit in range(16):
            if rng.chance_ppm(rate_ppm):
                value ^= 1 << bit
                changed = True
        out[node] = value & 0xFFFF
    if not changed:
        node = 8 + rng.randrange(4)
        out[node] ^= 1 << rng.randrange(16)
    return out


def crossover(a: list[int], b: list[int], rng: XorShift32) -> list[int]:
    out = PASS_THROUGH + PASS_THROUGH
    out += [a[i] if rng.chance_ppm(500_000) else b[i] for i in range(8, 12)]
    return out


def tournament(scored: list[tuple[int, list[int]]], rng: XorShift32, k: int) -> list[int]:
    best = scored[rng.randrange(len(scored))]
    for _ in range(1, k):
        item = scored[rng.randrange(len(scored))]
        if item[0] > best[0]:
            best = item
    return best[1]


def genome_hex(genome: list[int]) -> str:
    return " ".join(f"{v:04x}" for v in genome)


def parse_genome_hex(s: str) -> list[int]:
    vals = [int(x, 16) for x in s.split()]
    if len(vals) != GENOME_LEN:
        raise ValueError(f"expected {GENOME_LEN} words")
    return vals


def run_ga(args: argparse.Namespace) -> tuple[list[int], int, int]:
    rng = XorShift32(args.seed)
    pop = [random_genome(rng) for _ in range(args.population)]
    best_genome = pop[0][:]
    best_fit = evaluate(best_genome)
    best_gen = 0

    csv_file = None
    writer = None
    if args.csv:
        path = Path(args.csv)
        path.parent.mkdir(parents=True, exist_ok=True)
        csv_file = path.open("w", newline="")
        writer = csv.DictWriter(
            csv_file,
            fieldnames=["gen", "best_fitness", "rows_correct", "active_nodes", "genome"],
            lineterminator="\n",
        )
        writer.writeheader()

    try:
        for gen in range(args.generations + 1):
            scored = sorted(((evaluate(g), g) for g in pop), key=lambda item: item[0], reverse=True)
            if scored[0][0] > best_fit:
                best_fit = scored[0][0]
                best_genome = scored[0][1][:]
                best_gen = gen
            if writer:
                writer.writerow({
                    "gen": gen,
                    "best_fitness": best_fit,
                    "rows_correct": rows_correct(best_genome),
                    "active_nodes": active_nodes(best_genome),
                    "genome": genome_hex(best_genome),
                })
            if args.verbose and (gen == 0 or gen == args.generations or gen % args.report_every == 0):
                print(f"gen {gen:4d}: fitness={best_fit:2d}/64 rows={rows_correct(best_genome):2d}/16")
            if best_fit >= FITNESS_MAX:
                break

            next_pop = [g[:] for _, g in scored[: args.elites]]
            while len(next_pop) < args.population:
                p1 = tournament(scored, rng, args.tournament)
                if rng.chance_ppm(args.crossover_ppm):
                    p2 = tournament(scored, rng, args.tournament)
                    child = crossover(p1, p2, rng)
                else:
                    child = p1[:]
                next_pop.append(mutate(child, rng, args.mutation_ppm))
            pop = next_pop
    finally:
        if csv_file:
            csv_file.close()
    return best_genome, best_fit, best_gen


def truth_table_text(genome: list[int]) -> str:
    lines = ["a b | p  gold"]
    for idx in range(16):
        a = (idx & 1) | (((idx >> 1) & 1) << 1)
        b = ((idx >> 2) & 1) | (((idx >> 3) & 1) << 1)
        out_bits = eval_grid(genome, idx)
        out = sum(out_bits[i] << i for i in range(4))
        gold = a * b
        lines.append(f"{a:1d} {b:1d} | {out:02d} {gold:02d}")
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--population", type=int, default=64)
    ap.add_argument("--generations", type=int, default=200)
    ap.add_argument("--elites", type=int, default=2)
    ap.add_argument("--tournament", type=int, default=3)
    ap.add_argument("--crossover-ppm", type=int, default=700_000)
    ap.add_argument("--mutation-ppm", type=int, default=30_000)
    ap.add_argument("--report-every", type=int, default=20)
    ap.add_argument("--csv", default="runs/ehw1_0_cgp.csv")
    ap.add_argument("--quiet", dest="verbose", action="store_false")
    ap.add_argument("--check-golden", action="store_true")
    args = ap.parse_args()

    if args.check_golden:
        fit = evaluate(GOLDEN_GENOME)
        print(f"golden_fitness={fit}/64 rows={rows_correct(GOLDEN_GENOME)}/16 genome={genome_hex(GOLDEN_GENOME)}")
        return 0 if fit == FITNESS_MAX else 1

    best, fit, gen = run_ga(args)
    print("\n== EHW-1.0 CGP best ==")
    print(f"fitness={fit}/64 rows={rows_correct(best)}/16 generation={gen} active_nodes={active_nodes(best)}")
    print(f"genome={genome_hex(best)}")
    print(truth_table_text(best))
    if args.csv:
        print(f"csv={args.csv}")
    return 0 if fit == FITNESS_MAX else 1


if __name__ == "__main__":
    raise SystemExit(main())
