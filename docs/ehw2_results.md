# EHW-2 Host Prep — Per-Eval On-Chip ICAPE2 Bitstream Evolution

Status: host-prep complete; board run pending.

## Scope

EHW-2 is the stretch version of the ladder: every fitness evaluation is a real
LUT-INIT bitstream edit executed from inside the fabric:

NEORV32 -> `rtl/xbus_icap.v` -> ICAPE2 -> editable LUT -> on-chip fitness.

The PS is deliberately outside the eval loop. It only stages a small candidate
framebank into the shared AXI-Lite framebuf and clears `PCAP_PR` so ICAP owns the
configuration engine.

## Minimal Experiment

The first EHW-2 target is intentionally small:

- Substrate: one `DONT_TOUCH` LUT6 in `rtl/ehw2_lut_target.v`.
- Genome: the low 8 INIT bits, interpreted as a 3-input truth table.
- Fitness: rows matching 3-input majority, target mask `0xe8`, max `8/8`.
- Population: 4 pre-generated candidate frame sequences: `00`, `80`, `a8`, `e8`.
- Per-eval action: firmware streams that candidate's sequence through `xbus_icap`,
  then sweeps rows `0..7` via `0xF4000000` and scores the observed mask.

This is constrained evolution over a pre-staged candidate bank, not arbitrary
runtime synthesis of new frames. The important EHW-2 property is still present:
each candidate fitness measurement is preceded by a real on-chip ICAPE2
configuration write.

## Host Gate

Run:

```sh
python3 tests/compare_ehw2_micro.py
```

Expected final oracle row:

```text
3,e8,e8,8,3,8
```

That means candidate `0xe8` is observed as `0xe8`, scores `8/8`, and wins.

## Framebank Format

`scripts/ehw2-framebank-pack.py` packs up to 4 candidate sequences into the 4KB
framebuf used by `rtl/neorv32_soc_icap.vhd`.

```sh
python3 scripts/ehw2-framebank-pack.py --out runs/ehw2/framebank.bin \
  00:runs/ehw2/seq_00.bin \
  80:runs/ehw2/seq_80.bin \
  a8:runs/ehw2/seq_a8.bin \
  e8:runs/ehw2/seq_e8.bin
```

Layout:

- word 0: magic `0x45485732` (`EHW2`), written last by the loader.
- word 1: candidate count.
- word 2: descriptor base word offset, currently `4`.
- word 3: descriptor words per candidate, currently `4`.
- descriptor: `seq_offset`, `seq_len`, `candidate_init`, reserved.

Each sequence must be at most 255 words because `xbus_icap` stores the burst
length in one byte. The M7.5.1 single-frame envelopes are 233 words, so four
candidates fit in one 1024-word framebuf.

## Board Run

Required board-side steps:

1. Build the `neorv32_soc_icap` static design with:
   - `rtl/xbus_icap.v`
   - `rtl/axil_framebuf.vhd`
   - `rtl/ehw2_lut_target.v`
   - `rtl/neorv32_soc_icap.vhd`
   - EHW-2 firmware `sw/ehw/ehw2_icap_micro.c`
   - build helper `vivado/icap_ehw2/build_ehw2_icap.tcl`

```sh
vivado -mode batch -source vivado/icap_ehw2/build_ehw2_icap.tcl
```

2. Generate four routed bitstreams or edited DCP bitstreams for the same placed
   `ehw2_lut_target.l_ehw2` LUT INIT values `00`, `80`, `a8`, `e8`. The build
   helper writes `vivado/icap_ehw2/build/ehw2_init_<init>.bit` from one routed
   design by changing only the target LUT's INIT property.
3. Use prjxray/RAW-FDRI extraction to create one single-FAR ICAP write sequence
   per candidate. Reuse the M7.5.1 rule: one sync..DESYNC envelope per frame.
4. Pack the sequences with `ehw2-framebank-pack.py`.
5. Load the bitstream with baseline INIT `00`, stage the framebank:

```sh
python3 scripts/ehw2-framebank-load.py runs/ehw2/framebank.bin 0x40000000
```

6. Hand the configuration engine to ICAP:

```text
mw 0xF8007000 0x4400e07f
```

7. Observe mailbox:

- `0xE8000001`: firmware reached main.
- `0xE8100004`: candidate count 4.
- `0xE9iiffoo`: eval candidate `ii`, fitness `ff`, observed mask `oo`.
- `0xEAiiffoo`: selected best candidate.
- `0xEBiiffoo`: steady final result.

PASS condition: best/final mailbox reports candidate index `3`, fitness `8`, and
observed mask `0xe8`, i.e. `0xEA0308E8` then steady `0xEB0308E8`.

After the run, restore PCAP ownership:

```text
mw 0xF8007000 0x4c00e07f
```

## Open Gate

This still needs Vivado/prjxray sequence generation and an EBAZ4205 board run.
The local environment can gate the firmware/oracle/framebank contract, but cannot
prove `xbus_icap` timing or ICAPE2 ownership without hardware.
