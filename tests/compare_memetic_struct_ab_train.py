#!/usr/bin/env python3
"""EHW-5.4 host gate: same-boot A/B firmware stub vs C twin curves.

Covers both EHW-5.4a built-in four-arm table and EHW-5.4b staged parameter
blocks through the 4.6b framebuf contract.
"""

from __future__ import annotations

import csv
import os
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ARMS = [
    ("weight_only_lamarckian", "none"),
    ("hybrid_lamarckian_pressure", "bias_x3"),
    ("hybrid_no_adapt", "gate_x3"),
    ("hybrid_lamarckian", "bias_x3"),
]
DESC = {
    ("weight_only_lamarckian", "none"): (3, 3, 0x02),
    ("hybrid_lamarckian_pressure", "bias_x3"): (1, 2, 0x07),
    ("hybrid_no_adapt", "gate_x3"): (2, 1, 0x01),
    ("hybrid_lamarckian", "bias_x3"): (0, 2, 0x03),
}
PARAM_MAGIC = 0xE5400001
EXPECTED = {
    ("weight_only_lamarckian", "none"): {
        "best_correct": "40",
        "best_sse": "6116",
        "first_40": "3",
        "sat_count": "0",
    },
    ("hybrid_lamarckian_pressure", "bias_x3"): {
        "best_correct": "40",
        "best_sse": "4513",
        "first_40": "2",
        "feature_ones": "15",
        "feature_penalty": "0",
        "sat_count": "0",
    },
    ("hybrid_no_adapt", "gate_x3"): {
        "best_correct": "40",
        "best_sse": "4615",
        "first_40": "11",
        "feature_ones": "39",
        "feature_penalty": "0",
        "sat_count": "0",
    },
    ("hybrid_lamarckian", "bias_x3"): {
        "best_correct": "40",
        "best_sse": "5837",
        "first_40": "5",
        "feature_ones": "0",
        "feature_penalty": "0",
        "sat_count": "0",
    },
}


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def filter_curve(src: Path, dst: Path, arms: list[tuple[str, str]], generations: int) -> None:
    with src.open(newline="") as f:
        rows = list(csv.reader(f))
    header, body = rows[0], rows[1:]
    out = [header]
    for mode, coupling in arms:
        arm_rows = [r for r in body if r[0] == mode and r[1] == coupling]
        expected_rows = generations + 1
        if len(arm_rows) != expected_rows:
            raise SystemExit(f"expected {expected_rows} curve rows for {mode}/{coupling}, got {len(arm_rows)}")
        out.extend(arm_rows)
    with dst.open("w", newline="") as f:
        w = csv.writer(f, lineterminator="\n")
        w.writerows(out)


def check_summary(path: Path) -> None:
    with path.open(newline="") as f:
        rows = {(r["mode"], r["coupling"]): r for r in csv.DictReader(f)}
    for key in ARMS:
        row = rows.get(key)
        if row is None:
            raise SystemExit(f"missing summary row {key}")
        for field, expected in EXPECTED[key].items():
            if row[field] != expected:
                raise SystemExit(f"{key} {field}: got {row[field]}, expected {expected}")


def write_param_image(path: Path, arms: list[tuple[str, str]], *,
                      seed: int = 3, population: int = 16,
                      generations: int = 32, adapt_epochs: int = 1,
                      feature_min_balance: int = 8,
                      feature_penalty: int = 50000) -> None:
    words = [
        PARAM_MAGIC,
        len(arms),
        seed,
        population,
        generations,
        adapt_epochs,
        feature_min_balance,
        feature_penalty,
    ]
    for arm in arms:
        mode, coupling, flags = DESC[arm]
        words.append(mode | (coupling << 8) | (flags << 16))
    path.write_bytes(b"".join(struct.pack("<I", w & 0xffffffff) for w in words))


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    ref_exe = out_dir / "memetic_struct_eval"
    fw_exe = out_dir / "memetic_struct_ab_mbox"
    ref_curve_all = out_dir / "ehw5_4_ref_all_curves.csv"
    ref_summary = out_dir / "ehw5_4_ref_summary.csv"
    ref_curve = out_dir / "ehw5_4_ref_curve.csv"
    fw_curve = out_dir / "ehw5_4_fw_curve.csv"
    fw_staged_curve = out_dir / "ehw5_4b_fw_staged_curve.csv"
    ref_short_all = out_dir / "ehw5_4b_ref_short_all_curves.csv"
    ref_short_summary = out_dir / "ehw5_4b_ref_short_summary.csv"
    ref_short_curve = out_dir / "ehw5_4b_ref_short_curve.csv"
    fw_short_curve = out_dir / "ehw5_4b_fw_short_curve.csv"
    param_same = out_dir / "ehw5_4b_param_same.bin"
    param_short = out_dir / "ehw5_4b_param_short.bin"

    common = [
        "--seed", "3",
        "--population", "16",
        "--generations", "32",
        "--adapt-epochs", "1",
    ]
    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-I", "sw/ehw",
         "-o", str(ref_exe), "sw/ehw/memetic_struct_eval.c"])
    run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DMEMETIC_STRUCT_GA_HOST_STUB",
         "-I", "sw/ehw", "-o", str(fw_exe), "sw/ehw/memetic_struct_ab_mbox.c"])
    run([str(ref_exe), *common, "--curve-csv", str(ref_curve_all), "--summary-csv", str(ref_summary)])
    filter_curve(ref_curve_all, ref_curve, ARMS, generations=32)
    run([str(fw_exe), "--curve-csv", str(fw_curve)])

    if ref_curve.read_bytes() != fw_curve.read_bytes():
        print(f"FAIL: EHW-5.4a curve mismatch: {ref_curve} != {fw_curve}", file=sys.stderr)
        return 1
    check_summary(ref_summary)

    write_param_image(param_same, ARMS)
    run([str(fw_exe), "--param-bin", str(param_same), "--curve-csv", str(fw_staged_curve)])
    if ref_curve.read_bytes() != fw_staged_curve.read_bytes():
        print(f"FAIL: EHW-5.4b staged default curve mismatch: {ref_curve} != {fw_staged_curve}", file=sys.stderr)
        return 1

    short_arms = [("hybrid_lamarckian_pressure", "bias_x3")]
    write_param_image(param_short, short_arms, generations=4)
    short_common = [
        "--seed", "3",
        "--population", "16",
        "--generations", "4",
        "--adapt-epochs", "1",
    ]
    run([str(ref_exe), *short_common, "--curve-csv", str(ref_short_all), "--summary-csv", str(ref_short_summary)])
    filter_curve(ref_short_all, ref_short_curve, short_arms, generations=4)
    run([str(fw_exe), "--param-bin", str(param_short), "--curve-csv", str(fw_short_curve)])
    if ref_short_curve.read_bytes() != fw_short_curve.read_bytes():
        print(f"FAIL: EHW-5.4b staged short curve mismatch: {ref_short_curve} != {fw_short_curve}", file=sys.stderr)
        return 1
    if fw_short_curve.read_bytes() == fw_curve.read_bytes():
        print("FAIL: EHW-5.4b staged short run did not change the firmware curve", file=sys.stderr)
        return 1

    print("PASS: EHW-5.4a same-boot A/B firmware stub curves are byte-exact")
    print("PASS: EHW-5.4a four-arm expected summary fields match")
    print("PASS: EHW-5.4b staged default param block matches the built-in table")
    print("PASS: EHW-5.4b staged short scan changes the run and remains byte-exact")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
