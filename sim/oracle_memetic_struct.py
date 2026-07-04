#!/usr/bin/env python3
"""EHW-5.0 host oracle: safe structure + weights + fixed-point SGD.

This rung combines two already proven contracts:

  - EHW-3 spare-route structure genome: 16 bytes, safe LUT INIT/select fields.
  - EHW-4 memetic weight genome: 24 signed INT8 seed weights.

The first hybrid substrate computes one Boolean feature in the spare-route island
from thresholded inputs x0/x1/x2, then couples that feature back into the existing
4->4->2 EHW-4 network.  Candidate evaluation uses the EHW-4 fixed-point SGD inner
loop on the transformed dataset.  This is HOST-ONLY; no board claim is made here.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

import oracle_evolve as evo
import oracle_memetic as mem
import oracle_spare_routing as sr


HYBRID_GENOME_LEN = sr.GENOME_LEN + evo.GENOME_LEN
DEFAULT_CURVE_CSV = Path("runs/ehw5_0_struct_curves.csv")
DEFAULT_SUMMARY_CSV = Path("runs/ehw5_0_struct_summary.csv")
DEFAULT_DOC = Path("docs/ehw5_0_results.md")
COUPLINGS = ("replace_x3", "gate_x3", "bias_x3")

SR_MAJORITY = [0xA, 0xA, 0xA, 0x0, 0xE8, 0, 0, 1, 1, 2, 2, 3, 3, 0, 1, 2]
SR_REPAIR = [0xA, 0x0, 0xA, 0xA, 0xE8, 0, 0, 3, 3, 2, 2, 1, 1, 0, 3, 2]


@dataclass(frozen=True)
class StructEval:
    select_score: evo.Score
    pre_score: evo.Score
    post_score: evo.Score
    sr_genome: list[int]
    weight_for_next: list[int]
    pre_weight: list[int]
    post_weight: list[int]
    feature_mask: int
    feature_ones: int
    feature_penalty: int


@dataclass
class StructResult:
    mode: str
    coupling: str
    best_score: evo.Score
    first_40: int | None
    sr_genome: list[int]
    pre_weight: list[int]
    post_weight: list[int]
    feature_mask: int
    feature_ones: int
    feature_penalty: int
    degraded_correct: int
    repaired_correct: int


def clamp_i8(v: int) -> int:
    return -128 if v < -128 else 127 if v > 127 else v


def sat_count(genome: list[int]) -> int:
    return sum(1 for v in genome if v in (-128, 127))


def phi_for_x(sr_genome: list[int], x: list[int], fault: sr.Fault = sr.FAULT_NONE) -> int:
    row = int(x[0] >= 8) | (int(x[1] >= 8) << 1) | (int(x[2] >= 8) << 2)
    return sr.eval_row(sr_genome, row, fault)


def transformed_x(sr_genome: list[int], x: list[int], coupling: str,
                  fault: sr.Fault = sr.FAULT_NONE) -> list[int]:
    phi = phi_for_x(sr_genome, x, fault)
    if coupling == "replace_x3":
        x3 = 16 if phi else 0
    elif coupling == "gate_x3":
        x3 = x[3] if phi else 0
    elif coupling == "bias_x3":
        x3 = clamp_i8(x[3] + (8 if phi else -8))
    else:
        raise ValueError(f"bad coupling: {coupling}")
    return [x[0], x[1], x[2], x3]


def transformed_dataset(sr_genome: list[int], coupling: str,
                        fault: sr.Fault = sr.FAULT_NONE) -> list[list[int]]:
    return [transformed_x(sr_genome, x, coupling, fault) for x in evo.M753_TEST_X]


def feature_mask_for_dataset(sr_genome: list[int], fault: sr.Fault = sr.FAULT_NONE) -> int:
    mask = 0
    for idx, x in enumerate(evo.M753_TEST_X):
        if phi_for_x(sr_genome, x, fault):
            mask |= 1 << idx
    return mask


def feature_ones(mask: int) -> int:
    return mask.bit_count()


def structural_penalty(mask: int, args: argparse.Namespace) -> int:
    # Pressure against constant or near-constant feature channels. Correct labels
    # still dominate because the penalty is far below one label's 1_000_000 fitness
    # step, but among 40/40 candidates it strongly prefers non-trivial features.
    balance = min(feature_ones(mask), evo.NTEST - feature_ones(mask))
    return max(0, args.feature_min_balance - balance) * args.feature_penalty


def forward_master(w1: list[list[int]], w2: list[list[int]], x_i8: list[int]):
    w1_i8 = [[evo.q8(v, mem.WSHIFT) for v in row] for row in w1]
    w2_i8 = [[evo.q8(v, mem.WSHIFT) for v in row] for row in w2]

    z1: list[int] = []
    h: list[int] = []
    h_i8: list[int] = []
    for r in range(evo.NH):
        acc = sum(int(a) * int(b) for a, b in zip(w1_i8[r], x_i8))
        z = mem.sat16(((acc + (1 << 3)) >> 4) + evo.M753_B1[r])
        hv = evo.leaky(z)
        z1.append(z)
        h.append(hv)
        h_i8.append(evo.q8(hv, evo.XSHIFT_H))

    z2: list[int] = []
    y: list[int] = []
    for r in range(evo.NOUT):
        acc = sum(int(a) * int(b) for a, b in zip(w2_i8[r], h_i8))
        z = mem.sat16(((acc + (1 << 2)) >> 3) + evo.M753_B2[r])
        z2.append(z)
        y.append(evo.leaky(z))
    return (1 if y[1] > y[0] else 0), z1, h, z2, y


def evaluate_dataset(weight_genome: list[int], xs: list[list[int]]) -> evo.Score:
    w1, w2 = mem.master_from_genome(weight_genome)
    correct = 0
    sse = 0
    for x, label in zip(xs, evo.M753_TEST_Y):
        pred, _, _, _, y = forward_master(w1, w2, x)
        correct += int(pred == label)
        for k in range(evo.NOUT):
            target = mem.ONE if k == label else 0
            err = y[k] - target
            sse += mem.qmul(err, err)
    return evo.Score(correct * 1_000_000 - sse, correct, sse)


def sgd_epoch_dataset(w1: list[list[int]], w2: list[list[int]],
                      xs: list[list[int]], order: list[int], lr_shift: int) -> int:
    sse = 0
    for idx in order:
        x = xs[idx]
        label = evo.M753_TEST_Y[idx]
        _, z1, h, z2, y = forward_master(w1, w2, x)

        err = [mem.clamp(y[k] - (mem.ONE if k == label else 0),
                         -mem.ERR_CLAMP, mem.ERR_CLAMP - 1)
               for k in range(evo.NOUT)]
        for e in err:
            sse += mem.qmul(e, e)

        d2 = [mem.clamp(mem.qmul(err[k], mem.leaky_d(z2[k])),
                        -mem.DELTA_CLAMP, mem.DELTA_CLAMP - 1)
              for k in range(evo.NOUT)]
        dw2 = [[mem.qmul(d2[i], h[j]) for j in range(evo.NH)] for i in range(evo.NOUT)]

        d2_i8 = [evo.q8(v, mem.DSHIFT) for v in d2]
        w2_i8 = [[evo.q8(v, mem.WSHIFT) for v in row] for row in w2]
        w2td2 = []
        for j in range(evo.NH):
            acc = sum(w2_i8[i][j] * d2_i8[i] for i in range(evo.NOUT))
            w2td2.append(mem.sat16((acc + (1 << 3)) >> 4))

        d1 = [mem.clamp(mem.qmul(w2td2[i], mem.leaky_d(z1[i])),
                        -mem.DELTA_CLAMP, mem.DELTA_CLAMP - 1)
              for i in range(evo.NH)]
        x_q88 = [int(v) << mem.XSHIFT for v in x]
        dw1 = [[mem.qmul(d1[i], x_q88[j]) for j in range(evo.NIN)] for i in range(evo.NH)]

        for i in range(evo.NOUT):
            for j in range(evo.NH):
                w2[i][j] = mem.sat16(w2[i][j] - (dw2[i][j] >> lr_shift))
        for i in range(evo.NH):
            for j in range(evo.NIN):
                w1[i][j] = mem.sat16(w1[i][j] - (dw1[i][j] >> lr_shift))
    return sse


def adapt_dataset(weight_genome: list[int], xs: list[list[int]],
                  epochs: int, lr_shift: int, seed: int) -> tuple[list[int], int]:
    w1, w2 = mem.master_from_genome(weight_genome)
    rng = evo.XorShift32(seed)
    order = list(range(evo.NTEST))
    last_sse = 0
    for _ in range(epochs):
        for i in range(len(order) - 1, 0, -1):
            j = rng.randrange(i + 1)
            order[i], order[j] = order[j], order[i]
        last_sse = sgd_epoch_dataset(w1, w2, xs, order, lr_shift)
    return mem.genome_from_master(w1, w2), last_sse


def eval_struct_candidate(mode: str, coupling: str, sr_genome: list[int],
                          weight_genome: list[int], args: argparse.Namespace,
                          seed: int) -> StructEval:
    xs = transformed_dataset(sr_genome, coupling)
    pre = evaluate_dataset(weight_genome, xs)
    if mode == "hybrid_no_adapt":
        post_genome = weight_genome[:]
        post = pre
    else:
        post_genome, _ = adapt_dataset(weight_genome, xs, args.adapt_epochs, args.lr_shift, seed)
        post = evaluate_dataset(post_genome, xs)
    feature_mask = feature_mask_for_dataset(sr_genome)
    penalty = structural_penalty(feature_mask, args) if mode.endswith("_pressure") else 0
    select_score = evo.Score(post.fitness - penalty, post.correct, post.sse)
    return StructEval(
        select_score=select_score,
        pre_score=pre,
        post_score=post,
        sr_genome=sr_genome[:],
        weight_for_next=post_genome[:] if mode.startswith("hybrid_lamarckian") else weight_genome[:],
        pre_weight=weight_genome[:],
        post_weight=post_genome[:],
        feature_mask=feature_mask,
        feature_ones=feature_ones(feature_mask),
        feature_penalty=penalty,
    )


def mutate_sr(genome: list[int], rng: evo.XorShift32, init_ppm: int, sel_ppm: int) -> list[int]:
    return sr.mutate(genome, rng, init_ppm, sel_ppm)


def mutate_hybrid(candidate: tuple[list[int], list[int]], rng: evo.XorShift32,
                  args: argparse.Namespace) -> tuple[list[int], list[int]]:
    sr_genome, weight = candidate
    if rng.chance_ppm(args.struct_mutation_ppm):
        sr_genome = mutate_sr(sr_genome, rng, args.struct_init_mutation_ppm, args.struct_sel_mutation_ppm)
    weight = evo.mutate(weight, rng, evo.rate_to_ppm(args.mutation_rate), args.mutation_step)
    return sr_genome, weight


def crossover_hybrid(a: tuple[list[int], list[int]], b: tuple[list[int], list[int]],
                     rng: evo.XorShift32) -> tuple[list[int], list[int]]:
    sr_child = sr.crossover(a[0], b[0], rng)
    wt_child = evo.crossover(a[1], b[1], rng)
    return sr_child, wt_child


def seed_hybrid_population(rng: evo.XorShift32, args: argparse.Namespace) -> list[tuple[list[int], list[int]]]:
    pop: list[tuple[list[int], list[int]]] = []
    weight_seeds = evo.seed_population(rng, min(args.population, 8), args.init_span, evo.rate_to_ppm(args.mutation_rate))
    sr_seeds = [
        SR_MAJORITY[:],
        SR_REPAIR[:],
        sr.scaffold_genome(rng, use_spare=False),
        sr.scaffold_genome(rng, use_spare=True),
    ]
    for srg in sr_seeds:
        for wg in weight_seeds[:2]:
            pop.append((srg[:], wg[:]))
    while len(pop) < args.population:
        base_sr = sr_seeds[rng.randrange(len(sr_seeds))]
        srg = mutate_sr(base_sr, rng, args.struct_init_mutation_ppm, args.struct_sel_mutation_ppm)
        wg = weight_seeds[rng.randrange(len(weight_seeds))]
        wg = evo.mutate(wg, rng, evo.rate_to_ppm(args.mutation_rate), args.mutation_step)
        pop.append((srg, wg))
    return pop[:args.population]


def better(a: tuple[evo.Score, int], b: tuple[evo.Score, int]) -> bool:
    if a[0].fitness != b[0].fitness:
        return a[0].fitness > b[0].fitness
    return a[1] < b[1]


def tournament_pick(scored: list[tuple[evo.Score, int]], rng: evo.XorShift32, k: int) -> int:
    best = scored[rng.randrange(len(scored))]
    for _ in range(1, k):
        cur = scored[rng.randrange(len(scored))]
        if better(cur, best):
            best = cur
    return best[1]


def genome_i8_text(genome: list[int]) -> str:
    return " ".join(str(v) for v in genome)


def sr_hex_text(genome: list[int]) -> str:
    return " ".join(f"{v & 0xFF:02x}" for v in genome)


def run_hybrid_mode(mode: str, coupling: str, args: argparse.Namespace,
                    curve_rows: list[dict[str, str]]) -> StructResult:
    rng = evo.XorShift32(args.seed + {
        "hybrid_lamarckian": 303,
        "hybrid_no_adapt": 404,
        "hybrid_lamarckian_pressure": 505,
    }[mode])
    pop = seed_hybrid_population(rng, args)
    best_eval = eval_struct_candidate(mode, coupling, pop[0][0], pop[0][1], args, args.seed)
    first_40 = None

    for gen in range(args.generations + 1):
        evaluated = [
            eval_struct_candidate(mode, coupling, srg, wg, args, args.seed + gen * 1009 + i)
            for i, (srg, wg) in enumerate(pop)
        ]
        ranked = sorted(enumerate(evaluated), key=lambda item: (-item[1].select_score.fitness, item[0]))
        top_i, top_eval = ranked[0]
        if top_eval.select_score.fitness > best_eval.select_score.fitness:
            best_eval = top_eval
        if first_40 is None and best_eval.post_score.correct >= evo.NTEST:
            first_40 = gen

        curve_rows.append({
            "mode": mode,
            "coupling": coupling,
            "gen": str(gen),
            "best_correct": str(best_eval.post_score.correct),
            "best_sse": str(best_eval.post_score.sse),
            "best_fitness": str(best_eval.post_score.fitness),
            "select_fitness": str(best_eval.select_score.fitness),
            "feature_mask": f"{best_eval.feature_mask:010x}",
            "feature_ones": str(best_eval.feature_ones),
            "feature_penalty": str(best_eval.feature_penalty),
            "top_index": str(top_i),
            "sr_genome": sr_hex_text(best_eval.sr_genome),
            "pre_weight": genome_i8_text(best_eval.pre_weight),
            "post_weight": genome_i8_text(best_eval.post_weight),
        })
        if gen == args.generations:
            break

        scored = [(evaluated[i].select_score, i) for i, _ in ranked]
        next_pop: list[tuple[list[int], list[int]]] = [
            (evaluated[i].sr_genome[:], evaluated[i].weight_for_next[:])
            for i, _ in ranked[: args.elites]
        ]
        while len(next_pop) < args.population:
            p1 = tournament_pick(scored, rng, args.tournament)
            parent = (evaluated[p1].sr_genome, evaluated[p1].weight_for_next)
            if rng.chance_ppm(evo.rate_to_ppm(args.crossover_rate)):
                p2 = tournament_pick(scored, rng, args.tournament)
                mate = (evaluated[p2].sr_genome, evaluated[p2].weight_for_next)
                child = crossover_hybrid(parent, mate, rng)
            else:
                child = (parent[0][:], parent[1][:])
            next_pop.append(mutate_hybrid(child, rng, args))
        pop = next_pop

    degraded_xs = transformed_dataset(best_eval.sr_genome, coupling, sr.FAULT_DISABLE_A1)
    degraded = evaluate_dataset(best_eval.post_weight, degraded_xs)
    repaired_xs = transformed_dataset(SR_REPAIR, coupling, sr.FAULT_DISABLE_A1)
    repaired = evaluate_dataset(best_eval.post_weight, repaired_xs)
    return StructResult(
        mode=mode,
        coupling=coupling,
        best_score=best_eval.post_score,
        first_40=first_40,
        sr_genome=best_eval.sr_genome,
        pre_weight=best_eval.pre_weight,
        post_weight=best_eval.post_weight,
        feature_mask=best_eval.feature_mask,
        feature_ones=best_eval.feature_ones,
        feature_penalty=best_eval.feature_penalty,
        degraded_correct=degraded.correct,
        repaired_correct=repaired.correct,
    )


def append_weight_curve_rows(src_rows: list[dict[str, str]],
                             dst_rows: list[dict[str, str]]) -> None:
    for row in src_rows:
        dst_rows.append({
            "mode": "weight_only_lamarckian",
            "coupling": "none",
            "gen": row["gen"],
            "best_correct": row["best_correct"],
            "best_sse": row["best_sse"],
            "best_fitness": row["best_fitness"],
            "select_fitness": row["best_fitness"],
            "feature_mask": "",
            "feature_ones": "",
            "feature_penalty": "",
            "top_index": "",
            "sr_genome": "",
            "pre_weight": row["pre_genome"],
            "post_weight": row["post_genome"],
        })


def run_weight_baseline(args: argparse.Namespace,
                        curve_rows: list[dict[str, str]]) -> mem.ModeResult:
    baseline_args = argparse.Namespace(
        seed=args.seed,
        population=args.population,
        generations=args.generations,
        adapt_epochs=args.adapt_epochs,
        lr_shift=args.lr_shift,
        init_span=args.init_span,
        elites=args.elites,
        tournament=args.tournament,
        crossover_rate=args.crossover_rate,
        mutation_rate=args.mutation_rate,
        mutation_step=args.mutation_step,
    )
    rows: list[dict[str, str]] = []
    result = mem.run_evolution_mode("lamarckian", baseline_args, rows)
    append_weight_curve_rows(rows, curve_rows)
    return result


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()), lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_summary(path: Path, baseline: mem.ModeResult,
                  results: list[StructResult], args: argparse.Namespace) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "mode",
        "coupling",
        "best_correct",
        "best_sse",
        "best_fitness",
        "first_40",
        "sat_count",
        "feature_mask",
        "feature_ones",
        "feature_penalty",
        "degraded_correct",
        "repaired_correct",
        "sr_genome",
        "post_weight",
    ]
    rows = [{
        "mode": "weight_only_lamarckian",
        "coupling": "none",
        "best_correct": str(baseline.best_post_score.correct),
        "best_sse": str(baseline.best_post_score.sse),
        "best_fitness": str(baseline.best_post_score.fitness),
        "first_40": "" if baseline.first_40 is None else str(baseline.first_40),
        "sat_count": str(sat_count(baseline.best_post_genome)),
        "feature_mask": "",
        "feature_ones": "",
        "feature_penalty": "",
        "degraded_correct": "",
        "repaired_correct": "",
        "sr_genome": "",
        "post_weight": genome_i8_text(baseline.best_post_genome),
    }]
    for r in results:
        rows.append({
            "mode": r.mode,
            "coupling": r.coupling,
            "best_correct": str(r.best_score.correct),
            "best_sse": str(r.best_score.sse),
            "best_fitness": str(r.best_score.fitness),
            "first_40": "" if r.first_40 is None else str(r.first_40),
            "sat_count": str(sat_count(r.post_weight)),
            "feature_mask": f"{r.feature_mask:010x}",
            "feature_ones": str(r.feature_ones),
            "feature_penalty": str(r.feature_penalty),
            "degraded_correct": str(r.degraded_correct),
            "repaired_correct": str(r.repaired_correct),
            "sr_genome": sr_hex_text(r.sr_genome),
            "post_weight": genome_i8_text(r.post_weight),
        })
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def write_doc(path: Path, baseline: mem.ModeResult,
              results: list[StructResult], args: argparse.Namespace) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "| Mode | Coupling | Correct | SSE | First 40/40 | Feature ones | Penalty | Sat | Fault A1 |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|",
        f"| weight_only_lamarckian | none | {baseline.best_post_score.correct}/40 | "
        f"{baseline.best_post_score.sse} | {baseline.first_40 if baseline.first_40 is not None else 'none'} | "
        f"n/a | n/a | {sat_count(baseline.best_post_genome)} | n/a |",
    ]
    for r in results:
        lines.append(
            f"| {r.mode} | {r.coupling} | {r.best_score.correct}/40 | {r.best_score.sse} | "
            f"{r.first_40 if r.first_40 is not None else 'none'} | {r.feature_ones} | "
            f"{r.feature_penalty} | {sat_count(r.post_weight)} | {r.degraded_correct}/40 |"
        )
    best_hybrid = max(results, key=lambda r: (r.best_score.correct, -r.best_score.sse))
    doc = f"""# EHW-5.0 Results — Host Hybrid Structure + Weights Oracle

Generated by:

```bash
python3 sim/oracle_memetic_struct.py --seed {args.seed} --population {args.population} --generations {args.generations} --adapt-epochs {args.adapt_epochs}
```

Status: **HOST-ONLY.** No board claim is made. This rung tests whether the EHW-5
hybrid substrate is worth porting to a C twin and then to a combined
spare-route-VRC + train-unit RM.

The hybrid genome is 40 bytes:

```text
bytes  0..15  EHW-3 spare-route feature genome
bytes 16..39  EHW-4 INT8 seed-weight genome
```

For every sample, the spare-route island computes one Boolean feature from
thresholded `x0/x1/x2`. The feature is coupled back into the fourth input under
the selected coupling, then the normal EHW-4 fixed-point SGD adaptation inner loop
runs on the transformed 40-sample set.

This is still a same-set deployment/adaptation metric, not a holdout
generalization claim.

## Results

{chr(10).join(lines)}

## Interpretation

- The weight-only row reproduces the EHW-4 Lamarckian baseline under the same
  `POP/GENS/adapt_epochs` budget.
- `hybrid_lamarckian` evolves both the 16-byte structure and 24-byte seed weights;
  adapted weights are written back, structure changes only by GA.
- `hybrid_lamarckian_pressure` uses the same Lamarckian semantics, but selection
  subtracts a feature-balance penalty so constant and near-constant features are
  disfavored after label correctness is preserved.
- `hybrid_no_adapt` keeps the structural search but removes the HW-SGD inner loop.
- `Fault A1` evaluates the best evolved structure with `FAULT_DISABLE_NODE(A1)`.
  It is a quick robustness probe for the evolved feature structure, not a full
  EHW-3-style repair proof.

Important caveat: the unpressured substrate can exploit degenerate features. In
this run, `bias_x3` reaches a useful Lamarckian result with an all-zero feature
mask, which behaves like a constant input bias rather than a non-trivial evolved
feature. The pressure arm is the first EHW-5.0b check: it asks whether useful
results survive when selection demands a non-constant feature channel.

## Best Hybrid

Mode/coupling: `{best_hybrid.mode}` / `{best_hybrid.coupling}`

Feature mask over the 40 deployment samples:

```text
0x{best_hybrid.feature_mask:010x}
```

Spare-route genome:

```text
{sr_hex_text(best_hybrid.sr_genome)}
```

Post-adaptation weight genome:

```text
{genome_i8_text(best_hybrid.post_weight)}
```

## Reproducibility

- Curve CSV: `{args.curve_csv}`
- Summary CSV: `{args.summary_csv}`
- RNG: XorShift32, seed `{args.seed}`
- Population: `{args.population}`
- Generations: `{args.generations}`
- Adaptation epochs per candidate: `{args.adapt_epochs}`
- Learning-rate shift: `{args.lr_shift}`
"""
    path.write_text(doc)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--population", type=int, default=16)
    ap.add_argument("--generations", type=int, default=32)
    ap.add_argument("--adapt-epochs", type=int, default=1)
    ap.add_argument("--lr-shift", type=int, default=mem.LR_SHIFT)
    ap.add_argument("--init-span", type=int, default=32)
    ap.add_argument("--elites", type=int, default=2)
    ap.add_argument("--tournament", type=int, default=3)
    ap.add_argument("--crossover-rate", type=float, default=0.70)
    ap.add_argument("--mutation-rate", type=float, default=0.03)
    ap.add_argument("--mutation-step", type=int, default=8)
    ap.add_argument("--struct-mutation-ppm", type=int, default=250_000)
    ap.add_argument("--struct-init-mutation-ppm", type=int, default=50_000)
    ap.add_argument("--struct-sel-mutation-ppm", type=int, default=30_000)
    ap.add_argument("--feature-min-balance", type=int, default=8)
    ap.add_argument("--feature-penalty", type=int, default=50_000)
    ap.add_argument("--curve-csv", type=Path, default=DEFAULT_CURVE_CSV)
    ap.add_argument("--summary-csv", type=Path, default=DEFAULT_SUMMARY_CSV)
    ap.add_argument("--doc", type=Path, default=DEFAULT_DOC)
    args = ap.parse_args()

    if sr.truth_mask(SR_MAJORITY, sr.FAULT_NONE) != sr.TARGET_MASK:
        raise SystemExit("SR_MAJORITY self-check failed")
    if sr.truth_mask(SR_REPAIR, sr.FAULT_DISABLE_A1) != sr.TARGET_MASK:
        raise SystemExit("SR_REPAIR self-check failed")

    curve_rows: list[dict[str, str]] = []
    baseline = run_weight_baseline(args, curve_rows)
    results: list[StructResult] = []
    for coupling in COUPLINGS:
        results.append(run_hybrid_mode("hybrid_lamarckian", coupling, args, curve_rows))
    for coupling in COUPLINGS:
        results.append(run_hybrid_mode("hybrid_lamarckian_pressure", coupling, args, curve_rows))
    results.append(run_hybrid_mode("hybrid_no_adapt", "gate_x3", args, curve_rows))

    write_csv(args.curve_csv, curve_rows)
    write_summary(args.summary_csv, baseline, results, args)
    write_doc(args.doc, baseline, results, args)

    print(f"weight_only_lamarckian best={baseline.best_post_score.correct}/40 "
          f"sse={baseline.best_post_score.sse} first_40={baseline.first_40}")
    for r in results:
        first = r.first_40 if r.first_40 is not None else "none"
        print(f"{r.mode}:{r.coupling} best={r.best_score.correct}/40 "
              f"sse={r.best_score.sse} first_40={first} feature_ones={r.feature_ones} "
              f"penalty={r.feature_penalty} faultA1={r.degraded_correct}/40")
    print(f"wrote {args.curve_csv}")
    print(f"wrote {args.summary_csv}")
    print(f"wrote {args.doc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
