#!/usr/bin/env python3
"""EHW-3.0 host oracle: evolved spare-routing island recovery.

Frozen genome byte contract for EHW-3.0+:

  byte  field
  ----  ---------------------------------------------------------------
  0     logic_init[0]  A0 2-input LUT4 INIT, low nibble valid
  1     logic_init[1]  A1 2-input LUT4 INIT, low nibble valid
  2     logic_init[2]  A2 2-input LUT4 INIT, low nibble valid
  3     logic_init[3]  AS 2-input LUT4 INIT, low nibble valid
  4     init_out       O  3-input LUT8 INIT, full byte valid
  5     node_sel[0][0] A0 in0 source select into P=[x0,x1,x2,ZERO,ONE]
  6     node_sel[0][1] A0 in1 source select into P
  7     node_sel[1][0] A1 in0 source select into P
  8     node_sel[1][1] A1 in1 source select into P
  9     node_sel[2][0] A2 in0 source select into P
  10    node_sel[2][1] A2 in1 source select into P
  11    node_sel[3][0] AS in0 source select into P
  12    node_sel[3][1] AS in1 source select into P
  13    out_sel[0]     O in0 source select into [A0,A1,A2,AS]
  14    out_sel[1]     O in1 source select into [A0,A1,A2,AS]
  15    out_sel[2]     O in2 source select into [A0,A1,A2,AS]

LUT decode is also frozen:

  C1 node: out = (INIT >> (in1 << 1 | in0)) & 1
  O node : out = (INIT >> (in2 << 2 | in1 << 1 | in0)) & 1

Validity layer:

  node_sel >= 5 decodes to ZERO. out_sel >= 4 decodes to A0. Every genome byte
  string therefore maps to a legal single-driver circuit; selectors are pure
  fan-in muxes, never buses. Faults are injected during evaluation and are not
  part of the genome.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from enum import Enum
from pathlib import Path


TARGET_MASK = 0xE8
FITNESS_MAX = 8
GENOME_LEN = 16

NODE_NAMES = ["A0", "A1", "A2", "AS"]
POOL_NAMES = ["x0", "x1", "x2", "ZERO", "ONE"]

INIT_FIELDS = ["logic_init[A0]", "logic_init[A1]", "logic_init[A2]", "logic_init[AS]", "init_out[O]"]
SEL_FIELDS = [
    "node_sel[A0][in0]",
    "node_sel[A0][in1]",
    "node_sel[A1][in0]",
    "node_sel[A1][in1]",
    "node_sel[A2][in0]",
    "node_sel[A2][in1]",
    "node_sel[AS][in0]",
    "node_sel[AS][in1]",
    "out_sel[O][in0]",
    "out_sel[O][in1]",
    "out_sel[O][in2]",
]
FIELD_NAMES = INIT_FIELDS + SEL_FIELDS


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

    def chance_ppm(self, ppm: int) -> bool:
        return self.randrange(1_000_000) < ppm


class FaultKind(Enum):
    NONE = "FAULT_NONE"
    STUCK0 = "FAULT_STUCK0"
    STUCK1 = "FAULT_STUCK1"
    DISABLE_NODE = "FAULT_DISABLE_NODE"
    DISABLE_ROUTE = "FAULT_DISABLE_ROUTE"


@dataclass(frozen=True)
class Fault:
    kind: FaultKind = FaultKind.NONE
    node: int | None = None
    edge: tuple[str, int, int] | None = None

    def label(self) -> str:
        if self.kind is FaultKind.NONE:
            return self.kind.value
        if self.node is not None:
            return f"{self.kind.value}({NODE_NAMES[self.node]})"
        if self.edge is not None:
            section, idx, mux = self.edge
            name = NODE_NAMES[idx] if section == "node" else "O"
            return f"{self.kind.value}({name}.in{mux})"
        return self.kind.value


FAULT_NONE = Fault()
FAULT_DISABLE_A1 = Fault(FaultKind.DISABLE_NODE, node=1)


def lut2(init: int, in0: int, in1: int) -> int:
    return ((init & 0xF) >> ((in1 << 1) | in0)) & 1


def lut3(init: int, in0: int, in1: int, in2: int) -> int:
    return ((init & 0xFF) >> ((in2 << 2) | (in1 << 1) | in0)) & 1


def decode_node_sel(raw: int) -> int:
    return raw if 0 <= raw < len(POOL_NAMES) else 3  # ZERO


def decode_out_sel(raw: int) -> int:
    return raw if 0 <= raw < len(NODE_NAMES) else 0  # A0


def force_default_for_route(fault: Fault, section: str, idx: int, mux: int) -> bool:
    return fault.kind is FaultKind.DISABLE_ROUTE and fault.edge == (section, idx, mux)


def c1_node_value(genome: list[int], node: int, pool: list[int], fault: Fault) -> int:
    sel0 = 3 if force_default_for_route(fault, "node", node, 0) else decode_node_sel(genome[5 + 2 * node])
    sel1 = 3 if force_default_for_route(fault, "node", node, 1) else decode_node_sel(genome[6 + 2 * node])
    value = lut2(genome[node], pool[sel0], pool[sel1])
    if fault.node == node:
        if fault.kind is FaultKind.STUCK0 or fault.kind is FaultKind.DISABLE_NODE:
            return 0
        if fault.kind is FaultKind.STUCK1:
            return 1
    return value


def eval_row(genome: list[int], row: int, fault: Fault = FAULT_NONE) -> int:
    pool = [
        (row >> 0) & 1,
        (row >> 1) & 1,
        (row >> 2) & 1,
        0,
        1,
    ]
    nodes = [c1_node_value(genome, i, pool, fault) for i in range(4)]
    ins: list[int] = []
    for mux in range(3):
        sel = 0 if force_default_for_route(fault, "out", 0, mux) else decode_out_sel(genome[13 + mux])
        if fault.kind is FaultKind.DISABLE_NODE and fault.node == sel:
            ins.append(0)
        else:
            ins.append(nodes[sel])
    return lut3(genome[4], ins[0], ins[1], ins[2])


def truth_mask(genome: list[int], fault: Fault = FAULT_NONE) -> int:
    mask = 0
    for row in range(8):
        mask |= eval_row(genome, row, fault) << row
    return mask


def fitness(genome: list[int], fault: Fault = FAULT_NONE) -> int:
    return FITNESS_MAX - (truth_mask(genome, fault) ^ TARGET_MASK).bit_count()


def out_uses_node(genome: list[int], node: int) -> bool:
    return any(decode_out_sel(raw) == node for raw in genome[13:16])


def out_uses_spare(genome: list[int]) -> bool:
    return out_uses_node(genome, 3)


def genome_hex(genome: list[int]) -> str:
    return " ".join(f"{v & 0xFF:02x}" for v in genome)


def parse_genome_hex(text: str) -> list[int]:
    vals = [int(part, 16) for part in text.split()]
    if len(vals) != GENOME_LEN:
        raise ValueError(f"expected {GENOME_LEN} bytes, got {len(vals)}")
    return [v & 0xFF for v in vals]


def random_genome(rng: XorShift32) -> list[int]:
    return (
        [rng.randrange(16) for _ in range(4)]
        + [rng.randrange(256)]
        + [rng.randrange(5) for _ in range(8)]
        + [rng.randrange(4) for _ in range(3)]
    )


def scaffold_genome(rng: XorShift32, use_spare: bool) -> list[int]:
    """Seed useful but unsolved building blocks: identity-ish C1 nodes, random O INIT."""
    genome = [0xA, 0xA, 0xA, 0xA if use_spare else 0x0, rng.randrange(256)]
    for src in [0, 1, 2, 1 if use_spare else 3]:
        genome.extend([src, src])
    genome.extend([0, 3, 2] if use_spare else [0, 1, 2])
    return genome


def mutate(genome: list[int], rng: XorShift32, init_ppm: int, sel_ppm: int) -> list[int]:
    out = genome[:]
    changed = False
    for idx, bits in [(0, 4), (1, 4), (2, 4), (3, 4), (4, 8)]:
        value = out[idx]
        for bit in range(bits):
            if rng.chance_ppm(init_ppm):
                value ^= 1 << bit
                changed = True
        out[idx] = value & ((1 << bits) - 1)
    for idx, limit in [(i, 5) for i in range(5, 13)] + [(i, 4) for i in range(13, 16)]:
        if rng.chance_ppm(sel_ppm):
            out[idx] = rng.randrange(limit)
            changed = True
    if not changed:
        idx = rng.randrange(GENOME_LEN)
        if idx < 4:
            out[idx] ^= 1 << rng.randrange(4)
        elif idx == 4:
            out[idx] ^= 1 << rng.randrange(8)
        elif idx < 13:
            out[idx] = rng.randrange(5)
        else:
            out[idx] = rng.randrange(4)
    return out


def crossover(a: list[int], b: list[int], rng: XorShift32) -> list[int]:
    return [a[i] if rng.chance_ppm(500_000) else b[i] for i in range(GENOME_LEN)]


def tournament(scored: list[tuple[tuple[int, int, int], list[int]]], rng: XorShift32, k: int) -> list[int]:
    best = scored[rng.randrange(len(scored))]
    for _ in range(1, k):
        candidate = scored[rng.randrange(len(scored))]
        if candidate[0] > best[0]:
            best = candidate
    return best[1]


def score_key(genome: list[int], fault: Fault) -> tuple[int, int, int]:
    fit = fitness(genome, fault)
    if fault.kind is FaultKind.NONE:
        a1_drop = fit - fitness(genome, FAULT_DISABLE_A1)
        return (fit, a1_drop, int(out_uses_node(genome, 1)))
    return (fit, int(out_uses_spare(genome)), int(not out_uses_node(genome, 1)))


def run_ga(
    *,
    seed: int,
    fault: Fault,
    population: int,
    generations: int,
    elites: int,
    tournament_k: int,
    crossover_ppm: int,
    init_mutation_ppm: int,
    sel_mutation_ppm: int,
    injected: list[list[int]] | None,
) -> tuple[list[int], int, int]:
    trace = run_ga_trace(
        seed=seed,
        fault=fault,
        population=population,
        generations=generations,
        elites=elites,
        tournament_k=tournament_k,
        crossover_ppm=crossover_ppm,
        init_mutation_ppm=init_mutation_ppm,
        sel_mutation_ppm=sel_mutation_ppm,
        injected=injected,
    )
    if not trace:
        raise RuntimeError("empty GA trace")
    last = trace[-1]
    return parse_genome_hex(last["genome"]), int(last["best_fitness"]), int(last["best_gen"])


def run_ga_trace(
    *,
    seed: int,
    fault: Fault,
    population: int,
    generations: int,
    elites: int,
    tournament_k: int,
    crossover_ppm: int,
    init_mutation_ppm: int,
    sel_mutation_ppm: int,
    injected: list[list[int]] | None,
) -> list[dict[str, str]]:
    rng = XorShift32(seed)
    pop: list[list[int]] = []
    for _ in range(16):
        pop.append(scaffold_genome(rng, use_spare=False))
    for _ in range(16):
        pop.append(scaffold_genome(rng, use_spare=True))
    if injected:
        pop.extend(g[:] for g in injected)
    while len(pop) < population:
        pop.append(random_genome(rng))
    pop = pop[:population]

    best = pop[0][:]
    best_key = (-1, -1, -1)
    best_gen = 0
    trace: list[dict[str, str]] = []

    for gen in range(generations + 1):
        scored = sorted(((score_key(g, fault), g) for g in pop), key=lambda item: (item[0], item[1]), reverse=True)
        if scored[0][0] > best_key:
            best_key = scored[0][0]
            best = scored[0][1][:]
            best_gen = gen

        current_fit = fitness(best, fault)
        current_mask = truth_mask(best, fault)
        no_fault_mask = truth_mask(best, FAULT_NONE)
        disable_a1_fit = fitness(best, FAULT_DISABLE_A1)
        if fault.kind is FaultKind.NONE:
            accepted = current_fit == FITNESS_MAX and disable_a1_fit < FITNESS_MAX
        else:
            accepted = (
                current_fit == FITNESS_MAX
                and out_uses_spare(best)
                and not out_uses_node(best, 1)
            )
        trace.append({
            "gen": str(gen),
            "best_gen": str(best_gen),
            "best_fitness": str(current_fit),
            "mask": f"{current_mask:02x}",
            "no_fault_mask": f"{no_fault_mask:02x}",
            "disable_a1_fitness": str(disable_a1_fit),
            "uses_a1": str(int(out_uses_node(best, 1))),
            "uses_as": str(int(out_uses_spare(best))),
            "accepted": str(int(accepted)),
            "genome": genome_hex(best),
        })
        if accepted:
            return trace

        next_pop = [g[:] for _, g in scored[:elites]]
        while len(next_pop) < population:
            parent = tournament(scored, rng, tournament_k)
            if rng.chance_ppm(crossover_ppm):
                child = crossover(parent, tournament(scored, rng, tournament_k), rng)
            else:
                child = parent[:]
            next_pop.append(mutate(child, rng, init_mutation_ppm, sel_mutation_ppm))
        pop = next_pop

    return trace


def write_trace_csv(path: Path, trace: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = [
        "gen",
        "best_gen",
        "best_fitness",
        "mask",
        "no_fault_mask",
        "disable_a1_fitness",
        "uses_a1",
        "uses_as",
        "accepted",
        "genome",
    ]
    with path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(trace)


def field_value_text(index: int, value: int) -> str:
    if index < 4:
        return f"0x{value & 0xF:x}"
    if index == 4:
        return f"0x{value & 0xFF:02x}"
    if index < 13:
        decoded = decode_node_sel(value)
        suffix = "" if value == decoded else f" -> {POOL_NAMES[decoded]}"
        return f"{value}({POOL_NAMES[decoded]}){suffix}"
    decoded = decode_out_sel(value)
    suffix = "" if value == decoded else f" -> {NODE_NAMES[decoded]}"
    return f"{value}({NODE_NAMES[decoded]}){suffix}"


def field_diff(before: list[int], after: list[int]) -> list[str]:
    lines = []
    for idx, (a, b) in enumerate(zip(before, after)):
        if a != b:
            lines.append(f"{FIELD_NAMES[idx]}: {field_value_text(idx, a)} -> {field_value_text(idx, b)}")
    return lines


def decode_text(genome: list[int]) -> str:
    lines = []
    for node in range(4):
        s0 = decode_node_sel(genome[5 + 2 * node])
        s1 = decode_node_sel(genome[6 + 2 * node])
        lines.append(
            f"{NODE_NAMES[node]} = LUT4(init=0x{genome[node] & 0xF:x}, "
            f"in0={POOL_NAMES[s0]}, in1={POOL_NAMES[s1]})"
        )
    out_sels = [decode_out_sel(v) for v in genome[13:16]]
    lines.append(
        f"O  = LUT8(init=0x{genome[4] & 0xFF:02x}, "
        f"in0={NODE_NAMES[out_sels[0]]}, in1={NODE_NAMES[out_sels[1]]}, in2={NODE_NAMES[out_sels[2]]})"
    )
    return "\n".join(lines)


def truth_table_text(genome: list[int], fault: Fault) -> str:
    lines = ["row x2x1x0 out gold"]
    for row in range(8):
        out = eval_row(genome, row, fault)
        gold = (TARGET_MASK >> row) & 1
        lines.append(f"{row:3d}   {((row >> 2) & 1)}{((row >> 1) & 1)}{row & 1}    {out}   {gold}")
    return "\n".join(lines)


def print_run(label: str, genome: list[int], fault: Fault, generation: int) -> None:
    mask = truth_mask(genome, fault)
    print(f"== {label} ==")
    print(f"fault={fault.label()}")
    print(f"generation={generation}")
    print(f"fitness={fitness(genome, fault)}/8")
    print(f"mask=0x{mask:02x} target=0x{TARGET_MASK:02x}")
    print(f"genome={genome_hex(genome)}")
    print(decode_text(genome))
    print(truth_table_text(genome, fault))
    print()


def reference_representability_check() -> bool:
    no_fault = [0xA, 0xA, 0xA, 0x0, 0xE8, 0, 0, 1, 1, 2, 2, 3, 3, 0, 1, 2]
    repair = [0xA, 0x0, 0xA, 0xA, 0xE8, 0, 0, 3, 3, 2, 2, 1, 1, 0, 3, 2]
    return truth_mask(no_fault, FAULT_NONE) == TARGET_MASK and truth_mask(repair, FAULT_DISABLE_A1) == TARGET_MASK


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--recovery-seed", type=int, default=4)
    ap.add_argument("--population", type=int, default=128)
    ap.add_argument("--generations", type=int, default=1000)
    ap.add_argument("--elites", type=int, default=4)
    ap.add_argument("--tournament", type=int, default=3)
    ap.add_argument("--crossover-ppm", type=int, default=700_000)
    ap.add_argument("--init-mutation-ppm", type=int, default=50_000)
    ap.add_argument("--sel-mutation-ppm", type=int, default=30_000)
    args = ap.parse_args()

    if not reference_representability_check():
        print("representability_self_check=FAIL")
        return 1
    print("representability_self_check=PASS target=0xe8 output_lut=LUT8")
    print("validity_layer=PASS pure fan-in muxes; node_sel>=5->ZERO; out_sel>=4->A0")
    print()

    no_fault, no_fault_fit, no_fault_gen = run_ga(
        seed=args.seed,
        fault=FAULT_NONE,
        population=args.population,
        generations=args.generations,
        elites=args.elites,
        tournament_k=args.tournament,
        crossover_ppm=args.crossover_ppm,
        init_mutation_ppm=args.init_mutation_ppm,
        sel_mutation_ppm=args.sel_mutation_ppm,
        injected=None,
    )
    if no_fault_fit != FITNESS_MAX:
        print("no_fault_result=FAIL")
        return 1
    print_run("no-fault champion", no_fault, FAULT_NONE, no_fault_gen)

    degraded_fit = fitness(no_fault, FAULT_DISABLE_A1)
    degraded_mask = truth_mask(no_fault, FAULT_DISABLE_A1)
    print("== injected fault on no-fault champion ==")
    print(f"fault={FAULT_DISABLE_A1.label()}")
    print(f"fitness={degraded_fit}/8")
    print(f"mask=0x{degraded_mask:02x} target=0x{TARGET_MASK:02x}")
    print()
    if degraded_fit >= FITNESS_MAX:
        print("fault_drop=FAIL")
        return 1

    repaired, repaired_fit, repaired_gen = run_ga(
        seed=args.recovery_seed,
        fault=FAULT_DISABLE_A1,
        population=args.population,
        generations=args.generations,
        elites=args.elites,
        tournament_k=args.tournament,
        crossover_ppm=args.crossover_ppm,
        init_mutation_ppm=args.init_mutation_ppm,
        sel_mutation_ppm=args.sel_mutation_ppm,
        injected=[no_fault],
    )
    if repaired_fit != FITNESS_MAX:
        print("recovery_result=FAIL")
        return 1
    print_run("post-fault repaired champion", repaired, FAULT_DISABLE_A1, repaired_gen)

    print("== field-level repair diff ==")
    diffs = field_diff(no_fault, repaired)
    for line in diffs:
        print(line)
    print("physical_routing: unchanged (fixed-route island; genome contains only INIT/select fields)")
    print()
    print("summary=PASS no_fault=8/8 degraded<8 repaired=8/8 repaired_mask=0xe8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
