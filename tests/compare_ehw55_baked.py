#!/usr/bin/env python3
"""EHW-5.5 gate 1: no-fault baked target RTL + POST firmware host checks.

Three layers, all must pass:
  1. frozen reveal contract (tests/compare_ehw55_reveal_contract.py);
  2. iverilog truth-mask sim of rtl/spare_route_baked.v:
       - MS_SR_MAJORITY + NO_FAULT=1  -> 0xe8 (EHW-5.5 baseline)
       - EHW-5 champion + NO_FAULT=1  -> 0xa0 (post-reveal)
       - EHW-3.3 SRB0 + NO_FAULT=0    -> 0xc8 (regression: fault path unchanged)
  3. sw/ehw/ehw55_reveal_post.c host stub, baseline and champion variants
     (marker/truth/feature-mask/ones asserts inside the firmware).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

BASE = "8'h0a, 8'h0a, 8'h0a, 8'h00, 8'he8, 8'h00, 8'h00, 8'h01, 8'h01, 8'h02, 8'h02, 8'h03, 8'h03, 8'h00, 8'h01, 8'h02"
CHAMP = "8'h08, 8'h00, 8'h0a, 8'h00, 8'he8, 8'h00, 8'h00, 8'h03, 8'h03, 8'h02, 8'h02, 8'h03, 8'h01, 8'h00, 8'h03, 8'h02"
SRB0 = "8'h0a, 8'h08, 8'h01, 8'h0f, 8'h32, 8'h01, 8'h04, 8'h00, 8'h02, 8'h02, 8'h00, 8'h04, 8'h01, 8'h01, 8'h02, 8'h00"

TB = """`timescale 1ns/1ps
module tb_ehw55_baked;
  reg [2:0] idx;
  wire out_base, out_champ, out_srb0;

  spare_route_baked #(.NO_FAULT(1'b1), .%(base0)s) u_base (.idx(idx), .out(out_base));
  spare_route_baked #(.NO_FAULT(1'b1), .%(champ0)s) u_champ (.idx(idx), .out(out_champ));
  spare_route_baked #(.NO_FAULT(1'b0), .%(srb00)s) u_srb0 (.idx(idx), .out(out_srb0));

  integer i, errors;
  reg [7:0] m_base, m_champ, m_srb0;
  initial begin
    errors = 0;
    m_base = 0; m_champ = 0; m_srb0 = 0;
    for (i = 0; i < 8; i = i + 1) begin
      idx = i[2:0];
      #1;
      m_base  = m_base  | (out_base  << i);
      m_champ = m_champ | (out_champ << i);
      m_srb0  = m_srb0  | (out_srb0  << i);
    end
    if (m_base  !== 8'he8) begin $display("FAIL base mask %%02x", m_base); errors = errors + 1; end
    if (m_champ !== 8'ha0) begin $display("FAIL champ mask %%02x", m_champ); errors = errors + 1; end
    if (m_srb0  !== 8'hc8) begin $display("FAIL srb0 mask %%02x", m_srb0); errors = errors + 1; end
    if (errors == 0) $display("tb_ehw55_baked: PASS e8/a0/c8");
    else $fatal;
    $finish;
  end
endmodule
"""


def params(vals: str) -> str:
    names = [f"G{i}" for i in range(16)]
    parts = [v.strip() for v in vals.split(",")]
    return "), .".join(f"{n}({v}" for n, v in zip(names, parts)) + ")"


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True, cwd=ROOT)


def main() -> int:
    if not shutil.which("iverilog") or not shutil.which("vvp"):
        print("FAIL: iverilog/vvp required", file=sys.stderr)
        return 2
    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)

    run([sys.executable, "tests/compare_ehw55_reveal_contract.py"])

    tb = out_dir / "tb_ehw55_baked.v"
    tb.write_text(TB % {"base0": params(BASE), "champ0": params(CHAMP), "srb00": params(SRB0)})
    exe = out_dir / "tb_ehw55_baked.vvp"
    run(["iverilog", "-g2012", "-Wall", "-DSIM", "-s", "tb_ehw55_baked", "-o", str(exe),
         "rtl/spare_route_baked.v", str(tb)])
    run(["vvp", str(exe)])

    cc = os.environ.get("CC", "cc")
    for extra, tag in ([], "baseline"), (["-DEHW55_CHAMP"], "champion"):
        stub = out_dir / f"ehw55_post_{tag}"
        run([cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DSR_HOST_STUB", *extra,
             "-I", "sw/ehw", "-o", str(stub), "sw/ehw/ehw55_reveal_post.c"])
        run([str(stub)])

    print("PASS: EHW-5.5 gate 1 (contract + RTL truth masks + firmware stub, both variants)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
