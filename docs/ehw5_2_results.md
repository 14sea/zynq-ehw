# EHW-5.2 Results — Combined Spare-Route VRC + Train-Unit RM Prep

Status: **HOST-PREP ONLY.** No board claim is made here.

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

EHW-5.3 should connect this combined RM to the full hybrid GA loop. The C twin
from EHW-5.1 remains the firmware-facing golden, and this EHW-5.2 firmware stub
is the MMIO protocol template.
