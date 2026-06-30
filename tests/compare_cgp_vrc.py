#!/usr/bin/env python3
"""Build and run the EHW-1.1-fabric CGP VRC RTL host gate."""

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


def run_ooc() -> None:
    vivado = shutil.which("vivado")
    if not vivado:
        print("SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_cgp_vrc.tcl")
        return
    run([vivado, "-mode", "batch", "-source", "tests/vivado_ooc_cgp_vrc.tcl"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-ooc", action="store_true", help="skip Vivado OOC synth even if vivado exists")
    args = ap.parse_args()

    if not shutil.which("iverilog") or not shutil.which("vvp"):
        print("FAIL: iverilog and vvp are required for the CGP VRC RTL gate", file=sys.stderr)
        return 2

    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    rtl_exe = out_dir / "tb_cgp_vrc.vvp"
    fw_exe = out_dir / "cgp_vrc_mbox"

    run([
        "iverilog",
        "-g2012",
        "-Wall",
        "-s",
        "tb_cgp_vrc",
        "-o",
        str(rtl_exe),
        "rtl/cgp_vrc.v",
        "rtl/dfx/tpu_rp_rm_cgp_vrc.v",
        "tests/tb_cgp_vrc.v",
    ])
    run(["vvp", str(rtl_exe)])
    run([
        "cc",
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-DCGP_HOST_STUB",
        "-I",
        "sw/ehw",
        "-o",
        str(fw_exe),
        "sw/ehw/cgp_vrc_mbox.c",
    ])
    run([str(fw_exe)])
    if not args.skip_ooc:
        run_ooc()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
