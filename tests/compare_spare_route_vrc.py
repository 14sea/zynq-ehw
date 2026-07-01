#!/usr/bin/env python3
"""EHW-3.2 spare-routing VRC host gate: RTL sim, firmware stub, Vivado OOC synth."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def run(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, cwd=ROOT, check=True, text=True, capture_output=False)


def run_capture(cmd: list[str]) -> str:
    print("+", " ".join(cmd), flush=True)
    return subprocess.check_output(cmd, cwd=ROOT, text=True)


def run_ooc() -> None:
    vivado = shutil.which("vivado")
    if not vivado:
        print("SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_spare_route_vrc.tcl")
        return
    run([vivado, "-mode", "batch", "-source", "tests/vivado_ooc_spare_route_vrc.tcl"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-ooc", action="store_true", help="skip Vivado OOC synth even if vivado exists")
    ap.add_argument("--cc", default="cc")
    args = ap.parse_args()

    if not shutil.which("iverilog") or not shutil.which("vvp"):
        print("FAIL: iverilog and vvp are required for the spare-route VRC RTL gate", file=sys.stderr)
        return 2

    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    rtl_exe = out_dir / "tb_spare_route_vrc.vvp"
    fw_exe = out_dir / "spare_route_vrc_mbox"
    rm_exe = out_dir / "rm_spare_route_vrc.vvp"

    run([
        "iverilog",
        "-g2012",
        "-Wall",
        "-s",
        "tb_spare_route_vrc",
        "-o",
        str(rtl_exe),
        "rtl/spare_route_vrc.v",
        "rtl/dfx/tpu_rp_rm_spare_route_vrc.v",
        "tests/tb_spare_route_vrc.v",
    ])
    run(["vvp", str(rtl_exe)])
    run([
        "iverilog",
        "-g2012",
        "-Wall",
        "-s",
        "tpu_rp",
        "-o",
        str(rm_exe),
        "rtl/spare_route_vrc.v",
        "rtl/dfx/tpu_rp_rm_spare_route_vrc.v",
    ])

    run([
        args.cc,
        "-std=c99",
        "-Wall",
        "-Wextra",
        "-DSR_HOST_STUB",
        "-I",
        "sw/ehw",
        "-o",
        str(fw_exe),
        "sw/ehw/spare_route_vrc_mbox.c",
    ])
    fw_out = run_capture([str(fw_exe)])
    print(fw_out, end="")
    if "fit=8/8 mask=e8" not in fw_out or "degraded_fit=6/8" not in fw_out:
        print("FAIL: firmware host stub did not match expected no-fault/degraded result", file=sys.stderr)
        return 1
    if "repair gen=19 fit=8/8 mask=e8 uses=2" not in fw_out:
        print("FAIL: firmware host stub did not match expected repaired result", file=sys.stderr)
        return 1

    # Keep the Python/C oracle gate in the EHW-3.2 pre-board path. This catches
    # accidental drift in the GA/fault contract even if RTL protocol tests pass.
    run([sys.executable, "tests/compare_spare_route_twin.py"])

    if not args.skip_ooc:
        run_ooc()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
