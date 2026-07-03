#!/usr/bin/env python3
"""EHW-5.2 host gate: combined spare-route VRC + memetic train-unit RM."""

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


def emit_tb(path: Path) -> None:
    lines = [
        "`timescale 1ns/1ps",
        "module tb_memetic_struct_rm;",
        "  reg clk = 0, rst_n = 0;",
        "  reg [31:0] adr = 0, dat_w = 0;",
        "  reg [3:0] sel = 4'hf;",
        "  reg we = 0, stb = 0, cyc = 0;",
        "  wire [31:0] dat_r;",
        "  wire ack, err;",
        "  wire [3:0] leds;",
        "  integer errors = 0;",
        "  tpu_rp DUT(.clk(clk), .rst_n(rst_n), .xbus_adr(adr), .xbus_dat_w(dat_w),",
        "             .xbus_sel(sel), .xbus_we(we), .xbus_stb(stb), .xbus_cyc(cyc),",
        "             .xbus_dat_r(dat_r), .xbus_ack(ack), .xbus_err(err), .dbg_leds(leds));",
        "  always #5 clk = ~clk;",
        "  task xwr(input [31:0] a, input [31:0] d);",
        "    begin @(negedge clk); adr=a; dat_w=d; we=1; stb=1; cyc=1; sel=4'hf;",
        "      while (!ack) @(posedge clk); @(negedge clk); we=0; stb=0; cyc=0; adr=0; dat_w=0;",
        "    end endtask",
        "  task xrd(input [31:0] a, output [31:0] d);",
        "    begin @(negedge clk); adr=a; we=0; stb=1; cyc=1; sel=4'hf;",
        "      while (!ack) @(posedge clk); #1 d=dat_r; @(negedge clk); stb=0; cyc=0; adr=0;",
        "    end endtask",
        "  task chk(input [255:0] tag, input [31:0] a, input [31:0] exp);",
        "    reg [31:0] got;",
        "    begin xrd(a, got); if (got !== exp) begin errors=errors+1;",
        "      $display(\"MISMATCH %0s got=%08x exp=%08x\", tag, got, exp); end",
        "    end endtask",
        "  initial begin",
        "    rst_n = 0; repeat (4) @(posedge clk); rst_n = 1; @(posedge clk);",
        "    chk(\"sr_marker\", 32'hF0000420, 32'h53525630);",
    ]
    genome = [0x08, 0x00, 0x0A, 0x00, 0xE8, 0, 0, 3, 3, 2, 2, 3, 1, 0, 3, 2]
    for i, v in enumerate(genome):
        lines.append(f"    xwr(32'hF000{0x440 + 4 * i:04x}, 32'h{v:08x});")
    lines.extend([
        "    chk(\"sr_mask\", 32'hF0000410, 32'h000000a0);",
        "    xwr(32'hF0000408, 32'h00000007);",
        "    chk(\"sr_out_7\", 32'hF000040c, 32'h00000001);",
        "    xwr(32'hF0000408, 32'h00000000);",
        "    chk(\"sr_out_0\", 32'hF000040c, 32'h00000000);",
        "    xwr(32'hF0000880, 32'h0000000c);",
        "    chk(\"tu_w1_0\", 32'hF0000880, 32'h0000000c);",
        "    xwr(32'hF00008c0, 32'h00000014);",
        "    chk(\"tu_w2_0\", 32'hF00008c0, 32'h00000014);",
        "    xwr(32'hF0000830, 32'h00000100);",
        "    xwr(32'hF0000870, 32'h00000004);",
        "    chk(\"tu_busy_start\", 32'hF0000934, 32'h00000001);",
        "    repeat (10) @(posedge clk);",
        "    chk(\"tu_busy_done\", 32'hF0000934, 32'h00000000);",
        "    chk(\"tu_w2_0_upd\", 32'hF00008c0, 32'h00000012);",
        "    if (err !== 1'b0) begin errors=errors+1; $display(\"MISMATCH xbus_err\"); end",
        "    if (errors == 0) $display(\"tb_memetic_struct_rm: PASS\");",
        "    else begin $display(\"tb_memetic_struct_rm: FAIL %0d\", errors); $fatal; end",
        "    $finish;",
        "  end",
        "endmodule",
        "",
    ])
    path.write_text("\n".join(lines))


def run_ooc() -> None:
    vivado = shutil.which("vivado")
    if not vivado:
        print("SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_memetic_struct.tcl")
        return
    run([vivado, "-mode", "batch", "-source", "tests/vivado_ooc_memetic_struct.tcl"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-ooc", action="store_true", help="skip Vivado OOC synth even if vivado exists")
    args = ap.parse_args()

    if not shutil.which("iverilog") or not shutil.which("vvp"):
        print("FAIL: iverilog and vvp are required for the EHW-5.2 RTL gate", file=sys.stderr)
        return 2

    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    tb = out_dir / "tb_memetic_struct_rm.v"
    rtl_exe = out_dir / "tb_memetic_struct_rm.vvp"
    fw_exe = out_dir / "memetic_struct_train_mbox"

    emit_tb(tb)
    run([
        "iverilog", "-g2012", "-Wall", "-s", "tb_memetic_struct_rm",
        "-o", str(rtl_exe),
        "rtl/pe.v", "rtl/systolic_array_4x4.v", "rtl/tpu_accel.v",
        "rtl/wb_tpu_accel.v", "rtl/spare_route_vrc.v", "rtl/memetic_train_unit_lite.v",
        "rtl/dfx/tpu_rp_rm_memetic_struct.v", str(tb),
    ])
    run(["vvp", str(rtl_exe)])
    run([
        "cc", "-std=c99", "-Wall", "-Wextra", "-O2",
        "-DMEMETIC_STRUCT_TRAIN_HOST_STUB", "-I", "sw/ehw",
        "-o", str(fw_exe), "sw/ehw/memetic_struct_train_mbox.c",
    ])
    run([str(fw_exe)])
    if not args.skip_ooc:
        run_ooc()
    print("PASS: EHW-5.2 combined VRC + train-unit host prep")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
