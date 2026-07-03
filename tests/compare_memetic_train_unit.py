#!/usr/bin/env python3
"""EHW-4.2 host gate: memetic train-unit RTL + firmware stub."""

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


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def vlit(v: int) -> str:
    return f"-32'sd{-v}" if v < 0 else f"32'sd{v}"


def flatten_w1(w1: list[list[int]]) -> list[int]:
    return [w1[i][j] for i in range(evo.NH) for j in range(evo.NIN)]


def flatten_w2(w2: list[list[int]]) -> list[int]:
    return [w2[i][j] for i in range(evo.NOUT) for j in range(evo.NH)]


def traced_epoch() -> tuple[list[int], list[int], list[dict[str, object]]]:
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
        x = evo.M753_TEST_X[idx]
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
        w2_i8 = [[evo.q8(v, mem.WSHIFT) for v in row] for row in w2]
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
            "y": y,
            "z2": z2,
            "target": [mem.ONE if i == label else 0 for i in range(evo.NOUT)],
            "d2": d2,
            "loss": sse,
            "w2td2": w2td2,
            "z1": z1,
            "d1": d1,
            "dw2": [dw2[i][j] for i in range(evo.NOUT) for j in range(evo.NH)],
            "dw1": [dw1[i][j] for i in range(evo.NH) for j in range(evo.NIN)],
            "w1": flatten_w1(w1),
            "w2": flatten_w2(w2),
        })
    return init_w1, init_w2, trace


def emit_tb(path: Path) -> None:
    init_w1, init_w2, trace = traced_epoch()
    lines = [
        "`timescale 1ns/1ps",
        "module tb_memetic_train_unit;",
        "  reg clk = 0, rst_n = 0, we = 0;",
        "  reg [6:0] addr = 0;",
        "  reg signed [31:0] wdata = 0;",
        "  wire signed [31:0] rdata;",
        "  integer errors = 0;",
        "  memetic_train_unit DUT(.clk(clk), .rst_n(rst_n), .lr(6'd7), .k(6'd2),",
        "                          .we(we), .addr(addr), .wdata(wdata), .rdata(rdata));",
        "  always #5 clk = ~clk;",
        "  task wr(input [6:0] a, input signed [31:0] d);",
        "    begin @(negedge clk); addr = a; wdata = d; we = 1; @(posedge clk); #1 we = 0; end",
        "  endtask",
        "  task rdchk(input [255:0] tag, input [6:0] a, input signed [31:0] exp);",
        "    begin addr = a; #1; if (rdata !== exp) begin",
        "      errors = errors + 1; $display(\"MISMATCH %0s addr=%0d got=%0d exp=%0d\", tag, a, rdata, exp);",
        "    end end",
        "  endtask",
        "  initial begin",
        "    rst_n = 0; repeat (3) @(posedge clk); rst_n = 1; @(posedge clk);",
    ]
    for i, v in enumerate(init_w1):
        lines.append(f"    wr(7'd{32 + i}, {vlit(v)});")
    for i, v in enumerate(init_w2):
        lines.append(f"    wr(7'd{48 + i}, {vlit(v)});")
    lines.append("    wr(7'd28, 32'sd16);")
    for s, rec in enumerate(trace):
        y = rec["y"]
        z2 = rec["z2"]
        target = rec["target"]
        assert isinstance(y, list) and isinstance(z2, list) and isinstance(target, list)
        for i in range(evo.NOUT):
            lines.append(f"    wr(7'd{0 + i}, {vlit(y[i])});")
            lines.append(f"    wr(7'd{4 + i}, {vlit(z2[i])});")
            lines.append(f"    wr(7'd{8 + i}, {vlit(target[i])});")
        lines.append("    wr(7'd28, 32'sd1);")
        d2 = rec["d2"]
        assert isinstance(d2, list)
        for i, v in enumerate(d2):
            lines.append(f"    rdchk(\"s{s}_d2_{i}\", 7'd{64 + i}, {vlit(v)});")
        lines.append(f"    rdchk(\"s{s}_loss\", 7'd76, {vlit(int(rec['loss']))});")

        w2td2 = rec["w2td2"]
        z1 = rec["z1"]
        assert isinstance(w2td2, list) and isinstance(z1, list)
        for i in range(evo.NH):
            lines.append(f"    wr(7'd{0 + i}, {vlit(w2td2[i])});")
            lines.append(f"    wr(7'd{4 + i}, {vlit(z1[i])});")
        lines.append("    wr(7'd28, 32'sd2);")
        d1 = rec["d1"]
        assert isinstance(d1, list)
        for i, v in enumerate(d1):
            lines.append(f"    rdchk(\"s{s}_d1_{i}\", 7'd{68 + i}, {vlit(v)});")

        dw2 = rec["dw2"]
        assert isinstance(dw2, list)
        for i, v in enumerate(dw2):
            lines.append(f"    wr(7'd{12 + i}, {vlit(v)});")
        lines.append("    wr(7'd28, 32'sd4);")
        dw1 = rec["dw1"]
        assert isinstance(dw1, list)
        for i, v in enumerate(dw1):
            lines.append(f"    wr(7'd{12 + i}, {vlit(v)});")
        lines.append("    wr(7'd28, 32'sd8);")
        w1 = rec["w1"]
        w2 = rec["w2"]
        assert isinstance(w1, list) and isinstance(w2, list)
        for i, v in enumerate(w1):
            lines.append(f"    rdchk(\"s{s}_w1_{i}\", 7'd{32 + i}, {vlit(v)});")
        for i, v in enumerate(w2):
            lines.append(f"    rdchk(\"s{s}_w2_{i}\", 7'd{48 + i}, {vlit(v)});")
    lines.extend([
        "    if (errors == 0) $display(\"tb_memetic_train_unit: PASS\");",
        "    else $display(\"tb_memetic_train_unit: FAIL %0d\", errors);",
        "    $finish;",
        "  end",
        "endmodule",
        "",
    ])
    path.write_text("\n".join(lines))


def run_ooc() -> None:
    vivado = shutil.which("vivado")
    if not vivado:
        print("SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_memetic_train.tcl")
        return
    run([vivado, "-mode", "batch", "-source", "tests/vivado_ooc_memetic_train.tcl"])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skip-ooc", action="store_true", help="skip Vivado OOC synth even if vivado exists")
    args = ap.parse_args()

    if not shutil.which("iverilog") or not shutil.which("vvp"):
        print("FAIL: iverilog and vvp are required for the memetic train-unit RTL gate", file=sys.stderr)
        return 2

    out_dir = ROOT / "runs" / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)
    tb = out_dir / "tb_memetic_train_unit.v"
    rtl_exe = out_dir / "tb_memetic_train_unit.vvp"
    wrapper_exe = out_dir / "tpu_rp_rm_memetic_train.vvp"
    fw_exe = out_dir / "memetic_train_mbox"

    emit_tb(tb)
    run(["iverilog", "-g2012", "-Wall", "-s", "tb_memetic_train_unit",
         "-o", str(rtl_exe), "rtl/memetic_train_unit.v", str(tb)])
    run(["vvp", str(rtl_exe)])
    run(["iverilog", "-g2012", "-Wall", "-s", "tpu_rp", "-o", str(wrapper_exe),
         "rtl/pe.v", "rtl/systolic_array_4x4.v", "rtl/tpu_accel.v",
         "rtl/wb_tpu_accel.v", "rtl/memetic_train_unit.v",
         "rtl/dfx/tpu_rp_rm_memetic_train.v"])
    run(["cc", "-std=c99", "-Wall", "-Wextra", "-O2", "-DMEMETIC_TRAIN_HOST_STUB",
         "-I", "sw/ehw", "-o", str(fw_exe), "sw/ehw/memetic_train_mbox.c"])
    run([str(fw_exe)])
    if not args.skip_ooc:
        run_ooc()
    print("PASS: EHW-4.2 memetic train-unit RTL and firmware host stub")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
