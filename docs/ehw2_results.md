# EHW-2 HW-VERIFIED — Per-Eval On-Chip ICAPE2 Bitstream Evolution

Status: **PASS — HW-VERIFIED on EBAZ4205 (2026-06-30)**.

## Scope

EHW-2 is the stretch version of the ladder: every fitness evaluation is a real
LUT-INIT bitstream edit executed from inside the fabric:

NEORV32 -> `rtl/xbus_icap.v` -> ICAPE2 -> editable LUT -> on-chip fitness.

The PS is deliberately outside the eval loop. It only stages a small candidate
framebank into the shared AXI-Lite framebuf and clears `PCAP_PR` so ICAP owns the
configuration engine.

## Minimal Experiment

The EHW-2 target is intentionally small:

- Substrate: one `DONT_TOUCH` LUT6 in `rtl/ehw2_lut_target.v`.
- Genome: the low 8 INIT bits, interpreted as a 3-input truth table.
- Fitness: rows matching 3-input majority, target mask `0xe8`, max `8/8`.
- Population: 4 pre-generated candidate INITs: `00`, `80`, `a8`, `e8`.
- Per-eval action: firmware streams all single-FAR frame sequences for that
  candidate through `xbus_icap`, then sweeps rows `0..7` via `0xF4000000` and
  scores the observed mask.

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

`scripts/ehw2-build-framebank-from-bits.py` builds the candidate framebank from
the same-route bitstreams and their prjxray `bitread -y` outputs:

```sh
python3 scripts/ehw2-build-framebank-from-bits.py --out-dir runs/ehw2_seqs \
  --bit-template 'vivado/icap_ehw2/build/ehw2_init_{init}.bit' \
  --bits-template 'runs/ehw2_bitread/init_{init}.bits' \
  00 80 a8 e8
```

The `.bits` files must be generated from the exact `.bit` files in
`--bit-template`; stale bitread output from a previous build can self-check the
wrong FDRI positions and fail frame anchoring.

Internally, `scripts/ehw2-framebank-pack.py` packs up to 4 candidate INITs into
the 8KB framebuf used by `rtl/neorv32_soc_icap.vhd`. Each candidate can contain
0, 1, or 2 single-FAR ICAP sequences; the low 8 INIT bits of this placed LUT span
two FARs on the current build.

```sh
python3 scripts/ehw2-framebank-pack.py --out runs/ehw2/framebank.bin \
  00:- \
  80:runs/ehw2/seq_80_d23.bin \
  a8:runs/ehw2/seq_a8_d22.bin,runs/ehw2/seq_a8_d23.bin \
  e8:runs/ehw2/seq_e8_d22.bin,runs/ehw2/seq_e8_d23.bin
```

Layout:

- word 0: magic `0x45485732` (`EHW2`), written last by the loader.
- word 1: candidate count.
- word 2: descriptor base word offset, currently `4`.
- word 3: descriptor words per candidate, currently `6`.
- descriptor: `candidate_init`, `nseq`, `seq0_offset`, `seq0_len`,
  `seq1_offset`, `seq1_len`.

Each sequence must be at most 255 words because `xbus_icap` stores the burst
length in one byte. The M7.5.1 single-frame envelopes are 233 words, so up to
eight sequences fit in the 2048-word framebuf.

## First Board Result And Diagnosis

First EBAZ4205 EHW-2 run:

- Mechanism: PASS. NEORV32 reached main, read the staged framebank, executed the
  per-eval `xbus_icap` loop, emitted steady result, and recovered without wedge.
- Fidelity: FAIL. Steady mailbox was `0xEB020520` (candidate `a8`, fitness `5/8`,
  observed mask `0x20`) instead of expected `0xEB0308E8`.

Root cause found host-side after the board run: the generated candidate framebank
contained only one sequence per candidate, always FAR `0x00400d22`, but the target
INIT diff spans two FARs:

```text
80: bit_00400d23_100_06
a8: bit_00400d22_100_06, bit_00400d23_100_06, bit_00400d23_100_07
e8: bit_00400d22_100_06, bit_00400d23_100_06, bit_00400d23_100_07, bit_00400d23_100_14
```

So the board was genuinely editing CRAM, but each phenotype was truncated to a
single FAR before fitness measurement. The fix was the multi-seq framebank format
above; no DIN byte/bit-ordering change was needed.

## Verified Board Result

After rebuilding the 8KB framebuf RTL and descriptor firmware, then regenerating
the multi-FAR framebank, the EBAZ4205 run converged to the expected target:

- final mailbox: `0xeb0308e8`
- decoded: candidate `3` (`0xe8`), fitness `8/8`, observed mask `0xe8`
- stability: observed repeatedly, no wedge, `lut_o` live

This is the final EHW-2 PASS: NEORV32 rewrote a live LUT-INIT through the fabric
`xbus_icap` every fitness evaluation and selected the target candidate on silicon.

## Reproduction Flow

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
3. Use prjxray `bitread -y` on each `ehw2_init_<init>.bit` and run
   `ehw2-build-framebank-from-bits.py` to create every changed single-FAR ICAP
   write sequence per candidate. Reuse the M7.5.1 rule: one sync..DESYNC envelope
   per frame. Do not collapse two FARs into one envelope.
4. Use the generated `framebank.bin`.
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

## Gate Status

Host gates cover the oracle, firmware stub, and framebank format. The corrected
multi-FAR framebank has also been hardware-verified on EBAZ4205 via the final
`0xeb0308e8` mailbox result.
