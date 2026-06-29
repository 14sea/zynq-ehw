# EHW-1.1-fabric Results — CGP VRC

Generated / verified by:

```bash
python3 tests/compare_cgp_vrc.py
```

## Substrate

- `rtl/cgp_vrc.v`: register-configured `3 x 4` LUT4 CGP grid.
- `rtl/dfx/tpu_rp_rm_cgp_vrc.v`: DFX `tpu_rp` wrapper mapping the existing
  NEORV32 `0xF0000000` XBUS window to the CGP VRC.
- Genome: `12` 16-bit LUT INIT registers, same ordering as `sim/oracle_cgp.py`
  and `sw/ehw/cgp_kernel.h`.
- Routing: fixed column-to-column, contention-safe; only LUT INIT registers are
  mutable.

Register map:

| Offset | Name | Access | Meaning |
|---:|---|---|---|
| `0x000` | `CTRL` | W | bit4 clears genome/input registers |
| `0x004` | `STATUS` | R | bit0 ready/done, always `1` |
| `0x008` | `INPUT` | RW | `[3:0] = {b1,b0,a1,a0}` truth-table index bits |
| `0x00c` | `OUTPUT` | R | `[3:0] = {p3,p2,p1,p0}` |
| `0x010` | `FITNESS` | R | 0..64 golden bit matches |
| `0x014` | `ROWS` | R | 0..16 fully-correct rows |
| `0x018` | `ACTIVE` | R | non-0/non-FFFF LUT count |
| `0x040+i*4` | `INITi` | RW | low16 LUT INIT word, `i=0..11` |

## Host Result

- Verilog testbench loads the EHW-1.0 golden genome through the XBUS wrapper,
  reads back all 12 INIT registers, checks `FITNESS=64`, `ROWS=16`,
  `ACTIVE=12`, and verifies all 16 truth-table rows through `INPUT`/`OUTPUT`.
- Firmware host stub `sw/ehw/cgp_vrc_mbox.c` runs the board-resident GA path
  against the same register protocol and converges to the host champion:

```text
aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0 4c00 8000
```

Host gate passed; board result below confirms the same champion on real fabric.

## Board Result

Status: **PASS — HW-VERIFIED on EBAZ4205 (2026-06-29)**.

- Build: static + `rm_cgp_vrc` via `vivado/dfx/build_cgp_vrc.tcl` (`cfg10`/`impl_10`).
- Firmware: `sw/ehw/cgp_vrc_mbox.c`.
- Fitness path: NEORV32 writes all 12 INIT registers, drives the 16 truth-table inputs,
  and reads `OUTPUT` from the fabric CGP VRC over MMIO.
- Observed mailbox:
  - `0xdc000010`: DONE, `16/16` truth-table rows.
  - `0xda000040`: `64/64` fitness bits.
  - `0xA0..0xAB`: champion genome
    `aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0 4c00 8000`.

Vivado synth gotcha: no-input Verilog functions passed `iverilog` but failed Vivado
`[Synth 8-10738]`; `rtl/cgp_vrc.v` now gives those functions a dummy input. Future
RTL gates should add a quick OOC `synth_design` check before full DFX build.
