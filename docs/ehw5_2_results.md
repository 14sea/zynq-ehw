# EHW-5.2 Results — Combined Spare-Route VRC + Train-Unit RM

Status: **HW-VERIFIED on EBAZ4205 at FCLK0=50 MHz.** Final mailbox:
`0xF5F00000` (`mism=0`, `got_sse=gold_sse=4560`, `correct=38`, VRC marker
`SRV0`, mask `0xa0`).

Important board precondition: miner U-Boot leaves FCLK0 at 125 MHz, while the
Vivado DFX design signs off `clk_fpga_0` at 50 MHz. Run
`scripts/board-set-fclk50.py` before `fpga loadb`; see `docs/hw_notes.md`.

EHW-5.2 combines the two hardware paths needed by the EHW-5 hybrid line:

- EHW-3 spare-route VRC feature island at byte window `0x400..0x47f`
- EHW-4 memetic train unit at byte window `0x800..0x930`
- existing 4x4 array forwarded outside those local windows

Deliverables:

- `rtl/dfx/tpu_rp_rm_memetic_struct.v`
- `rtl/memetic_train_unit_lite.v`
- `sw/ehw/memetic_struct_train_mbox.c`
- `tests/compare_memetic_struct_train.py`
- `tests/vivado_ooc_memetic_struct.tcl`

## Address Map

Inside the existing `0xF0000000` XBUS peripheral window:

```text
0x000..0x3ff  existing 4x4 array / TPU accelerator
0x400..0x47f  spare-route VRC feature island
0x800..0x934  memetic_train_unit_lite, firmware-compatible subset plus BUSY
```

The EHW-5.2 train-unit window keeps the same words used by EHW-4.3/EHW-4.6
firmware, plus read-only `word 77` (`0xF0000934`) as `BUSY` for the serialized
update path.

## Gate

```bash
python3 tests/compare_memetic_struct_train.py
```

Result in this environment:

```text
tb_memetic_struct_rm: PASS
PASS: memetic-struct VRC+train-unit stub matches CPU golden (mask=a0 sse=4560 correct=38)
SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_memetic_struct.tcl
PASS: EHW-5.2 combined VRC + train-unit host prep
```

The RTL test drives the combined RM wrapper through the XBUS port:

- reads the spare-route VRC marker at `0xF0000420`;
- loads the 16-byte EHW-5.0b `bias_x3` feature genome through `0xF0000440..`;
- checks the VRC truth mask `0xa0`;
- checks live feature output rows;
- writes and reads train-unit master-weight registers through `0xF0000800`.
- checks a serialized W2 update through `BUSY` and the updated master-weight readback.
- replays the full 40-sample EHW-5.2 epoch transaction stream against a Python
  golden: every per-sample VRC `INPUT`/`OUTPUT` phi, `D2`, accumulated loss, `D1`,
  and post-update W1/W2 master word is checked.

The firmware host stub then uses the same register protocol to:

1. load the spare-route genome into the VRC window;
2. transform each 40-sample input using the VRC feature and `bias_x3`;
3. run one fixed-point SGD epoch through the memetic train-unit protocol;
4. compare the adapted 24-byte weight genome against a CPU golden.

`mask=0xa0` is intentional: this EHW-5 feature genome is a useful dataset feature
from the pressured `bias_x3` arm, not the EHW-3 majority target `0xe8`.

## Resource Fix After First Review

The first combined wrapper used the full EHW-4 `memetic_train_unit.v` and failed
Claude's mandatory OOC resource gate:

```text
DSP48E1  18/20  pass
LUT      5049/4400  fail
```

The corrected EHW-5.2 wrapper uses `memetic_train_unit_lite.v`. It keeps the
firmware-visible train-unit words used by this line, fixes `LR_SHIFT=7` and
`K=2`, and serializes W1/W2 master-weight updates behind `TU_BUSY`. This removes
the 24 parallel saturating update lanes that are unnecessary for the serial
candidate-evaluation firmware path.

A later board run of the lite wrapper still failed deterministically in the
hardware train-unit arm: VRC marker and mask were correct, the board CPU-golden
path matched the host (`gold_sse=4560`), but the train-unit arm reported
`got_sse=4611`, two genome bytes mismatched, and final correct count was `35`
instead of the host/stub `38`. The host gate was strengthened after that failure:
`sr_read(SR_OUTPUT)` in host mode now models the real input/output register path,
and the RTL test now replays the full epoch rather than a smoke update only.

Because full RTL sim still passes, the remaining likely issue is a Vivado
synthesis or post-implementation behavior in the lite update path. One attempted
fix used fully explicit `case` statements with per-register `update_value(...)`
arms; Claude's OOC gate correctly held it because Vivado duplicated the
saturating add/sub lane 24 times (`+1210` LUT cells, `+308` CARRY4).

The shared-lane fix keeps one arithmetic lane:

```text
cur_w/cur_dw mux -> one update_value(cur_w, cur_dw) -> next_w -> case writeback
```

The explicit `case` logic is now only write decode, not 24 arithmetic lanes.

A clean A/B/C board matrix then exonerated the static/fb_0 theory and temporarily
pointed at the 5.2 RM/lite train-unit arm: clean 5.2 builds failed with and
without fb_0, while the proven 4.3 train-unit RM passed on a fresh placement.
That intermediate conclusion is superseded by the final clock root cause below;
the A/B/C matrix still usefully excludes fb_0/static/dirty-project explanations.
See `docs/board_results.md` and `docs/evidence_ehw52/`.

The retained RTL hygiene fix keeps the shared update lane and hardens the TU XBUS boundary:
the wrapper now latches `memetic_train_unit_lite.rdata` when a TU request is
accepted and returns that held value during the ACK cycle. This removes the
unsafe pattern where NEORV32 could sample a combinational read-data path on the
same edge that the lite unit updates `BUSY`, weight, loss, or delta state. Host
RTL still replays the full epoch and passes. Later board evidence showed this
was not the root cause, but it improves bus protocol discipline and was kept.

## Board Result And Root Cause

Claude ran the retained RTL through host gates, OOC (`LUT 3455`, `DSP 18/20`),
clean DFX build, and board load. At the miner default FCLK0 it still failed. The
decisive checks were:

- post-synth full-epoch funcsim PASS;
- post-route funcsim of the routed RM cell from the failing build PASS;
- `check_timing` clean with one 20 ns `clk_fpga_0`;
- SLCR `FPGA0_CLK_CTRL` readback showed miner U-Boot had FCLK0 at 125 MHz, not
  the 50 MHz signoff clock.

Same-bitstream silicon proof:

| Bitstream | 125 MHz miner default | 50 MHz signoff clock |
|---|---|---|
| `ws_fix` (`a327a9f`) | FAIL, `mism=14`, `got_sse=4738` | PASS `0xF5F00000`, `mism=0`, `4560/4560` |
| `ws_withfb` (`465b9c7`) | FAIL, `mism=1` | PASS `0xF5F00000`, same evidence words |

Conclusion: the previous failures were overclocking beyond the signed-off PL
clock, not an RM logic bug. Historical board PASSes remain valid because they
were bit-exact at the clock they actually ran; future board claims must pin
FCLK0 to the signoff frequency first.

## Resource Gate For Board

Vivado is not available in this host environment. Before any board run, Claude
must run:

```bash
vivado -mode batch -source tests/vivado_ooc_memetic_struct.tcl
```

Required board-side checks:

- OOC synth must pass;
- DSP should remain near `18/20` if the array is retained (`array 16 + train unit
  loss squares 2`);
- LUT/slice utilization is still the primary risk and must be checked with both the
  OOC hierarchical report and place-level `report_utilization -pblocks`;
- target pblock LUT utilization is `<= ~3800` before board run, leaving route
  headroom under the 4400 LUT hard capacity;
- no board claim until OOC and place/resource gates pass.

## Next

EHW-5.3 connected this combined RM to the full hybrid GA loop and was
board-verified at FCLK0=50 MHz. The C twin from EHW-5.1 remained the
firmware-facing golden, and this EHW-5.2 firmware stub was the MMIO protocol
template for the final EHW-5.3 firmware.
