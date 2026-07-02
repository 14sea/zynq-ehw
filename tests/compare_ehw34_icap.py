#!/usr/bin/env python3
"""EHW-3.4 host gate: oracle, C firmware stub, RTL target, framebank pack."""

from __future__ import annotations

import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

GENOMES = {
    "base": "0a08010f320104000202000401010200",
    "logic": "0b090903b10104000202000401010200",
    "route": "0a08010f320004040102000001020300",
    "repair": "0b090903b10004040102000001020300",
}


def run(cmd: list[str]) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def run_ooc() -> None:
    vivado = shutil.which("vivado")
    if not vivado:
        print("SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_ehw34_spare_route.tcl")
        return
    run([vivado, "-mode", "batch", "-source", "tests/vivado_ooc_ehw34_spare_route.tcl"])


def main() -> int:
    cc = os.environ.get("CC", "cc")
    out_dir = ROOT / "runs" / "tests" / "ehw34"
    out_dir.mkdir(parents=True, exist_ok=True)
    py_csv = out_dir / "ehw34_python.csv"
    c_csv = out_dir / "ehw34_c.csv"
    exe = out_dir / "ehw34_icap_spare_route"
    rtl_exe = out_dir / "tb_ehw34_spare_route_target.vvp"

    run([sys.executable, "sim/ehw34_icap_oracle.py", "--check-target"])
    run([sys.executable, "sim/ehw34_icap_oracle.py", "--csv", str(py_csv)])
    run([
        cc, "-std=c99", "-Wall", "-Wextra", "-O2", "-DEHW34_HOST_STUB",
        "-I", "sw/ehw", "-o", str(exe), "sw/ehw/ehw34_icap_spare_route.c",
    ])
    run([str(exe), "--check-target"])
    run([str(exe), "--csv", str(c_csv)])

    if py_csv.read_bytes() != c_csv.read_bytes():
        print(f"FAIL: CSV mismatch: {py_csv} != {c_csv}", file=sys.stderr)
        return 1

    run([
        "iverilog", "-g2012", "-Wall", "-DSIM", "-s", "tb_ehw34_spare_route_target",
        "-o", str(rtl_exe),
        "rtl/spare_route_baked.v",
        "rtl/ehw34_spare_route_target.v",
        "tests/tb_ehw34_spare_route_target.v",
    ])
    run(["vvp", str(rtl_exe)])

    seq_specs = [f"base={GENOMES['base']}:-"]
    for idx, label in enumerate(("logic", "route", "repair"), start=1):
        paths = []
        for seq_idx in range(idx):
            path = out_dir / f"fake_{label}_{seq_idx}.seq.bin"
            path.write_bytes(struct.pack(">3I", 0xAA995566, idx, seq_idx))
            paths.append(path)
        seq_specs.append(f"{label}={GENOMES[label]}:" + ",".join(str(p) for p in paths))

    bank = out_dir / "framebank.bin"
    run([sys.executable, "scripts/ehw34-framebank-pack.py", "--out", str(bank), *seq_specs])
    bank_data = bank.read_bytes()
    words = struct.unpack(f">{len(bank_data) // 4}I", bank_data)
    desc_words = 37
    if (
        len(words) != 16384 or
        words[0] != 0x45483334 or words[1] != 4 or words[2] != 4 or words[3] != desc_words or
        words[4] != 0x0A08010F or words[5] != 0x32010400 or words[8] != 0 or
        words[4 + desc_words] != 0x0B090903 or words[4 + desc_words + 4] != 1 or
        words[4 + 2 * desc_words] != 0x0A08010F or words[4 + 2 * desc_words + 4] != 2 or
        words[4 + 3 * desc_words] != 0x0B090903 or words[4 + 3 * desc_words + 4] != 3
    ):
        print("FAIL: packed EHW-3.4 framebank header/descriptor is wrong", file=sys.stderr)
        return 1

    if not any(line == "3,repair,0b090903b10004040102000001020300,e8,8,3,8"
               for line in py_csv.read_text().splitlines()):
        print("FAIL: missing expected repair CSV row", file=sys.stderr)
        return 1

    if "--skip-ooc" not in sys.argv:
        run_ooc()

    print("PASS: EHW-3.4 oracle, C stub, RTL target, and framebank pack agree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
