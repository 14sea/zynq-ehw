#!/usr/bin/env python3
"""EHW-4.5 host gate: same-boot Baldwinian/Lamarckian train-unit firmware stub."""

from __future__ import annotations

import csv
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODES = {"baldwinian", "lamarckian"}


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def filter_ab(src: Path, dst: Path) -> None:
    with src.open(newline="") as inf, dst.open("w", newline="") as outf:
        reader = csv.reader(inf)
        writer = csv.writer(outf, lineterminator="\n")
        header = next(reader)
        writer.writerow(header)
        for row in reader:
            if row and row[0] in MODES:
                writer.writerow(row)


def first_40(rows: list[dict[str, str]], mode: str) -> int | None:
    for row in rows:
        if row["mode"] == mode and row["best_correct"] == "40":
            return int(row["gen"])
    return None


def final_row(rows: list[dict[str, str]], mode: str) -> dict[str, str]:
    mode_rows = [row for row in rows if row["mode"] == mode]
    if not mode_rows:
        raise AssertionError(f"missing mode {mode}")
    return mode_rows[-1]


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    ref_exe = out_dir / "memetic_eval_ref"
    fw_exe = out_dir / "memetic_ab_train_mbox"
    ref_all = out_dir / "ehw4_5_ref_all_curves.csv"
    ref_summary = out_dir / "ehw4_5_ref_summary.csv"
    ref_ab = out_dir / "ehw4_5_ref_ab.csv"
    fw_curve = out_dir / "ehw4_5_fw_ab.csv"

    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-I", "sw/ehw",
         "-o", str(ref_exe), "sw/ehw/memetic_eval.c"])
    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DMEMETIC_AB_HOST_STUB",
         "-I", "sw/ehw", "-o", str(fw_exe), "sw/ehw/memetic_ab_train_mbox.c"])
    common = ["--population", "16", "--generations", "32", "--adapt-epochs", "1"]
    run([str(ref_exe), *common, "--curve-csv", str(ref_all), "--summary-csv", str(ref_summary)])
    filter_ab(ref_all, ref_ab)
    run([str(fw_exe), "--curve-csv", str(fw_curve)])

    if ref_ab.read_bytes() != fw_curve.read_bytes():
        print(f"FAIL: A/B firmware curve mismatch: {ref_ab} != {fw_curve}", file=sys.stderr)
        return 1

    rows = list(csv.DictReader(fw_curve.open(newline="")))
    if len(rows) != 66:
        print(f"FAIL: expected 66 A/B rows, got {len(rows)}", file=sys.stderr)
        return 1
    b_final = final_row(rows, "baldwinian")
    l_final = final_row(rows, "lamarckian")
    b_first = first_40(rows, "baldwinian")
    l_first = first_40(rows, "lamarckian")
    if b_final["best_correct"] != "40" or l_final["best_correct"] != "40":
        print("FAIL: expected both arms to finish 40/40", file=sys.stderr)
        return 1
    if b_first != 29 or l_first != 3:
        print(f"FAIL: expected first_40 B=29 L=3, got B={b_first} L={l_first}", file=sys.stderr)
        return 1
    print("PASS: EHW-4.5 same-boot A/B firmware curves are bit-exact vs memetic_eval")
    print(f"PASS: Baldwinian first_40={b_first} sse={b_final['best_sse']}; "
          f"Lamarckian first_40={l_first} sse={l_final['best_sse']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
