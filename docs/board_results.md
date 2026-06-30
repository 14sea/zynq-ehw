# Board Results

Record only observations from commands actually run on the EBAZ4205. Keep exact
mailbox words and commands so codegen can reproduce the host model comparison.

## EHW-0.2 VRC Compiled-Champion Eval

Firmware: `sw/ehw/ehw_eval_mbox.c`

Host model:

- array self-check low16: `14`
- correct: `40/40`
- SSE: `4799`
- fitness: `39995201`

Board commands / observations:

```text
# build firmware into NEORV32 image:
# make APP_SRC=ehw_eval_mbox.c ...

# load full bitstream:
# fpga loadb ...

# observe mailbox:
# md 0x41200000 1
```

Expected mailbox sequence:

- `0xE0000000`: reached main.
- `0xE100000E`: array self-check acc0 = 14.
- `0xE22812BF`: correct = 40, SSE low16 = `0x12BF` = 4799.
- `0xE3624741`: fitness low24 for `39995201`.

Result:

- status: pending board run
- exact observed words:
- notes:

## EHW-0.3 Board-Resident GA

Firmware: `sw/ehw/ehw_ga_mbox.c`

Host model:

- seed: `3`
- population: `32`
- generations cap: `64`
- expected curve:

```text
gen,best_correct,best_acc,best_sse,best_fitness,genome
0,38,0.9500,4787,37995213,3 -1 -3 -2 13 19 21 18 -7 -3 -1 0 4 0 2 -35 -2 27 3 0 14 -14 5 13
1,40,1.0000,4799,39995201,3 -1 -3 -2 13 13 21 18 -7 -3 -7 0 4 0 2 -35 -2 27 3 0 14 -14 5 13
```

Expected mailbox tags:

- `0xE8000000`: reached main.
- `0xE9000026`: generation 0, correct 38.
- `0xEA0012B3`: SSE 4787.
- `0xEB43C2CD`: fitness low24 for 37995213.
- `0xE9000128`: generation 1, correct 40.
- `0xEA0012BF`: SSE 4799.
- `0xEB624741`: fitness low24 for 39995201.
- `0xEC000028`: done, correct 40.
- `0xD0..0xD7`: 24-byte champion genome chunks.

Result:

- status: **PASS — HW-VERIFIED on EBAZ4205 (2026-06-29), first DFX roll**
- bitstream: `vivado/dfx/build/dfx.runs/impl_1/dfx_top.bit` (static + `rm1_tpu`),
  timing met (WNS +7.829 / WHS +0.042 / 0 DRC errors)
- exact observed words (`host/ehw_watch.py`):
  - `0xe9004028` — GA_GEN gen=64 correct=40/40
  - `0xea0012bf` — GA_SSE 4799
  - `0xeb624741` — GA_FITNESS low24 = `39995201 & 0xFFFFFF`
  - `0xD0..0xD7` champion chunks
- reconstructed champion genome:
  `3 -1 -3 -2 13 13 21 18 -7 -3 -7 0 4 0 2 -35 -2 27 3 0 14 -14 5 13`
- host cross-check: `sim/oracle_evolve.py --seed 3 --population 32 --generations 64`
  → champion **bit-identical**, correct 40/40, SSE 4799, fitness 39995201. PASS.
- notes:
  - The board reports `gen=64` (ran the full cap) while the host reaches 40/40 at
    `gen 1`; the final champion is identical. Only difference from ChatGPT's
    predicted tags is the gen counter in `0xE9` (predicted `0x0128` gen1, observed
    `0x4028` gen64) — both `correct=40`, expected.
  - The 4×4 VRC array (`tpu_accel`) computes correctly on silicon: SSE + genome
    match the host model exactly → the register-map driver in `ehw_ga_mbox.c` is
    hardware-verified. The 300000-count settle was sufficient for `rm1_tpu`.

---

## EHW-1.1 board-resident CGP GA (2-bit multiplier) — PASS (2026-06-29)

**On-chip evolution of a logic circuit on real silicon.** Board-resident CGP GA on
NEORV32 evolved a 2-bit unsigned multiplier (fixed-routing LUT grid, `cgp_kernel.h`)
to a perfect solution, champion bit-identical to the host oracle.

- Firmware: `sw/ehw/cgp_ga_mbox.c` (Claude-authored for board; GA helpers + loop
  COPIED VERBATIM from ChatGPT's `cgp_eval.c` for bit-exactness). `.text` 1780 B.
  Software LUT-grid eval (does NOT use the fabric array) — so this is on-chip
  *evolution of a logic circuit*, not yet a fabric-LUT substrate (that = `cgp_vrc.v`,
  a later rung).
- Bitstream: same static+`rm1_tpu` build, new IMEM (timing met WNS +7.574).
- **Pre-board host gate (PASS):** `cc -DCGP_HOST_STUB ... cgp_ga_mbox.c` champion ==
  `cgp_eval.c`/`oracle_cgp.py` champion, bit-exact.
- Observed mailbox (`0xCC`/`0xCA`/`0xB0..0xBB`):
  - `0xcc000010` — DONE rows = 16/16
  - `0xca000040` — fitness 64/64
  - champion genome (b0..bb): `aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0
    4c00 8000` — **bit-identical to host** (12/12 words), 12 active nodes.

### Gotcha caught on silicon (worth keeping)
- First board attempt: mailbox stuck at `0x00000000` — firmware wasn't writing. Root
  cause = I wrote the firmware-side `MBOX` to the **PS** address `0x41200000` instead
  of the **PL** mailbox input `0xF1000000` (PS *reads* 0x41200000; firmware *writes*
  0xF1000000 — `docs/hw_notes.md`). The **host gate did NOT catch this** (host stub
  uses `MBOX_STUB`), only the board did → board run is the final gate for
  board-specific addresses. Fixed → rebuilt → PASS.

---

## EHW-0.5 ICAP champion-bake reveal — PASS (2026-06-29)

**"The chip wrote its evolved weights into its own LUT logic, live."** The EHW-0.3
board-evolved champion's W1 tile (16 INT8 weights) was ICAP-baked into the LUT-KCM
fabric, PS/NEORV32 never reset, mailbox bit-exact to the VPU model.

- Setup: built static + `rm_lutkcm` in zynq_ehw (`build_lutkcm.tcl`, impl_7), firmware
  `sw/ehw/lutkcm_post.c` (= zynq_xpart `tpu_vpu_firmware`: drives lutkcm tile + VPU
  bias→leaky→requant, publishes POST). **VPU leaky here is `z-(z>>>α)`** (NOT the
  m753 `z>>k`) — so this is a *mechanism* reveal (evolved weights live in LUT logic,
  output bit-exact to the VPU model), not the same classifier accuracy.
- Champion W1 tile (champion[0:16] as 4×4): `[[3,-1,-3,-2],[13,13,21,18],
  [-7,-3,-7,0],[4,0,2,-35]]`. VPU-model golden POST = `[-14,127,-81,-128]` →
  mailbox **0x80AF7FF2**.
- Frame gen (reusing M7.5.1 tooling, outputs to `vivado/dfx/m65_icap/`, gitignored):
  `m753_edit_tile.tcl` baked the 16 weights into the routed impl_7 dcp → champion
  partial; prjxray `bitread -y` diff (57 set + 10 clr bits) → `m75-build-frameseqs.py`
  → **20 per-frame ICAP write sequences**, all 20 self-checked vs prjxray.
- Board flow: `fpga loadb` lutkcm → baseline **0x1019391F** (matches VPU golden) →
  `PCAP_PR=0` (`mw 0xF8007000 0x4400e07f`) → attest `readreg 12 = 0x13722093` →
  20× `hwicap-uart.py writeseq` → mailbox **0x1019391F → 0x80AF7FF2** (3× stable,
  bit-exact) → re-attest `0x13722093` → `PCAP_PR=1` restored.

### Gotcha caught on silicon (worth keeping)
- First bake loop ran in the foreground and was **killed by the 2-min tool timeout
  mid-`writeseq`** → left the ICAP config FSM mid-frame; subsequent `readreg`
  returned garbage (`0xffffffdb`) and re-baking on top did NOT recover (mailbox stuck
  at a mixed `0x157f1cee`). The board stayed responsive (mailbox + NEORV32 alive — NOT
  a DEVCFG wedge). **Recovery: full `fpga loadb` reload** (resets ICAP + tile to
  baseline) → re-attest clean `0x13722093` → bake all 20 **uninterrupted in the
  background** → PASS. **Lesson: never run the multi-frame ICAP bake in a foreground
  tool call (timeout kills it mid-transfer and corrupts ICAP); always background it.**

---

## EHW-1.1-fabric: CGP GA on a real fabric VRC — PASS (2026-06-29)

**On-chip evolution of a logic circuit on a TRUE fabric substrate.** Unlike EHW-1.1-sw
(software LUT-grid eval), here the fitness is evaluated on `rtl/cgp_vrc.v` — the CGP
grid as config-loaded LUTs in real FPGA fabric, driven over MMIO. The evolved circuit
*is* hardware.

- Substrate: `rtl/cgp_vrc.v` (3×4 LUT4 grid, 12 config-loaded INIT regs, fixed
  column-to-column routing) wrapped as a DFX RM `rtl/dfx/tpu_rp_rm_cgp_vrc.v`
  (drop-in `tpu_rp`, XBUS window @0xF0000000). Built static+`rm_cgp_vrc` via
  `vivado/dfx/build_cgp_vrc.tcl` (cfg10/impl_10). Firmware `sw/ehw/cgp_vrc_mbox.c`
  runs the GA on NEORV32; **fitness eval writes the 12 INITs + applies the 16 inputs
  + reads the grid outputs over the cgp_vrc registers** (real fabric eval).
- Mailbox tags: `0xD8` main, `0xD9` gen+rows, `0xDA` fitness, `0xDC` done, `0xA0..0xAB`
  champion genome.
- Observed: `0xdc000010` (DONE 16/16 rows), `0xda000040` (64/64 bits), champion genome
  `aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0 4c00 8000` — **bit-identical to the
  host oracle** (12/12 words). Champion bit-exact to `tests/compare_cgp_vrc.py`.

### Gotcha caught on silicon (worth keeping)
- `rtl/cgp_vrc.v` passed the iverilog host gate but **failed Vivado synth**: three
  functions (`fitness_count`/`rows_count`/`active_count`) had **no input** (read
  module signals) — iverilog allows it, Vivado errors `[Synth 8-10738] function must
  have at least one input`. Fix = add a dummy `input` + pass `1'b0` (logic-neutral,
  host gate still 16/16). **Lesson: the iverilog host gate does NOT catch Vivado-synth
  strictness — add a quick OOC `synth_design` check (~1 min) to the RTL host gate
  before the full DFX build.** (Same class as the EHW-1.1-sw MBOX-address bug: the
  host gate has blind spots; the board build/run is the final gate.)

---

## EHW-1.2: ICAP-rewrite the evolved logic circuit's LUTs, live — PASS (2026-06-29)

**The chip rewrites its own evolved logic circuit, live.** A baked-CGP grid that
computes a *broken* 2-bit multiplier (7/16 rows) is transformed by ICAP — editing only
the 4 logic LUT INITs (n8..n11) — into a *perfect* multiplier (16/16), PS/NEORV32 never
reset. The CGP analogue of EHW-0.5.

- Built static+`rm_cgp_baked_base` (`build_cgp_baked.tcl`, impl_11); firmware
  `sw/ehw/cgp_baked_post.c` drives the baked grid over MMIO + publishes rows/fitness/
  marker. Champion edit = `cgp_baked_edit_champ.tcl` sets n8..n11 INITs (A0A0/6AC0/
  4C00/8000 = champion genome[8..11]) in the routed baseline DCP → 4-frame ICAP diff
  (28 set bits, FAR 0x004014a0..a3).
- Board: `fpga loadb` baseline → `0xe3000007` (rows 7), `0xe4000032` (fit 50),
  `0xe5475030` (marker CGP0) → PCAP_PR=0 → attest `0x13722093` → 4× `writeseq` →
  **`0xe3000010` (rows 16), `0xe4000040` (fit 64)** → marker stays `0xe5475030` (only
  the logic LUTs were edited, not the marker constant — honest: same bitstream, evolved
  logic ICAP-rewritten) → attest → PCAP_PR=1.

### Gotcha caught + FIXED on silicon (a real tooling bug)
- First bake corrupted the grid (rows 7→1, marker also flipped) — NOT the champion.
  Root cause = **`scripts/m75-build-frameseqs.py` frame-anchoring assigned the SAME
  start to two FARs** (a0 & a2 both 18629; a1 & a3 both 18730) because n8/n10 (and
  n9/n11) flip the *identical* INIT bit pattern, so the per-FAR pattern match took
  `starts[0]` for both → FAR a2/a3 wrote a0/a1's frame data to the wrong address.
- **Fix:** assign starts monotonically — frames lay out in increasing FAR order, so
  pick the first valid start **strictly greater than the previous FAR's**. After the
  fix the 4 frames anchor to **distinct** starts 18629/18730/18831/18932 (distinct
  ECCs) → re-bake → PASS. Board recovered between attempts by `fpga loadb` reload (and
  a `Ctrl-C` to clear a stuck `loady`). **Lesson: the M7.5.1 pattern-anchor is unsafe
  for small diffs where multiple frames share an identical bit-pattern; the monotonic-
  start fix makes it robust. Verify distinct per-FAR starts host-side before baking.**

---

## EHW-2: per-eval on-chip ICAPE2 LUT-INIT evolution — PARTIAL (mechanism runs; fidelity TBD) (2026-06-30)

**The hardest path: NEORV32 drives the fabric `xbus_icap` (ICAPE2) to rewrite a live
LUT-INIT every fitness eval — authentic Thompson live-bitstream evolution.** The
mechanism runs on silicon, but the LUT-edit result is not yet correct.

- Built PS7 + `neorv32_soc_icap` (`build_ehw2_icap.tcl`); 4 same-route INIT bitstreams
  (00/80/a8/e8). Firmware `sw/ehw/ehw2_icap_micro.c` reads a framebank from the AXI
  framebuf, streams each candidate's frame through `xbus_icap` (ICAP_FILL/WBURST),
  reads `lut_o`, scores vs target `0xe8`. Mailbox is on **AXI-GPIO ch2 = `0x41200008`**
  (`mbox_o`); ch1 `0x41200000` = `lut_o`.
- Frames: 4 candidate single-FAR envelopes extracted at FAR `0x00400d22` (the target
  LUT moved from `0x0040121a` to `0x00400d22` when the firmware IMEM changed → had to
  re-extract from the rebuilt bitstreams).
- Board: loadb → firmware `0xe8000000` (waiting) → PCAP_PR=0 → stage framebank
  (`ehw2-framebank-load.py`, magic word0 last) → firmware ran the per-eval ICAP loop →
  steady **`0xeb020520`** (best candidate 2=a8, fitness 5/8, observed mask `0x20`).
  **Expected `0xeb0308e8`** (candidate 3=e8, 8/8, mask `0xe8`). No wedge; board stayed
  responsive; PCAP_PR restored.
- **Diagnosis:** observed masks don't match the candidate INITs (different candidates
  give different masks, so the ICAP writes have *some* effect but don't land the INIT
  correctly). Almost certainly the known internal-ICAPE2 gotcha: DIN bit/byte ordering
  for ICAPE2-from-fabric differs from the PS-HWICAP `writeseq` envelope (raw-FDRI,
  no-GRESTORE), OR the `lut_o` 8-combo readout in firmware is off. Both are board-only
  (the host gate used fake fixed-length seqs + a stub eval, so neither the real frame
  format nor `lut_o` was covered). **Next: a debug round on `rtl/xbus_icap.v` data
  handling + the frame format (try DIN bit-reversal) and the `lut_o` readout.**

### Gotchas caught on silicon (3, all real)
1. **Forgot to bake the EHW-2 firmware into IMEM before the Vivado build** → bitstream
   ran the stale EHW-1.2 firmware. Fix = build `ehw2_icap_micro.c` → IMEM, rebuild.
2. **Mailbox was on a different GPIO channel** (`0x41200008`, not `0x41200000`) in this
   BD — read the wrong address for a while (saw all-zero `lut_o`).
3. **A mistaken PS-side `hwicap-uart readreg` (no PS HWICAP exists in the EHW-2 SoC)
   wrote to an unmapped AXI addr → wedged the PL-AXI interconnect** (later `md` data-
   aborted; SLCR-reset's openocd halt also failed). **Only a physical Type-C power-cycle
   recovered** (the one time this whole project needed it). After power-cycle: JTAG/DAP
   back, SLCR-reset → U-Boot, clean retry ran without wedge. **Lesson: do NOT poke the
   PS HWICAP path in an internal-ICAPE2 build; it has no PS HWICAP.**
