#!/usr/bin/env python3
"""EHW-3.3 baked spare-route host gate: RTL sim, firmware stub, INIT diff, OOC hook."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

BASE = [0x0A, 0x08, 0x01, 0x0F, 0x32, 0x01, 0x04, 0x00,
        0x02, 0x02, 0x00, 0x04, 0x01, 0x01, 0x02, 0x00]
REPAIR = [0x0B, 0x09, 0x09, 0x03, 0xB1, 0x00, 0x04, 0x04,
          0x01, 0x02, 0x00, 0x00, 0x01, 0x02, 0x03, 0x00]
EXPECTED_CHANGED = {"g0", "g1", "g2", "g3", "g4", "g5", "g7", "g8", "g11", "g13", "g14"}


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def pool_mux_init(sel: int) -> int:
    if sel == 0:
        return 0xAAAA_AAAA_AAAA_AAAA
    if sel == 1:
        return 0xCCCC_CCCC_CCCC_CCCC
    if sel == 2:
        return 0xF0F0_F0F0_F0F0_F0F0
    if sel == 4:
        return 0xFFFF_0000_FFFF_0000
    return 0


def out_mux_init(sel: int) -> int:
    if sel == 0:
        return 0xAAAA
    if sel == 1:
        return 0xCCCC
    if sel == 2:
        return 0xF0F0
    if sel == 3:
        return 0xFF00
    return 0xAAAA


def primitive_inits(genome: list[int]) -> dict[str, tuple[int, int]]:
    table: dict[str, tuple[int, int]] = {}
    for i in range(4):
        table[f"g{i}"] = (4, genome[i] & 0xF)
    table["g4"] = (8, genome[4] & 0xFF)
    for i in range(5, 13):
        table[f"g{i}"] = (64, pool_mux_init(genome[i]))
    for i in range(13, 16):
        table[f"g{i}"] = (16, out_mux_init(genome[i]))
    return table


def check_init_diff() -> None:
    base = primitive_inits(BASE)
    repair = primitive_inits(REPAIR)
    changed = {name for name in base if base[name] != repair[name]}
    if changed != EXPECTED_CHANGED:
        raise RuntimeError(f"INIT diff changed {sorted(changed)}, expected {sorted(EXPECTED_CHANGED)}")
    print("INIT diff target cells:")
    for name in sorted(changed, key=lambda n: int(n[1:])):
        width, before = base[name]
        _, after = repair[name]
        nibbles = width // 4
        print(f"  {name}: {width}'h{before:0{nibbles}x} -> {width}'h{after:0{nibbles}x}")
    print("PASS: only intended baked LUT/select INITs differ")


def run_rtl(name: str, repaired: bool) -> None:
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / f"tb_spare_route_baked_{name}.vvp"
    cmd = [
        "iverilog", "-g2012", "-Wall", "-DSIM", "-s", "tb_spare_route_baked",
        "-o", str(exe),
    ]
    if repaired:
        cmd.insert(4, "-DSR_BAKED_REPAIR")
    cmd += ["rtl/spare_route_baked.v", "tests/tb_spare_route_baked.v"]
    run(cmd)
    run(["vvp", str(exe)])


def check_rm_wrapper(name: str, wrapper: str) -> None:
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / f"rm_{name}.vvp"
    run([
        "iverilog", "-g2012", "-Wall", "-DSIM", "-s", "tpu_rp",
        "-o", str(exe),
        "rtl/spare_route_baked.v", wrapper,
    ])


def run_fw(name: str, repaired: bool) -> None:
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / f"spare_route_baked_post_{name}"
    cmd = [
        "cc", "-std=c99", "-Wall", "-Wextra", "-DSR_HOST_STUB",
        "-I", "sw/ehw", "-o", str(exe), "sw/ehw/spare_route_baked_post.c",
    ]
    if repaired:
        cmd.insert(5, "-DSR_BAKED_REPAIR")
    run(cmd)
    run([str(exe)])


def run_ooc() -> None:
    vivado = shutil.which("vivado")
    if not vivado:
        print("SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_spare_route_baked.tcl")
        return
    run([vivado, "-mode", "batch", "-source", "tests/vivado_ooc_spare_route_baked.tcl"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-ooc", action="store_true", help="skip Vivado OOC synth even if vivado exists")
    args = ap.parse_args()

    if not shutil.which("iverilog") or not shutil.which("vvp"):
        print("FAIL: iverilog and vvp are required", file=sys.stderr)
        return 2

    check_init_diff()
    run_rtl("base", repaired=False)
    run_rtl("repair", repaired=True)
    check_rm_wrapper("spare_route_baked_base", "rtl/dfx/tpu_rp_rm_spare_route_baked_base.v")
    check_rm_wrapper("spare_route_baked_repair", "rtl/dfx/tpu_rp_rm_spare_route_baked_repair.v")
    run_fw("base", repaired=False)
    run_fw("repair", repaired=True)
    if not args.skip_ooc:
        run_ooc()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
