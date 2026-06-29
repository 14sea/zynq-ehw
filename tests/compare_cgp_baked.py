#!/usr/bin/env python3
"""EHW-1.2 baked-CGP host gate: RTL sim, firmware stub, optional Vivado OOC synth."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def run_rtl(name: str, champion: bool) -> None:
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = out_dir / f"tb_cgp_baked_{name}.vvp"
    cmd = [
        "iverilog", "-g2012", "-Wall", "-DSIM", "-s", "tb_cgp_baked",
        "-o", str(exe),
    ]
    if champion:
        cmd.insert(4, "-DCGP_BAKED_CHAMPION")
    cmd += ["rtl/cgp_baked.v", "tests/tb_cgp_baked.v"]
    run(cmd)
    run(["vvp", str(exe)])


def check_rm_wrapper(name: str, wrapper: str) -> None:
    out_dir = ROOT / "runs" / "tests"
    exe = out_dir / f"rm_{name}.vvp"
    run([
        "iverilog", "-g2012", "-Wall", "-DSIM", "-s", "tpu_rp",
        "-o", str(exe),
        "rtl/cgp_baked.v", wrapper,
    ])


def run_fw(name: str, champion: bool) -> None:
    out_dir = ROOT / "runs" / "tests"
    exe = out_dir / f"cgp_baked_post_{name}"
    cmd = [
        "cc", "-std=c99", "-Wall", "-Wextra", "-DCGP_HOST_STUB",
        "-I", "sw/ehw", "-o", str(exe), "sw/ehw/cgp_baked_post.c",
    ]
    if champion:
        cmd.insert(5, "-DCGP_BAKED_CHAMPION")
    run(cmd)
    run([str(exe)])


def run_ooc() -> None:
    vivado = shutil.which("vivado")
    if not vivado:
        print("SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_cgp_baked.tcl")
        return
    run([vivado, "-mode", "batch", "-source", "tests/vivado_ooc_cgp_baked.tcl"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-ooc", action="store_true", help="skip Vivado OOC synth even if vivado exists")
    args = ap.parse_args()

    if not shutil.which("iverilog") or not shutil.which("vvp"):
        print("FAIL: iverilog and vvp are required", file=sys.stderr)
        return 2

    run_rtl("base", champion=False)
    run_rtl("champ", champion=True)
    check_rm_wrapper("cgp_baked_base", "rtl/dfx/tpu_rp_rm_cgp_baked_base.v")
    check_rm_wrapper("cgp_baked_champ", "rtl/dfx/tpu_rp_rm_cgp_baked_champ.v")
    run_fw("base", champion=False)
    run_fw("champ", champion=True)
    if not args.skip_ooc:
        run_ooc()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
