# EHW-1.2 HW-VERIFIED — ICAP-Baked CGP Multiplier

Host gate:

```bash
python3 tests/compare_cgp_baked.py
```

## Goal

Bake the EHW-1.1-fabric evolved CGP multiplier into hardwired LUT4 INITs, then use
ICAP frame writes to flip a routed baseline RM into the champion without resetting
PS/NEORV32. This mirrors EHW-0.5, but the payload is a logic-circuit genome rather
than INT8 NN weights.

## Baseline And Champion

Baseline baked genome:

```text
aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 0000 0000 0000 0000
```

Champion baked genome:

```text
aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0 4c00 8000
```

The first two columns are identical pass-through LUTs. The ICAP reveal only needs
to change output LUTs `n8..n11`.

| Image | Marker | Rows | Fitness |
|---|---:|---:|---:|
| baseline | `0x43475030` (`CGP0`) | `7/16` | `50/64` |
| champion | `0x43475031` (`CGP1`) | `16/16` | `64/64` |

## Deliverables

- `rtl/cgp_baked.v`: explicit LUT4-primitive CGP grid with compile-time INITs.
- `rtl/dfx/tpu_rp_rm_cgp_baked_base.v`: baseline DFX RM.
- `rtl/dfx/tpu_rp_rm_cgp_baked_champ.v`: champion DFX RM for host/OOC checks.
- `sw/ehw/cgp_baked_post.c`: POST firmware that drives `INPUT`, reads `OUTPUT`,
  computes rows/fitness in firmware, and publishes mailbox words.
- `tests/compare_cgp_baked.py`: RTL sim + RM syntax + firmware host-stub gate;
  runs `tests/vivado_ooc_cgp_baked.tcl` automatically when `vivado` is on PATH.
- `vivado/dfx/build_cgp_baked.tcl`: builds static + baseline baked-CGP RM.
- `vivado/dfx/cgp_baked_edit_champ.tcl`: edits the routed baseline DCP in-place,
  setting only `n8..n11` LUT4 INITs to the champion.

## Board Flow

Board-side sequence used for the verified run:

```bash
vivado -mode batch -source vivado/dfx/build_cgp_baked.tcl
vivado -mode batch -source vivado/dfx/cgp_baked_edit_champ.tcl

mkdir -p vivado/dfx/cgp_icap
bitread -y vivado/dfx/build/dfx.runs/impl_11/dfx_top.bit \
  | sort > vivado/dfx/cgp_icap/cgp_base.bits
bitread -y vivado/dfx/cgp_icap/dfx_top_cgp_champ.bit \
  | sort > vivado/dfx/cgp_icap/cgp_champ.bits
comm -13 vivado/dfx/cgp_icap/cgp_base.bits vivado/dfx/cgp_icap/cgp_champ.bits \
  > vivado/dfx/cgp_icap/cgp_setbits.txt
comm -23 vivado/dfx/cgp_icap/cgp_base.bits vivado/dfx/cgp_icap/cgp_champ.bits \
  > vivado/dfx/cgp_icap/cgp_clrbits.txt
python3 scripts/m75-build-frameseqs.py \
  vivado/dfx/build/dfx.runs/impl_11/dfx_top.bit \
  vivado/dfx/cgp_icap/dfx_top_cgp_champ.bit \
  vivado/dfx/cgp_icap/cgp_setbits.txt \
  vivado/dfx/cgp_icap/cgp_clrbits.txt \
  vivado/dfx/cgp_icap/frames
```

Then load the baseline full bitstream + `cgp_baked_post.c`, observe baseline
mailbox (`7/16`, `50/64`, marker `CGP0`), disable `PCAP_PR`, stream the generated
frame sequences uninterrupted through `hwicap-uart.py writeseq`, and observe the
same firmware report champion (`16/16`, `64/64`). Re-attest `IDCODE=0x13722093`
and restore `PCAP_PR`.

## Board Result

Status: **PASS — HW-VERIFIED on EBAZ4205 (2026-06-29)**.

- Baseline mailbox: rows `0xe3000007`, fitness `0xe4000032`, marker `0xe5475030`
  (`CGP0`).
- After ICAP rewrite of the four output LUTs `n8..n11`: rows `0xe3000010`,
  fitness `0xe4000040`, marker stayed `0xe5475030`.
- Interpretation: the live routed baseline CGP circuit changed from a broken
  `7/16` multiplier to the champion `16/16` multiplier without resetting PS or
  NEORV32. See `docs/board_results.md` for the full log and the frame anchoring
  bug/fix.
