# zynq_ehw Hardware Notes

This file is the hardware-truth scratchpad for codegen and review. Prefer adding
new board facts here instead of relying on memory.

## Board And Provenance

- Target board: EBAZ4205, Xilinx Zynq-7010 / XC7Z010.
- EHW builds on hardware-verified primitives copied from `/home/test/zynq_xpart`.
- Source projects remain read-only. Local snapshots live under `external/` and are
  gitignored.
- PS workflow used by the reference project is U-Boot-centric: load full bitstreams
  with `fpga loadb`; load partials with `loadbp` / measured-load wrappers.

## Board Bring-up (Claude's entry point — host side, /home/test/xilinx)

The board tooling (openocd `ebaz4205.cfg`, `uboot-intercept.py`, `.env/bin/python`,
`/dev/ebaz-uart`) lives in `/home/test/xilinx`, NOT in this repo. ChatGPT writes
firmware/host code; the steps below are Claude's to run.

- **Reach the U-Boot prompt from any board state** (SRST is not wired; reset goes
  through SLCR). In `/home/test/xilinx`:
  ```bash
  .env/bin/python scripts/uboot-intercept.py --duration 25 &   # hammer 'd' on UART
  sleep 1
  openocd -f ebaz4205.cfg -c init -c halt \
    -c "mww phys 0xF8000008 0xDF0D" -c "mww phys 0xF8000200 1" -c shutdown
  wait   # look for "U-Boot prompt detected"; autoboot break key is 'd', not Ctrl-C
  ```
- **CH340 brownout on reset:** the reset drops a ~1 s 3.3 V brownout that detaches
  the CH340 from WSL usbipd. Keep it re-attaching in the background:
  `usbipd.exe attach --wsl --busid 4-3 --auto-attach`. Always use the `/dev/ebaz-uart`
  symlink — the underlying `/dev/ttyUSBn` number is unstable across detach/reattach.
- **Iterating after a load:** after `fpga loadb`/`loadbp` the PS stays in U-Boot.
  To re-iterate, just flush the UART and `loadb` again — do NOT re-run the SLCR
  reset each time.
- **Safe PL-AXI probe that never wedges the CPU** (use instead of a Linux `/dev/mem`
  read, which hard-hangs if FCLK0 is gated):
  ```bash
  openocd -f ebaz4205.cfg \
    -c "target create zynq.ahb mem_ap -dap zynq.dap -ap-num 0" \
    -c init -c "zynq.ahb mdw 0x41200000 1" -c shutdown
  ```
- **DEVCFG wedge recovery:** a bad/corrupt bitstream load leaves DEVCFG stuck
  ("Timeout waiting for PCFG_INIT"); subsequent good loads also fail. **Only a
  power-cycle recovers** (physical Type-C unplug; the S2 button on this PCB rev is
  unreliable). Don't deliberately load a bad bitstream unless ready to power-cycle.

## Visibility And Mailbox

- NEORV32 firmware writes its mailbox at PL address `0xF1000000`.
- The PS observes the same mailbox through AXI GPIO at physical address
  `0x41200000`, usually from U-Boot:

  ```text
  md 0x41200000 1
  ```

- Current copied DFX top does **not** pin out NEORV32 `uart0`. Do not design board
  protocols that require direct host-to-softcore UART input unless the static
  design is changed.
- Host scripts should treat the mailbox as the reliable PS-visible status channel.

## EHW-0.2 Mailbox Tags

`sw/ehw/ehw_eval_mbox.c` publishes:

- `0xE0000000`: firmware reached `main`.
- `0xE100xxxx`: VRC array self-check; low 16 bits should be `14`.
- `0xE2ccssss`: final score; `cc` is correct count, `ssss` is low 16 bits of SSE.
- `0xE3ffffff`: low 24 bits of fitness.

Default compiled champion expected by the host stub:

- correct: `40/40`
- SSE: `4799`
- fitness: `39995201`

## EHW-0.3 Board-Resident GA Mailbox Tags

`sw/ehw/ehw_ga_mbox.c` runs the deterministic GA on NEORV32 and publishes:

- `0xE8000000`: board-resident GA firmware reached `main`.
- `0xE900ggcc`: generation progress; `gg` is generation low 8 bits, `cc` is best
  correct count.
- `0xEAssssss`: current best SSE low 24 bits.
- `0xEBffffff`: current best fitness low 24 bits.
- `0xEC0000cc`: GA done; `cc` is final correct count.
- `0xD0aabbcc` .. `0xD7aabbcc`: final 24-byte champion genome, three signed
  INT8 bytes per mailbox word.

Host gate:

- `tests/compare_ehw0_twin.py` compiles `ehw_ga_mbox.c` with `EHW_HOST_STUB` and
  compares its CSV curve byte-for-byte against `sim/oracle_evolve.py`.
- Default board parameters are `seed=3`, `population=32`, `generations=64`; this
  reaches `40/40` by generation 1 in the current oracle.

## 4x4 VRC Array Register Map

The register-loaded VRC path uses `tpu_accel.v` from the copied `zynq_xpart`
reference.

Map verified against `zynq_xpart/rtl/tpu_accel.v` header (2026-06-29).

- TPU base in NEORV32 address space: `0xF0000000` (confirmed in `sw/m7_train/m752_loop.c`).
- `TPU_CTRL` at `+0x00`
  - bit 0: start compute (`sa_en`).
  - bit 4: clear accumulators / done.
  - bit 8: `load_from_lut` (v3 one-shot: copy LUT[0..15] into PE weights — the
    LUT-KCM auto-load path; relevant for the ICAP champion-bake reveal).
- `TPU_STATUS` at `+0x04`
  - bit 0: compute done.
  - bit 1: `lut_load_busy` (v3).
- `TPU_W_ADDR` at `+0x08`: row in bits `[3:2]`, col in bits `[1:0]`.
- `TPU_W_DATA` at `+0x0C`: `[7:0]` single weight byte → 1-cycle `load_weight`
  pulse at the current `W_ADDR` (per-PE load; `W_DATA4` is the bulk path).
- `TPU_X_IN` at `+0x10`: packed signed INT8 `{x3,x2,x1,x0}`.
- `TPU_W_DATA4` at `+0x14`: packed signed INT8 `{w3,w2,w1,w0}`; loads a full row
  and auto-increments row. **Bus stalls 3 extra cycles** during the bulk load.
- `TPU_RES(r)` at `+0x20 + 4*r`: signed INT32 row accumulator.

Compute sequence: load weights → write `X_IN` → `CTRL[0]=1` → poll `STATUS[0]` →
read `RES0-3` → `CTRL[4]=1` to clear before the next output group.

Reference self-check:

- W rows `{{1,1,1,1},{1,2,3,4},{2,2,2,2},{1,0,1,0}}`
- X `{2,3,4,5}`
- expected accumulators `{14,40,28,6}`.

## M7.5.3 Fixed-Point Rules Used By EHW-0

EHW-0 intentionally follows the M7.5.3-lite software-net math from
`external/zynq_xpart/sim/oracle_m753.py`.

- `WSHIFT = 2`
- `XSHIFT = 2`
- `XSHIFT_H = 3`
- `K = 2`
- leaky activation: `z >= 0 ? z : z >> K`
- array downshift rule: `down = 8 - wshift - ashift`
  - L1 input to hidden: `8 - 2 - 2 = 4`, so `(acc + 8) >> 4`
  - L2 hidden to output: `8 - 2 - 3 = 3`, so `(acc + 4) >> 3`
- Bias is added after array requant and before leaky.
- Hidden activations are quantized with `XSHIFT_H`.

Important guard:

- The M7.5.3 trained tile reproduces `M753_GOLD_CLS` exactly.
- That golden classification bitmap is `37/40` against labels. This is not a
  model bug; it is the upstream M7.5.3 result being reproduced.
- EHW-0's GA may evolve a different 24-byte genome that reaches `40/40` labels
  under the same fixed-point net.

## Leaky Variants

Do not mix these two paths:

- M7.5.3 software-net / EHW-0 oracle: `z >= 0 ? z : z >> K`.
- M6 VPU/LUT-KCM prediction path documented in `zynq_xpart`: uses the VPU leaky
  form, noted there as `z >= 0 ? z : z - (z >>> alpha)`.

If a future milestone uses the VPU path rather than the EHW-0 software-net path,
add a fresh golden cross-check before trusting results.

## EHW-4.2 Memetic Train-Unit Contract

EHW-4.2 adapts the `zynq_xpart` M7.2 train-unit idea into this repo, but the
contract is **EHW-4's 4→4→2 net**, not the original 2→4→1 XOR train unit.

- RTL: `rtl/memetic_train_unit.v`.
- DFX wrapper: `rtl/dfx/tpu_rp_rm_memetic_train.v`.
- Firmware smoke test: `sw/ehw/memetic_train_mbox.c`.
- Host gate: `tests/compare_memetic_train_unit.py`.
- Status: host-prep only; no board claim until exact mailbox words are logged in
  `docs/board_results.md`.

Fixed-point rules:

- Q8.8 master weights, 24 values: `W1[4][4]` + `W2[2][4]`.
- Biases stay fixed in firmware/oracle and are not part of the genome.
- `qmul(a,b) = (a*b + 128) >>> 8`.
- `leaky'(z) = 256` if `z>=0`, otherwise `64`.
- `err` clamp: `[-2^20, 2^20-1]`.
- delta clamp: `[-2^14, 2^14-1]`.
- update: `master = sat16(master - (dw >>> 7))`.
- Resource constraint for the DFX RP: the 4x4 array already consumes 16 DSP48E1
  out of the pblock's 20, so `memetic_train_unit` must stay at **4 DSP48E1 or
  fewer**. Do not reintroduce generic `qmul` multipliers in the leaky-derivative
  lanes; `qmul(x, 256>>k)` is the exact rounding shift `(x + 2^(k-1)) >>> k`.
  OOC synth/resource reporting is mandatory before board build.

Train-unit MMIO word map inside the claimed window:

- `0..3`: `INA0..3` (`y[0..1]` for loss/d2, or `w2td2[0..3]` for d1).
- `4..7`: `Z0..3` (`z2[0..1]` for loss/d2, or `z1[0..3]` for d1).
- `8..11`: targets; only `T0/T1` are used.
- `12..27`: gradient bank; `DW0..7` for W2, `DW0..15` for W1.
- `28`: command `[0]=loss_d2 [1]=d1 [2]=upd_l2 [3]=upd_l1 [4]=clr_loss`.
- `32..47`: Q8.8 master W1, row-major.
- `48..55`: Q8.8 master W2, row-major.
- `64..65`: D2.
- `68..71`: D1.
- `76`: accumulated epoch SSE.

In the DFX wrapper, this appears at byte address `0xF0000800 + word*4`; non-claimed
accesses continue to the 4x4 array at the existing `0xF0000000` register map.
Firmware publishes through the usual mailbox at `0xF1000000` (PS sees
`0x41200000`, unless the chosen SoC variant documents channel 2).

## ICAP / PCAP Facts

- Zynq config-engine handoff uses `devcfg.CTRL[PCAP_PR]`, bit 27, at
  `0xF8007000`.
- Route PR to ICAP by clearing bit 27:

  ```text
  mw 0xF8007000 0x4400e07f
  ```

- Restore PCAP ownership by setting bit 27:

  ```text
  mw 0xF8007000 0x4c00e07f
  ```

- Verify ICAP health before baking:
  - `readreg 12` should return IDCODE `0x13722093`.
  - M7.5 reference STAT example: `0x46106ffd`.
- ICAP reveal edits only LUT-INIT bits. Routing is never evolved or randomly
  edited.
- **One FAR-set per sync..DESYNC envelope** (M7.5.1 lesson): two FAR writes inside
  one envelope mis-commit the buffered frame and corrupt the array (seen as
  `0x7F7F7F7F`). Emit each frame as its own write sequence.
- LUT-INIT bit locations (the genotype↔phenotype map) come from **prjxray**:
  tools at `/home/test/prjxray`, DB at `/home/test/prjxray-db`. CRC is disabled in
  the reference ICAP write sequence.

## Post-Config Settle And Build Variance

> **⚑ SECTION SUPERSEDED 2026-07-02 — both phenomena were toolchain bugs, not
> silicon.** Root cause: NEORV32 v1.12.9 `image_gen` drops LMA alignment gaps
> (whole `.rodata` shifts −4 B in IMEM whenever `.text % 8 == 4` under the
> picolibc ld script) → deterministic constant corruption that *looked* build/
> firmware-size-dependent. The M7.1 "wall-clock settle" correlation was
> firmware-layout confounding (zero-settle cold start is bit-exact on a correct
> image); the M7.2 "in-context-routing" variance was the same bug (flat non-DFX
> control reproduced it; fixed image cures it — full multi-epoch rm_train and the
> 64→8→4 MNIST now train bit-exact). Fix auto-applied by `scripts/setup-deps.sh`
> from `sw/patches/image_gen_lma_fix/`; tripwire = `make verify-image`. See
> zynq-xpart `docs/m7_2_dcpdiff.md` + stnolting/neorv32#1593. **New firmware needs
> NO settle.** Historical bullets kept below for the record:

- ~~Reference M7.1 found that some array workloads must wait **wall-clock time**
  after `fpga loadb` before compute~~ — settle busted; layout confound.
- Chunked settle loops with mailbox heartbeats remain a *useful liveness pattern*
  (a heartbeat proves the CPU is alive), just not needed as settle.
- ~~M7.2 exposed a build-dependent 7-series DFX in-context-routing issue~~ —
  retired; it was the image_gen bug.
- The VRC single-fixed-bitstream design remains a virtue (determinism, eval
  speed), just no longer *motivated* by a routing gremlin.
- Still, any new board milestone must compare board output against the host model
  and record exact mailbox words in `docs/board_results.md`.

## Current Open Hardware Decision

True host-in-loop EHW-0.2 needs a PS-to-PL command path for genomes. Because
NEORV32 `uart0` is not available in the copied static design, the two practical
options are:

- add a small PS-writeable command mailbox / BRAM path in the static design; or
- move directly toward board-resident GA and use the existing mailbox only for
  progress, score, and champion reporting.

Until that is decided, `sw/ehw/ehw_eval_mbox.c` is the board bridge: it evaluates
a compiled champion genome on the VRC array and publishes mailbox results. The
next bridge, `sw/ehw/ehw_ga_mbox.c`, runs the GA on-board and uses the mailbox for
progress/champion reporting, avoiding a PS-to-PL genome stream for now.
