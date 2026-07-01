#!/usr/bin/env python3
"""Build and compare the EHW-3.1 spare-routing Python oracle against the C twin."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def write_python_csvs(args: argparse.Namespace, nofault_csv: Path, recovery_csv: Path) -> tuple[str, str]:
    sys.path.insert(0, str(ROOT / "sim"))
    import oracle_spare_routing as oracle

    nofault_trace = oracle.run_ga_trace(
        seed=args.seed,
        fault=oracle.FAULT_NONE,
        population=args.population,
        generations=args.generations,
        elites=args.elites,
        tournament_k=args.tournament,
        crossover_ppm=args.crossover_ppm,
        init_mutation_ppm=args.init_mutation_ppm,
        sel_mutation_ppm=args.sel_mutation_ppm,
        injected=None,
    )
    oracle.write_trace_csv(nofault_csv, nofault_trace)
    nofault_genome = nofault_trace[-1]["genome"]

    recovery_trace = oracle.run_ga_trace(
        seed=args.recovery_seed,
        fault=oracle.FAULT_DISABLE_A1,
        population=args.population,
        generations=args.generations,
        elites=args.elites,
        tournament_k=args.tournament,
        crossover_ppm=args.crossover_ppm,
        init_mutation_ppm=args.init_mutation_ppm,
        sel_mutation_ppm=args.sel_mutation_ppm,
        injected=[oracle.parse_genome_hex(nofault_genome)],
    )
    oracle.write_trace_csv(recovery_csv, recovery_trace)
    recovery_genome = recovery_trace[-1]["genome"]
    return nofault_genome, recovery_genome


def compare_bytes(a: Path, b: Path, label: str) -> None:
    if a.read_bytes() != b.read_bytes():
        print(f"FAIL: {label} CSV mismatch: {a} != {b}", file=sys.stderr)
        raise SystemExit(1)


def final_row(path: Path) -> str:
    lines = path.read_text().splitlines()
    return lines[-1] if len(lines) > 1 else "<empty>"


def python_fault_masks(genome_text: str) -> str:
    sys.path.insert(0, str(ROOT / "sim"))
    import oracle_spare_routing as oracle

    genome = oracle.parse_genome_hex(genome_text)
    faults = [
        oracle.FAULT_NONE,
        oracle.Fault(oracle.FaultKind.STUCK0, node=1),
        oracle.Fault(oracle.FaultKind.STUCK1, node=1),
        oracle.FAULT_DISABLE_A1,
        oracle.Fault(oracle.FaultKind.DISABLE_ROUTE, edge=("out", 0, 1)),
        oracle.Fault(oracle.FaultKind.DISABLE_ROUTE, edge=("node", 1, 0)),
    ]
    return "".join(
        f"{fault.label()},{oracle.truth_mask(genome, fault):02x},{oracle.fitness(genome, fault)}\n"
        for fault in faults
    )


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
    ap.add_argument("--cc", default=os.environ.get("CC", "cc"))
    args = ap.parse_args()

    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / "spare_route_eval"
    py_nofault = out_dir / "spare_route_python_nofault.csv"
    py_recovery = out_dir / "spare_route_python_recovery.csv"
    c_nofault = out_dir / "spare_route_c_nofault.csv"
    c_recovery = out_dir / "spare_route_c_recovery.csv"

    nofault_genome, recovery_genome = write_python_csvs(args, py_nofault, py_recovery)

    run([args.cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-o", str(exe), "sw/ehw/spare_route_eval.c"])
    run([str(exe), "--check-contract"])
    c_faults = subprocess.check_output(
        [str(exe), "--dump-fault-masks", nofault_genome],
        cwd=ROOT,
        text=True,
    )
    py_faults = python_fault_masks(nofault_genome)
    if c_faults != py_faults:
        print("FAIL: fault-model mask mismatch", file=sys.stderr)
        print("Python:\n" + py_faults, file=sys.stderr)
        print("C:\n" + c_faults, file=sys.stderr)
        return 1

    common = [
        "--population", str(args.population),
        "--generations", str(args.generations),
        "--elites", str(args.elites),
        "--tournament", str(args.tournament),
        "--crossover-ppm", str(args.crossover_ppm),
        "--init-mutation-ppm", str(args.init_mutation_ppm),
        "--sel-mutation-ppm", str(args.sel_mutation_ppm),
    ]
    run([str(exe), "--mode", "nofault", "--seed", str(args.seed), *common, "--csv", str(c_nofault)])
    run([
        str(exe),
        "--mode", "recovery",
        "--seed", str(args.recovery_seed),
        "--inject", nofault_genome,
        *common,
        "--csv", str(c_recovery),
    ])

    compare_bytes(py_nofault, c_nofault, "no-fault")
    compare_bytes(py_recovery, c_recovery, "post-fault recovery")

    nofault_last = final_row(py_nofault)
    recovery_last = final_row(py_recovery)
    nofault_cols = nofault_last.split(",")
    recovery_cols = recovery_last.split(",")
    if nofault_cols[2] != "8" or nofault_cols[3] != "e8" or nofault_cols[8] != "1":
        print(f"FAIL: no-fault curve did not end accepted at 8/8 mask e8: {nofault_last}", file=sys.stderr)
        return 1
    if recovery_cols[2] != "8" or recovery_cols[3] != "e8" or recovery_cols[8] != "1" or recovery_genome not in recovery_last:
        print(f"FAIL: recovery curve did not end at 8/8 mask e8: {recovery_last}", file=sys.stderr)
        return 1

    print("PASS: spare-routing Python oracle and C twin are bit-exact")
    print("fault masks:")
    print(py_faults, end="")
    print(f"no-fault last: {nofault_last}")
    print(f"recovery last: {recovery_last}")
    print(f"repaired genome: {recovery_genome}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
