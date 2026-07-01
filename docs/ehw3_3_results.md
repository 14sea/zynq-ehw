# EHW-3.3 Results — ICAP-Baked Spare-Route Repair

Generated / verified by:

```bash
python3 tests/compare_spare_route_baked.py
```

Status: **BOARD-VERIFIED on the EBAZ4205 (2026-07-01).** Live ICAP LUT-INIT edit of
8 frames rewrote the baked island from broken (`mask=c8`, `7/8`) to repaired
(`mask=e8`, `8/8`) with the marker staying `SRB0` and no PS/NEORV32 reset — exact
mailbox words and the frame-extraction details are in `docs/board_results.md`. This
rung is the EHW-3 analogue of EHW-1.2: the island is no longer register-configured at
runtime; the logic and local path-select fields are baked into real LUT INITs, then a
same-route Vivado checkpoint edit produces the repaired bitstream for frame diff
extraction.

## Deliverables

- `rtl/spare_route_baked.v`: explicit LUT2/LUT3/LUT4/LUT6 primitive island. It
  uses the frozen EHW-3 16-byte genome contract and models the hard fault by
  disabling A1 as an output source.
- `rtl/dfx/tpu_rp_rm_spare_route_baked_base.v`: baseline/broken DFX RM, marker
  `SRB0`, POP=128 no-fault champion genome.
- `rtl/dfx/tpu_rp_rm_spare_route_baked_repair.v`: repaired DFX RM, marker `SRB1`,
  used for host/OOC checks only.
- `sw/ehw/spare_route_baked_post.c`: POST firmware that drives the baked island
  input, reads output, and publishes marker/mask/fitness.
- `tests/tb_spare_route_baked.v`: RTL truth-table sweep for baseline and repaired
  phenotypes.
- `tests/compare_spare_route_baked.py`: host gate tying RTL sim, RM wrapper compile,
  firmware host stub, and exact target INIT-diff checks.
- `tests/vivado_ooc_spare_route_baked.tcl`: Vivado OOC synth gate.
- `vivado/dfx/build_spare_route_baked.tcl`: fresh routed build for the baseline
  broken island.
- `vivado/dfx/spare_route_baked_edit_repair.tcl`: edits the routed baseline DCP
  into the repaired same-route bitstream by changing only target INITs.

## Baked Genomes

Baseline/broken genome, under hard `DISABLE_NODE(A1)`:

```text
0a 08 01 0f 32 01 04 00 02 02 00 04 01 01 02 00
mask = c8, fitness = 7/8
```

Repaired genome, under the same hard fault:

```text
0b 09 09 03 b1 00 04 04 01 02 00 00 01 02 03 00
mask = e8, fitness = 8/8
```

The hard fault is modeled in the island as `A1 -> 0` at the output-select source
boundary. The repair uses the spare node/path inside the fixed-route island; no
vendor routing bits are evolved.

## INIT Diff Contract

The host gate computes the primitive INITs that Vivado should see for each genome
field and verifies the exact changed set:

```text
g0:  4'ha -> 4'hb
g1:  4'h8 -> 4'h9
g2:  4'h1 -> 4'h9
g3:  4'hf -> 4'h3
g4:  8'h32 -> 8'hb1
g5:  64'hcccccccccccccccc -> 64'haaaaaaaaaaaaaaaa
g7:  64'haaaaaaaaaaaaaaaa -> 64'hffff0000ffff0000
g8:  64'hf0f0f0f0f0f0f0f0 -> 64'hcccccccccccccccc
g11: 64'hffff0000ffff0000 -> 64'haaaaaaaaaaaaaaaa
g13: 16'hcccc -> 16'hf0f0
g14: 16'hf0f0 -> 16'hff00
```

No other baked primitive INIT is expected to change. The repaired full wrapper has
marker `SRB1` for host/OOC sanity checks, but the **same-route ICAP edit keeps the
live marker at `SRB0`**, because `spare_route_baked_edit_repair.tcl` edits only the
listed LUT/select INIT properties.

## Host Gate Output

Expected local output:

```text
PASS: only intended baked LUT/select INITs differ
PASS: spare_route_baked marker=53524230 mask=c8 fitness=7/8
PASS: spare_route_baked marker=53524231 mask=e8 fitness=8/8
SR_BAKED marker=0x53524230 mask=0xc8 fitness=7/8
SR_BAKED marker=0x53524231 mask=0xe8 fitness=8/8
```

Vivado is optional in `tests/compare_spare_route_baked.py`; when it is unavailable,
the script reports that the OOC gate can be run via
`tests/vivado_ooc_spare_route_baked.tcl`.

## Board Flow

Build the POST firmware into IMEM before routing:

```bash
cp sw/ehw/Makefile sw/ehw/spare_route_kernel.h sw/ehw/spare_route_baked_post.c sw_src/sr_build/
cd sw_src/sr_build
make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
  RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
  APP_SRC=spare_route_baked_post.c clean install
```

Local link result:

```text
text=580 data=0 bss=0 dec=580 hex=244
```

Then generate a fresh routed baseline and same-route repaired bitstream:

```bash
vivado -mode batch -source vivado/dfx/build_spare_route_baked.tcl
vivado -mode batch -source vivado/dfx/spare_route_baked_edit_repair.tcl
```

The frame extraction rule is strict: run `bitread`/diff and
`scripts/m75-build-frameseqs.py` against these exact fresh bitstreams. Target LUT
sites can move between builds. If the changed INIT bits span multiple FARs, the
framebank must contain one envelope per FAR, matching the EHW-2/EHW-1.2 lesson.

Board acceptance:

- baseline loaded island publishes `E331` marker low24 for `SRB0`, `E33200c8`,
  `E3330007`;
- ICAP repair applies only the generated framebank envelopes, with no PS/NEORV32
  reset;
- repaired live island publishes marker still `SRB0`, then `E33200e8`,
  `E3330008`.

Those mailbox words have been captured on the EBAZ4205; the hardware evidence is
recorded in `docs/board_results.md`.
