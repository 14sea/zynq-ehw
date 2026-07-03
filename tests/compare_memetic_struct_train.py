#!/usr/bin/env python3
"""EHW-5.2 host gate: combined spare-route VRC + memetic train-unit RM."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "sim"))

import oracle_evolve as evo  # noqa: E402
import oracle_memetic as mem  # noqa: E402
import oracle_spare_routing as sr  # noqa: E402

SR_FEATURE = [0x08, 0x00, 0x0A, 0x00, 0xE8, 0, 0, 3, 3, 2, 2, 3, 1, 0, 3, 2]


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def xhex(v: int) -> str:
    return f"32'h{v & 0xFFFFFFFF:08x}"


def flatten_w1(w1: list[list[int]]) -> list[int]:
    return [w1[i][j] for i in range(evo.NH) for j in range(evo.NIN)]


def flatten_w2(w2: list[list[int]]) -> list[int]:
    return [w2[i][j] for i in range(evo.NOUT) for j in range(evo.NH)]


def transformed_x_and_row(x: list[int]) -> tuple[int, int, list[int]]:
    row = int(x[0] >= 8) | (int(x[1] >= 8) << 1) | (int(x[2] >= 8) << 2)
    phi = sr.eval_row(SR_FEATURE, row)
    x3 = x[3] + (8 if phi else -8)
    x3 = -128 if x3 < -128 else 127 if x3 > 127 else x3
    return row, phi, [x[0], x[1], x[2], x3]


def traced_struct_epoch() -> tuple[list[int], list[int], list[dict[str, object]]]:
    w1, w2 = mem.master_from_genome(evo.M753_TRAINED_GENOME)
    init_w1 = flatten_w1(w1)
    init_w2 = flatten_w2(w2)
    rng = evo.XorShift32(3)
    order = list(range(evo.NTEST))
    for i in range(len(order) - 1, 0, -1):
        j = rng.randrange(i + 1)
        order[i], order[j] = order[j], order[i]

    trace: list[dict[str, object]] = []
    sse = 0
    for idx in order:
        row, phi, x = transformed_x_and_row(evo.M753_TEST_X[idx])
        label = evo.M753_TEST_Y[idx]
        _, z1, h, z2, y = mem.forward_master(w1, w2, x)
        err = [mem.clamp(y[k] - (mem.ONE if k == label else 0),
                         -mem.ERR_CLAMP, mem.ERR_CLAMP - 1)
               for k in range(evo.NOUT)]
        d2 = [mem.clamp(mem.qmul(err[k], mem.leaky_d(z2[k])),
                        -mem.DELTA_CLAMP, mem.DELTA_CLAMP - 1)
              for k in range(evo.NOUT)]
        for e in err:
            sse += mem.qmul(e, e)
        dw2 = [[mem.qmul(d2[i], h[j]) for j in range(evo.NH)] for i in range(evo.NOUT)]
        d2_i8 = [evo.q8(v, mem.DSHIFT) for v in d2]
        w2_i8 = [[evo.q8(v, mem.WSHIFT) for v in row_w] for row_w in w2]
        w2td2 = []
        for j in range(evo.NH):
            acc = sum(w2_i8[i][j] * d2_i8[i] for i in range(evo.NOUT))
            w2td2.append(mem.sat16((acc + (1 << 3)) >> 4))
        d1 = [mem.clamp(mem.qmul(w2td2[i], mem.leaky_d(z1[i])),
                        -mem.DELTA_CLAMP, mem.DELTA_CLAMP - 1)
              for i in range(evo.NH)]
        x_q88 = [int(v) << mem.XSHIFT for v in x]
        dw1 = [[mem.qmul(d1[i], x_q88[j]) for j in range(evo.NIN)] for i in range(evo.NH)]

        for i in range(evo.NOUT):
            for j in range(evo.NH):
                w2[i][j] = mem.sat16(w2[i][j] - (dw2[i][j] >> mem.LR_SHIFT))
        for i in range(evo.NH):
            for j in range(evo.NIN):
                w1[i][j] = mem.sat16(w1[i][j] - (dw1[i][j] >> mem.LR_SHIFT))
        trace.append({
            "row": row, "phi": phi, "y": y, "z2": z2,
            "target": [mem.ONE if i == label else 0 for i in range(evo.NOUT)],
            "d2": d2, "loss": sse, "w2td2": w2td2, "z1": z1, "d1": d1,
            "dw2": [dw2[i][j] for i in range(evo.NOUT) for j in range(evo.NH)],
            "dw1": [dw1[i][j] for i in range(evo.NH) for j in range(evo.NIN)],
            "w1": flatten_w1(w1), "w2": flatten_w2(w2),
        })
    return init_w1, init_w2, trace


def emit_tb(path: Path) -> None:
    init_w1, init_w2, trace = traced_struct_epoch()
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
        "  task wait_idle;",
        "    reg [31:0] busy;",
        "    begin busy = 32'h1; while (busy[0]) xrd(32'hF0000934, busy); end",
        "  endtask",
        "  initial begin",
        "    rst_n = 0; repeat (4) @(posedge clk); rst_n = 1; @(posedge clk);",
        "    chk(\"sr_marker\", 32'hF0000420, 32'h53525630);",
    ]
    for i, v in enumerate(SR_FEATURE):
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
        "    // Full EHW-5.2 epoch replay: VRC feature path + lite train unit.",
        "    xwr(32'hF0000870, 32'h00000010);",
    ])
    for i, v in enumerate(init_w1):
        lines.append(f"    xwr(32'hF000{0x880 + 4 * i:04x}, {xhex(v)});")
    for i, v in enumerate(init_w2):
        lines.append(f"    xwr(32'hF000{0x8c0 + 4 * i:04x}, {xhex(v)});")
    for s, rec in enumerate(trace):
        lines.append(f"    xwr(32'hF0000408, 32'h{int(rec['row']):08x});")
        lines.append(f"    chk(\"s{s}_phi\", 32'hF000040c, 32'h{int(rec['phi']):08x});")
        y = rec["y"]; z2 = rec["z2"]; target = rec["target"]
        assert isinstance(y, list) and isinstance(z2, list) and isinstance(target, list)
        for i in range(evo.NOUT):
            lines.append(f"    xwr(32'hF000{0x800 + 4 * i:04x}, {xhex(y[i])});")
            lines.append(f"    xwr(32'hF000{0x810 + 4 * i:04x}, {xhex(z2[i])});")
            lines.append(f"    xwr(32'hF000{0x820 + 4 * i:04x}, {xhex(target[i])});")
        lines.append("    xwr(32'hF0000870, 32'h00000001);")
        d2 = rec["d2"]
        assert isinstance(d2, list)
        for i, v in enumerate(d2):
            lines.append(f"    chk(\"s{s}_d2_{i}\", 32'hF000{0x900 + 4 * i:04x}, {xhex(v)});")
        lines.append(f"    chk(\"s{s}_loss\", 32'hF0000930, {xhex(int(rec['loss']))});")

        w2td2 = rec["w2td2"]; z1 = rec["z1"]
        assert isinstance(w2td2, list) and isinstance(z1, list)
        for i in range(evo.NH):
            lines.append(f"    xwr(32'hF000{0x800 + 4 * i:04x}, {xhex(w2td2[i])});")
            lines.append(f"    xwr(32'hF000{0x810 + 4 * i:04x}, {xhex(z1[i])});")
        lines.append("    xwr(32'hF0000870, 32'h00000002);")
        d1 = rec["d1"]
        assert isinstance(d1, list)
        for i, v in enumerate(d1):
            lines.append(f"    chk(\"s{s}_d1_{i}\", 32'hF000{0x910 + 4 * i:04x}, {xhex(v)});")

        dw2 = rec["dw2"]
        assert isinstance(dw2, list)
        for i, v in enumerate(dw2):
            lines.append(f"    xwr(32'hF000{0x830 + 4 * i:04x}, {xhex(v)});")
        lines.append("    xwr(32'hF0000870, 32'h00000004);")
        lines.append("    wait_idle();")

        dw1 = rec["dw1"]
        assert isinstance(dw1, list)
        for i, v in enumerate(dw1):
            lines.append(f"    xwr(32'hF000{0x830 + 4 * i:04x}, {xhex(v)});")
        lines.append("    xwr(32'hF0000870, 32'h00000008);")
        lines.append("    wait_idle();")

        w1 = rec["w1"]; w2 = rec["w2"]
        assert isinstance(w1, list) and isinstance(w2, list)
        for i, v in enumerate(w1):
            lines.append(f"    chk(\"s{s}_w1_{i}\", 32'hF000{0x880 + 4 * i:04x}, {xhex(v)});")
        for i, v in enumerate(w2):
            lines.append(f"    chk(\"s{s}_w2_{i}\", 32'hF000{0x8c0 + 4 * i:04x}, {xhex(v)});")
    lines.extend([
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
