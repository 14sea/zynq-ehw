# EHW-4.2 Results — Memetic Train-Unit Prep

Status: **HOST-PREP; no board claim.**

EHW-4.2 adds the first board-bound hardware split for the EHW-4 GA x HW-SGD line.
It copies the proven `zynq_xpart` M7.2 train-unit idea into this repository as an
EHW-local design, but adapts the contract from the original 2→4→1 XOR net to the
EHW-4 4→4→2, 24-byte genome.

Command:

```sh
python3 tests/compare_memetic_train_unit.py --skip-ooc
```

Verified output:

```text
tb_memetic_train_unit: PASS
PASS: memetic train-unit firmware stub matches mem_adapt (sse=4761 correct=31)
PASS: EHW-4.2 memetic train-unit RTL and firmware host stub
```

Isolated firmware build also passes the repo image tripwire:

```text
Memory utilization: text=3880 data=0 bss=0
[verify-image] OK: image == ELF load image
```

## What Is Proven

- `rtl/memetic_train_unit.v` implements the EHW-4.1 fixed-point contract:
  Q8.8 `qmul`, leaky derivative, error/delta clamps, and saturating Q8.8 master
  updates.
- The RTL test drives a generated full 40-sample epoch trace from the Python oracle
  and checks, at every sample:
  - accumulated SSE;
  - output-layer `d2`;
  - hidden-layer `d1`;
  - all 24 post-update master weights.
- `sw/ehw/memetic_train_mbox.c` uses the same train-unit MMIO protocol in host and
  board builds. The host model compares the train-unit path against `mem_adapt()`
  from `memetic_kernel.h`.
- The board firmware smoke test fits comfortably in the 16 KB NEORV32 DMEM budget:
  `.bss=0`, with only stack-local state in this first deterministic check.
- `rtl/dfx/tpu_rp_rm_memetic_train.v` keeps the existing `tpu_rp` interface,
  forwards non-train-unit accesses to the 4x4 INT8 array, and claims the local
  train-unit window at byte offsets `0x800..0x930`.
- `tests/vivado_ooc_memetic_train.tcl` is available for a Vivado OOC synth check in
  a board/Vivado environment. The one-command host gate uses `--skip-ooc`, matching
  the other RTL gates when Vivado is not on `PATH`.

## Vivado Resource Gate

This rung is board-bound, so RTL simulation is not sufficient. A post-review OOC
synth run on the first version found a real blocker: the RP pblock has 20 DSP48E1
sites, but the wrapper synthesized to 48 DSP48E1 total (`array 16 + train_unit 32`).
That version would place-fail and must not be pushed as board-ready.

The fix in `rtl/memetic_train_unit.v` removes the generic multiplier from the
leaky-derivative path. Because the negative leaky slope is a power of two,
`qmul(x, 256>>k)` is implemented as the exact rounding arithmetic shift
`(x + 2^(k-1)) >>> k`. The unit now keeps multiplication only for the two
loss-square terms; the outer products remain in firmware. The target resource
budget is:

```text
array DSP48E1      16
train_unit DSP48E1 <= 4
total DSP48E1      <= 20
```

Claude must rerun the non-skipped OOC gate before push or board build:

```sh
python3 tests/compare_memetic_train_unit.py
```

or directly:

```sh
vivado -mode batch -source tests/vivado_ooc_memetic_train.tcl
```

## Frozen Register Contract

Train-unit word addresses inside the claimed window:

| Word | Meaning |
|---:|---|
| `0..3` | `INA0..3`: `y[0..1]` for loss/d2, or `w2td2[0..3]` for d1 |
| `4..7` | `Z0..3`: `z2[0..1]` for loss/d2, or `z1[0..3]` for d1 |
| `8..11` | target lanes; only `T0/T1` are used by the 2-output net |
| `12..27` | gradient bank; `DW0..7` for W2, `DW0..15` for W1 |
| `28` | command: `[0]=loss_d2 [1]=d1 [2]=upd_l2 [3]=upd_l1 [4]=clr_loss` |
| `32..47` | Q8.8 master `W1[4][4]`, row-major |
| `48..55` | Q8.8 master `W2[2][4]`, row-major |
| `64..65` | output-layer delta `D2[0..1]` |
| `68..71` | hidden-layer delta `D1[0..3]` |
| `76` | accumulated epoch SSE |

In the DFX wrapper this appears at byte address `0xF0000800 + word*4`.

## Board Firmware Sketch

`sw/ehw/memetic_train_mbox.c` is intentionally a smoke test, not the final board GA.
It adapts the M7.5.3 seed for two epochs through the train-unit protocol and checks
the result against the software oracle. Mailbox tags:

```text
0xF4000042  reached EHW-4.2 firmware
0xF420mmss  mm = genome mismatch count, ss = SSE-match bit
0xF430ccss  cc = adapted-genome label correct, ss = low 16 bits of SSE
0xF4F00000  PASS
0xF4F00001  FAIL
```

On a board build, use the existing isolated firmware build discipline:
`__neorv32_ram_size=16k`, `make verify-image`, no post-config settle requirement,
and U-Boot `fpga loadb`.

## Next

EHW-4.3 can now run the actual board loop: NEORV32 evaluates candidates with a short
HW-SGD inner loop using the 4x4 array plus this train unit, then publishes the
mailbox curve for board verification.
