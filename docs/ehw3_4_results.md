# EHW-3.4 Results — Per-Eval ICAPE2 Spare-Routing Evolution Host Prep

Generated / verified by:

```bash
python3 tests/compare_ehw34_icap.py
```

Status: **HOST PREP COMPLETE / BOARD ICAPE2 RUN PENDING.** This stretch combines
EHW-2's internal-ICAPE2 per-eval loop with the EHW-3 spare-routing genome. The
board design intentionally has **no PS-HWICAP**; PS only stages the framebank and
reads GPIO mailboxes. Do not use PS-HWICAP `readreg`/`writeseq` on this bitstream.

## Deliverables

- `sim/ehw34_icap_oracle.py`: tiny candidate-bank oracle for the spare-routing
  genome under the hard disabled-A1 island fault.
- `sw/ehw/ehw34_icap_spare_route.c`: NEORV32 firmware and host stub. Board mode
  streams candidate frame sequences through `rtl/xbus_icap.v`, then scores the
  live island.
- `rtl/ehw34_spare_route_target.v`: baked spare-route target behind XBUS, starting
  from baseline `SR34/c8/7`.
- `rtl/neorv32_soc_icap_sr.vhd`: internal-ICAPE2 NEORV32 SoC with XBUS maps:
  `0xF1000000` mailbox, `0xF3000000` xbus_icap, `0xF4000000` spare-route target,
  `0xF5000000` framebank.
- `scripts/ehw34-framebank-pack.py`: generalized 16-byte-genome framebank packer.
- `scripts/ehw34-build-framebank-from-bits.py`: bitread/diff wrapper around
  `m75-build-frameseqs.py` and the EHW-3.4 packer.
- `tests/compare_ehw34_icap.py`: host gate tying Python oracle, C firmware stub,
  RTL target sim, and framebank packing.
- `tests/tb_ehw34_spare_route_target.v`: Verilog target baseline sweep.
- `tests/vivado_ooc_ehw34_spare_route.tcl`: optional Vivado OOC synth gate.
- `vivado/icap_ehw34/build_ehw34_icap.tcl`: no-PS-HWICAP build that writes four
  same-route candidate bitstreams from one routed design.

## Candidate Bank

The first micro bank is deliberately small, mirroring the EHW-2 stretch style:

| idx | label | genome | observed | fitness |
|---:|---|---|---:|---:|
| 0 | base | `0a08010f320104000202000401010200` | `c8` | 7/8 |
| 1 | logic | `0b090903b10104000202000401010200` | `5a` | 4/8 |
| 2 | route | `0a08010f320004040102000001020300` | `00` | 4/8 |
| 3 | repair | `0b090903b10004040102000001020300` | `e8` | 8/8 |

Each board candidate evaluation should read its descriptor from the staged
framebank, stream every sequence for that candidate to `xbus_icap`, sweep the live
target rows through `0xF4000008/0xF400000c`, and publish
`0xE9000000 | idx<<16 | fitness<<8 | observed`.

Expected best endpoint: `0xEA0308E8`, then steady `0xEC0308E8`.

## Framebank Contract

`scripts/ehw34-framebank-pack.py` writes an 8KB big-endian framebank:

```text
word0 = 0x45483334 ("EH34", written last by loader)
word1 = candidate count
word2 = descriptor base = 4
word3 = descriptor words = 37
descriptor = genome_word0..3, nseq, then up to 16 (offset,len) pairs
```

This is a generalized EHW-2 bank: one 16-byte genome plus up to 16 ICAP envelopes
per candidate. Each envelope must still fit the existing `xbus_icap` 255-word
burst limit. If a candidate's edited INITs span multiple FARs, keep one envelope
per FAR.

## Host Gate Output

Expected local output includes:

```text
target=0xe8 final=repair observed=0xe8
PASS: ehw34_spare_route_target marker=53523334 mask=c8 fitness=7/8
packed 4 candidates, 2048 words -> .../framebank.bin
PASS: EHW-3.4 oracle, C stub, RTL target, and framebank pack agree
```

Vivado is optional in the compare script. When available, run without `--skip-ooc`
to execute `tests/vivado_ooc_ehw34_spare_route.tcl`.

## Board Flow

Build firmware into IMEM first:

```bash
cp sw/ehw/Makefile sw/ehw/spare_route_kernel.h sw/ehw/ehw34_icap_spare_route.c sw_src/sr_build/
cd sw_src/sr_build
make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
  RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
  APP_SRC=ehw34_icap_spare_route.c clean install
```

Local link result:

```text
text=1068 data=0 bss=0 dec=1068 hex=42c
```

Then route and write same-route candidate bitstreams:

```bash
vivado -mode batch -source vivado/icap_ehw34/build_ehw34_icap.tcl
```

After `bitread` produces `.bits` files for `base/logic/route/repair`, build and
stage the framebank:

```bash
python3 scripts/ehw34-build-framebank-from-bits.py --out-dir runs/ehw34_seqs \
  --base-label base \
  --bit-template 'vivado/icap_ehw34/build/ehw34_{label}.bit' \
  --bits-template 'runs/ehw34_bits/{label}.bits' \
  --candidate base=0a08010f320104000202000401010200 \
  --candidate logic=0b090903b10104000202000401010200 \
  --candidate route=0a08010f320004040102000001020300 \
  --candidate repair=0b090903b10004040102000001020300

python3 scripts/ehw2-framebank-load.py runs/ehw34_seqs/framebank.bin 0x40000000
```

Board acceptance:

- after load, firmware waits with `0xE8400000` until framebank word0 becomes
  `EH34`;
- candidate loop publishes `E900..` rows, with final candidate `repair/e8/8`;
- best publishes `0xEA0308E8`, steady loop publishes `0xEC0308E8`;
- PS/NEORV32 must not reset during the per-eval ICAPE2 loop.

Only after those mailbox words are captured should EHW-3.4 be marked
hardware-verified.
