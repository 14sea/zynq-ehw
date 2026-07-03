#!/usr/bin/env python3
"""EHW-4.0 host oracle: GA x fixed-point SGD memetic evolution.

This is a host-only design rung. It keeps the EHW-0 24-byte INT8 weight genome
and adds a deterministic QAT-style adaptation inner loop:

  genome -> Q8.8 master weights -> K fixed-point SGD epochs -> INT8 genome

Biases stay fixed to the EHW-0/M7.5.3-lite constants in this first rung, so the
Lamarckian write-back remains the same 24-byte genome contract used by EHW-0.
The same 40 samples are used for adaptation and scoring; this is a deployment-set
mechanism check, not a holdout/generalization claim.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import oracle_evolve as evo


FRAC = 8
ONE = 1 << FRAC
WSHIFT = 2
XSHIFT = 2
DSHIFT = 2
LR_SHIFT = 7
ERR_CLAMP = 1 << 20
DELTA_CLAMP = 1 << 14
QMIN = -(1 << 15)
QMAX = (1 << 15) - 1

DEFAULT_CURVE_CSV = Path("runs/ehw4_0_memetic_curves.csv")
DEFAULT_SUMMARY_CSV = Path("runs/ehw4_0_memetic_summary.csv")
DEFAULT_DOC = Path("docs/ehw4_0_results.md")


@dataclass(frozen=True)
class EvalResult:
    select_score: evo.Score
    pre_score: evo.Score
    post_score: evo.Score
    genome_for_next: list[int]
    pre_genome: list[int]
    post_genome: list[int]


@dataclass
class ModeResult:
    mode: str
    best_genome: list[int]
    best_pre_genome: list[int]
    best_post_genome: list[int]
    best_pre_score: evo.Score
    best_post_score: evo.Score
    first_40: int | None


def clamp(v: int, lo: int, hi: int) -> int:
    return lo if v < lo else hi if v > hi else v


def sat16(v: int) -> int:
    return clamp(v, QMIN, QMAX)


def qmul(a: int, b: int) -> int:
    return (a * b + (1 << (FRAC - 1))) >> FRAC


def leaky_d(z: int) -> int:
    return ONE if z >= 0 else ONE >> evo.K


def master_from_genome(genome: list[int]) -> tuple[list[list[int]], list[list[int]]]:
    w1_i8, w2_i8 = evo.unpack(genome)
    w1 = [[int(v) << WSHIFT for v in row] for row in w1_i8]
    w2 = [[int(v) << WSHIFT for v in row] for row in w2_i8]
    return w1, w2


def genome_from_master(w1: list[list[int]], w2: list[list[int]]) -> list[int]:
    genome: list[int] = []
    for row in w1:
        genome.extend(evo.q8(v, WSHIFT) for v in row)
    for row in w2:
        genome.extend(evo.q8(v, WSHIFT) for v in row)
    return genome


def dot4(row: list[int], x: list[int]) -> int:
    return sum(int(a) * int(b) for a, b in zip(row, x))


def forward_master(w1: list[list[int]], w2: list[list[int]], x_i8: list[int]):
    w1_i8 = [[evo.q8(v, WSHIFT) for v in row] for row in w1]
    w2_i8 = [[evo.q8(v, WSHIFT) for v in row] for row in w2]

    z1: list[int] = []
    h: list[int] = []
    h_i8: list[int] = []
    for r in range(evo.NH):
        acc = dot4(w1_i8[r], x_i8)
        z = sat16(((acc + (1 << 3)) >> 4) + evo.M753_B1[r])
        hv = evo.leaky(z)
        z1.append(z)
        h.append(hv)
        h_i8.append(evo.q8(hv, evo.XSHIFT_H))

    z2: list[int] = []
    y: list[int] = []
    for r in range(evo.NOUT):
        acc = dot4(w2_i8[r], h_i8)
        z = sat16(((acc + (1 << 2)) >> 3) + evo.M753_B2[r])
        z2.append(z)
        y.append(evo.leaky(z))

    pred = 1 if y[1] > y[0] else 0
    return pred, z1, h, z2, y


def sgd_epoch(w1: list[list[int]], w2: list[list[int]], order: list[int], lr_shift: int) -> int:
    sse = 0
    for idx in order:
        x = evo.M753_TEST_X[idx]
        label = evo.M753_TEST_Y[idx]
        _, z1, h, z2, y = forward_master(w1, w2, x)

        err = [clamp(y[k] - (ONE if k == label else 0), -ERR_CLAMP, ERR_CLAMP - 1)
               for k in range(evo.NOUT)]
        for e in err:
            sse += qmul(e, e)

        d2 = [clamp(qmul(err[k], leaky_d(z2[k])), -DELTA_CLAMP, DELTA_CLAMP - 1)
              for k in range(evo.NOUT)]

        dw2 = [[qmul(d2[i], h[j]) for j in range(evo.NH)] for i in range(evo.NOUT)]

        d2_i8 = [evo.q8(v, DSHIFT) for v in d2]
        w2_i8 = [[evo.q8(v, WSHIFT) for v in row] for row in w2]
        w2td2 = []
        for j in range(evo.NH):
            acc = sum(w2_i8[i][j] * d2_i8[i] for i in range(evo.NOUT))
            w2td2.append(sat16((acc + (1 << 3)) >> 4))

        d1 = [clamp(qmul(w2td2[i], leaky_d(z1[i])), -DELTA_CLAMP, DELTA_CLAMP - 1)
              for i in range(evo.NH)]
        x_q88 = [int(v) << XSHIFT for v in x]
        dw1 = [[qmul(d1[i], x_q88[j]) for j in range(evo.NIN)] for i in range(evo.NH)]

        for i in range(evo.NOUT):
            for j in range(evo.NH):
                w2[i][j] = sat16(w2[i][j] - (dw2[i][j] >> lr_shift))
        for i in range(evo.NH):
            for j in range(evo.NIN):
                w1[i][j] = sat16(w1[i][j] - (dw1[i][j] >> lr_shift))
    return sse


def adapt(genome: list[int], epochs: int, lr_shift: int, seed: int) -> tuple[list[int], int]:
    w1, w2 = master_from_genome(genome)
    order_rng = evo.XorShift32(seed)
    last_sse = 0
    order = list(range(evo.NTEST))
    for _ in range(epochs):
        # Deterministic Fisher-Yates shuffle using the same C-friendly RNG family.
        for i in range(len(order) - 1, 0, -1):
            j = order_rng.randrange(i + 1)
            order[i], order[j] = order[j], order[i]
        last_sse = sgd_epoch(w1, w2, order, lr_shift)
    return genome_from_master(w1, w2), last_sse


def eval_candidate(mode: str, genome: list[int], epochs: int, lr_shift: int, seed: int) -> EvalResult:
    pre = evo.evaluate(genome)
    if mode == "pure_ga":
        return EvalResult(pre, pre, pre, genome[:], genome[:], genome[:])

    post_genome, _ = adapt(genome, epochs, lr_shift, seed)
    post = evo.evaluate(post_genome)
    if mode == "baldwinian":
        return EvalResult(post, pre, post, genome[:], genome[:], post_genome)
    if mode == "lamarckian":
        return EvalResult(post, pre, post, post_genome[:], genome[:], post_genome)
    raise ValueError(f"bad memetic mode: {mode}")


def ranked_better(a: tuple[evo.Score, int], b: tuple[evo.Score, int]) -> bool:
    if a[0].fitness != b[0].fitness:
        return a[0].fitness > b[0].fitness
    return a[1] < b[1]


def run_evolution_mode(mode: str, args: argparse.Namespace, curve_rows: list[dict[str, str]]) -> ModeResult:
    rng = evo.XorShift32(args.seed + {"pure_ga": 0, "baldwinian": 101, "lamarckian": 202}[mode])
    mutation_rate_ppm = evo.rate_to_ppm(args.mutation_rate)
    crossover_rate_ppm = evo.rate_to_ppm(args.crossover_rate)
    pop = evo.seed_population(rng, args.population, args.init_span, mutation_rate_ppm)

    best_eval = eval_candidate(mode, pop[0], args.adapt_epochs, args.lr_shift, args.seed)
    first_40 = None

    for gen in range(args.generations + 1):
        evaluated = [
            eval_candidate(mode, g, args.adapt_epochs, args.lr_shift, args.seed + gen * 1009 + i)
            for i, g in enumerate(pop)
        ]
        ranked = sorted(enumerate(evaluated), key=lambda item: (-item[1].select_score.fitness, item[0]))
        _, top_eval = ranked[0]
        if top_eval.select_score.fitness > best_eval.select_score.fitness:
            best_eval = top_eval
        if first_40 is None and best_eval.post_score.correct >= evo.NTEST:
            first_40 = gen

        curve_rows.append({
            "mode": mode,
            "gen": str(gen),
            "best_correct": str(best_eval.post_score.correct),
            "best_acc": f"{best_eval.post_score.accuracy:.4f}",
            "best_sse": str(best_eval.post_score.sse),
            "best_fitness": str(best_eval.post_score.fitness),
            "pre_correct": str(best_eval.pre_score.correct),
            "post_correct": str(best_eval.post_score.correct),
            "genome": " ".join(str(v) for v in best_eval.genome_for_next),
            "pre_genome": " ".join(str(v) for v in best_eval.pre_genome),
            "post_genome": " ".join(str(v) for v in best_eval.post_genome),
        })

        if gen == args.generations:
            break

        next_pop = [evaluated[i].genome_for_next[:] for i, _ in ranked[: args.elites]]
        scored_for_pick = [(evaluated[i].select_score, i) for i, _ in ranked]
        while len(next_pop) < args.population:
            p1_idx = tournament_pick(scored_for_pick, rng, args.tournament)
            p1 = evaluated[p1_idx].genome_for_next
            if rng.chance_ppm(crossover_rate_ppm):
                p2_idx = tournament_pick(scored_for_pick, rng, args.tournament)
                child = evo.crossover(p1, evaluated[p2_idx].genome_for_next, rng)
            else:
                child = p1[:]
            next_pop.append(evo.mutate(child, rng, mutation_rate_ppm, args.mutation_step))
        pop = next_pop

    return ModeResult(mode, best_eval.genome_for_next, best_eval.pre_genome,
                      best_eval.post_genome,
                      best_eval.pre_score, best_eval.post_score, first_40)


def tournament_pick(scored: list[tuple[evo.Score, int]], rng: evo.XorShift32, k: int) -> int:
    best = scored[rng.randrange(len(scored))]
    for _ in range(1, k):
        cur = scored[rng.randrange(len(scored))]
        if ranked_better(cur, best):
            best = cur
    return best[1]


def run_pure_sgd(args: argparse.Namespace, curve_rows: list[dict[str, str]]) -> ModeResult:
    genome = evo.M753_TRAINED_GENOME[:]
    best_genome = genome[:]
    best_score = evo.evaluate(genome)
    first_40 = 0 if best_score.correct >= evo.NTEST else None
    for gen in range(args.generations + 1):
        score = evo.evaluate(genome)
        if score.fitness > best_score.fitness:
            best_score = score
            best_genome = genome[:]
        if first_40 is None and best_score.correct >= evo.NTEST:
            first_40 = gen
        curve_rows.append({
            "mode": "pure_sgd",
            "gen": str(gen),
            "best_correct": str(best_score.correct),
            "best_acc": f"{best_score.accuracy:.4f}",
            "best_sse": str(best_score.sse),
            "best_fitness": str(best_score.fitness),
            "pre_correct": str(score.correct),
            "post_correct": str(score.correct),
            "genome": " ".join(str(v) for v in best_genome),
            "pre_genome": " ".join(str(v) for v in genome),
            "post_genome": " ".join(str(v) for v in genome),
        })
        if gen < args.generations:
            genome, _ = adapt(genome, args.population * args.adapt_epochs, args.lr_shift,
                              args.seed + 50000 + gen)
    return ModeResult("pure_sgd", best_genome, best_genome, best_genome,
                      best_score, best_score, first_40)


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def genome_str(genome: list[int]) -> str:
    return " ".join(str(v) for v in genome)


def write_doc(path: Path, results: list[ModeResult], args: argparse.Namespace) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    for r in results:
        first = f"generation {r.first_40}" if r.first_40 is not None else f">{args.generations}"
        rows.append(
            f"| {r.mode} | {r.best_post_score.correct}/40 | {r.best_post_score.sse} | "
            f"{r.best_post_score.fitness} | {first} |"
        )
    lamarck = next(r for r in results if r.mode == "lamarckian")
    doc = f"""# EHW-4.0 Results — Host-Only Memetic Oracle

Generated by:

```bash
python3 sim/oracle_memetic.py --seed {args.seed} --population {args.population} --generations {args.generations} --adapt-epochs {args.adapt_epochs}
```

Status: **HOST-ONLY.** No board claim is made. This rung establishes the deterministic
oracle for EHW-4 GA × HW-SGD memetic evolution before any C twin, RTL, firmware, or
board work.

The first substrate keeps the EHW-0 24-byte INT8 genome (`W1[4][4] + W2[2][4]`) and
adds a fixed-point QAT-style adaptation inner loop. Adaptation uses Q8.8 master
weights, INT8 forward views, saturating arithmetic, and fixed EHW-0/M7.5.3-lite
biases. The Lamarckian genome write-back quantizes the adapted master weights back
to the same 24-byte INT8 contract.

Both adaptation and scoring use the same 40-sample 2x2 MNIST 0/1 deployment set.
This is a same-set mechanism comparison, not a holdout/generalization result.

| Mode | Best Label Acc | SSE | Fitness | First 40/40 |
|---|---:|---:|---:|---:|
{chr(10).join(rows)}

## Interpretation

- `pure_ga` is the EHW-0-style baseline: selection sees direct deployment fitness.
- `pure_sgd` starts from the M7.5.3 trained tile and spends the same rough adaptation
  budget without population search.
- `baldwinian` selects on post-adaptation fitness but reproduces the original genome.
- `lamarckian` selects on post-adaptation fitness and writes adapted weights back into
  the genome.
- These results only prove that the host oracle is deterministic and that the four
  modes are comparable under one fixed-point contract. Scientific claims need a later
  train/evolution/holdout split.

## Best Lamarckian Genome

Before adaptation/write-back:

```text
{genome_str(lamarck.best_pre_genome)}
```

After adaptation/write-back:

```text
{genome_str(lamarck.best_post_genome)}
```

## Reproducibility

- Curve CSV: `{args.curve_csv}`
- Summary CSV: `{args.summary_csv}`
- RNG: XorShift32, seed `{args.seed}`
- Population: `{args.population}`
- Generations: `{args.generations}`
- Adaptation epochs per candidate: `{args.adapt_epochs}`
- Learning-rate shift: `{args.lr_shift}`
- Fitness: `1_000_000 * label_correct - SSE`
"""
    path.write_text(doc)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--population", type=int, default=16)
    ap.add_argument("--generations", type=int, default=32)
    ap.add_argument("--adapt-epochs", type=int, default=2)
    ap.add_argument("--lr-shift", type=int, default=LR_SHIFT)
    ap.add_argument("--init-span", type=int, default=32)
    ap.add_argument("--elites", type=int, default=2)
    ap.add_argument("--tournament", type=int, default=3)
    ap.add_argument("--crossover-rate", type=float, default=0.70)
    ap.add_argument("--mutation-rate", type=float, default=0.03)
    ap.add_argument("--mutation-step", type=int, default=8)
    ap.add_argument("--curve-csv", type=Path, default=DEFAULT_CURVE_CSV)
    ap.add_argument("--summary-csv", type=Path, default=DEFAULT_SUMMARY_CSV)
    ap.add_argument("--doc", type=Path, default=DEFAULT_DOC)
    args = ap.parse_args()

    curve_rows: list[dict[str, str]] = []
    results = [
        run_evolution_mode("pure_ga", args, curve_rows),
        run_pure_sgd(args, curve_rows),
        run_evolution_mode("baldwinian", args, curve_rows),
        run_evolution_mode("lamarckian", args, curve_rows),
    ]

    summary_rows = []
    for r in results:
        summary_rows.append({
            "mode": r.mode,
            "best_correct": str(r.best_post_score.correct),
            "best_acc": f"{r.best_post_score.accuracy:.4f}",
            "best_sse": str(r.best_post_score.sse),
            "best_fitness": str(r.best_post_score.fitness),
            "first_40": "" if r.first_40 is None else str(r.first_40),
            "genome": genome_str(r.best_genome),
            "pre_genome": genome_str(r.best_pre_genome),
            "post_genome": genome_str(r.best_post_genome),
        })

    write_csv(args.curve_csv, curve_rows)
    write_csv(args.summary_csv, summary_rows)
    write_doc(args.doc, results, args)

    for row in summary_rows:
        first = row["first_40"] or "none"
        print(f"{row['mode']:11s} best={row['best_correct']}/40 sse={row['best_sse']} first_40={first}")
    print(f"wrote {args.curve_csv}")
    print(f"wrote {args.summary_csv}")
    print(f"wrote {args.doc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
