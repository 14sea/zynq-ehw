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

- status: superseded by EHW-0.3 board-resident GA; not part of the final milestone
  ladder
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
  - The board reports `gen=64` because after hitting the target the firmware breaks
    out of the GA loop and enters its steady publish loop using `EHW_GA_GENS` as
    the replay tag. It does **not** mean the search needed or ran all 64
    generations. The host reaches 40/40 at `gen 1`; the final champion is
    identical. Only difference from ChatGPT's predicted tags is this completion
    replay tag in `0xE9` (predicted `0x0128` gen1, observed `0x4028` gen64) —
    both `correct=40`, expected.
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

## EHW-2: per-eval on-chip ICAPE2 LUT-INIT evolution — PASS (HW-VERIFIED 2026-06-30)

**The hardest path: NEORV32 drives the fabric `xbus_icap` (ICAPE2) to rewrite a live
LUT-INIT every fitness eval — authentic Thompson live-bitstream evolution.** Final
result: the mechanism and the LUT-edit fidelity both pass on silicon.

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
- **First-run diagnosis:** observed masks didn't match the candidate INITs (different candidates
  give different masks, so the ICAP writes have *some* effect but don't land the INIT
  correctly). This initially looked like a possible internal-ICAPE2 DIN ordering or
  `lut_o` readout issue, but the host-side diagnosis below found the simpler root
  cause first.

**Post-run host diagnosis (2026-06-30):** before changing DIN ordering, the generated
framebank itself was found incomplete. All `cand/seq_*.seq.bin` files targeted only FAR
`0x00400d22`, but the placed LUT's low INIT bits span two FARs:

```text
80: bit_00400d23_100_06
a8: bit_00400d22_100_06, bit_00400d23_100_06, bit_00400d23_100_07
e8: bit_00400d22_100_06, bit_00400d23_100_06, bit_00400d23_100_07, bit_00400d23_100_14
```

So the partial result is consistent with a real ICAP write of a truncated phenotype,
not yet evidence that ICAPE2 DIN ordering is wrong. Fix prepared: 8KB framebank +
multi-sequence descriptors per candidate; rerun with `scripts/ehw2-build-framebank-from-bits.py`.

**RESOLVED — PASS (2026-06-30):** the half-phenotype diagnosis was correct; **NOT a DIN
bit-ordering issue.** With the 8KB framebank holding both FARs per candidate (the LUT INIT
spans `0x00000c22`+`0x00000c23` in the rebuild — it moves every build, so re-extract via
`ehw2-build-framebank-from-bits.py`), candidates a8/e8 now carry **two** envelopes (d22+d23,
each a proper target+pad frame). Rebuilt (8KB framebuf RTL + new descriptor firmware),
re-staged the multi-FAR framebank, ran the per-eval ICAP loop → steady **`0xeb0308e8`**
(candidate 3 = e8, fitness 8/8, observed mask `0xe8` = target) — exactly the PASS condition,
12× stable, no wedge, `lut_o` live. **This is authentic Thompson per-eval on-chip ICAPE2
evolution on real silicon: NEORV32 rewrote a live LUT-INIT through the fabric `xbus_icap`
every fitness evaluation and converged to the target.** Root cause: 7-series FDRI writes the
last frame of a burst as a non-committed pad, so a single FAR-d22 envelope set d22 but left
d23 uncommitted (truncated phenotype) — one envelope per spanned FAR fixes it.

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

## EHW-3.2 — spare-routing fabric VRC island (2026-07-01)

Firmware: `sw/ehw/spare_route_vrc_mbox.c` (POP=128), IMEM baked before the build.
Bitstream: `vivado/dfx/build_spare_route_vrc.tcl` → `build_sr/dfx.runs/impl_2/dfx_top.bit`
(static + `rm_spare_route_vrc`; impl_2 timing **met**, DRC **0 errors**, bitgen OK; the
549-cell island is the reconfigurable module behind the `0xF0000000` XBUS window).

Board bring-up: reset-to-U-Boot (SLCR `0xF8000008=0xDF0D` + `0xF8000200=1` while
`uboot-intercept.py` hammers `d`) → `uboot-fpga-load.py --op loadb dfx_top.bit`
(part `7z010clg400`, design `dfx_top` 2026/07/01 21:01:17, 2083740 bytes) → poll the
mailbox at PS `0x41200000` with `host/ehw_watch.py` (firmware writes PL `0xF1000000`
→ AXI-GPIO → PS `0x41200000`).

The firmware republish loop was extended (board-instrumentation change) to emit the
FULL result set in steady state, so a slow U-Boot `md` poll captures the whole
fault→recovery narrative, not just the repair endpoint. Observed mailbox words (steady,
two polls, identical), decoded against the firmware tag scheme `0xE3<tag>0000 | value`:

| word | tag | decode |
|---|---|---|
| `0xe32100e8` | E321 nofault_mask | `0xe8` = target 3-input majority |
| `0xe3220008` | E322 nofault_fit  | `8` → **8/8** (no fault) |
| `0xe32300c8` | E323 degraded_mask| `0xc8` under injected `FAULT_DISABLE_NODE(A1)` |
| `0xe3240007` | E324 degraded_fit | `7` → **7/8** — the fault genuinely degrades it |
| `0xe32500e8` | E325 repair_mask  | `0xe8` = target, recovered |
| `0xe3260008` | E326 repair_fit   | `8` → **8/8** |
| `0xe3270002` | E327 uses         | `2` (bit1=AS) → output routes the **spare node AS** |
| `0xe3280001` | E328 heartbeat    | republish loop alive |

**Result: on the REAL register-configured fabric VRC island, no-fault majority evaluates
to `0xe8` = 8/8; injecting `FAULT_DISABLE_NODE(A1)` degrades it to `0xc8` = 7/8; the
on-board-evolved repaired genome recovers `0xe8` = 8/8 with the output mux routing the
spare node AS. The complete fault→recovery narrative is confirmed on silicon and matches
the POP=128 host/RTL model exactly** (`docs/ehw3_2_results.md`).

The no-fault, degraded, and repair values are all captured in steady state because the
board republish loop now re-emits E321–E327 (not just the repair endpoint); the earlier
run had only the repair endpoint in the loop (the one-shot startup detail is ~40 µs/word,
uncatchable by slow `md` polling).

No wedge; DEVCFG healthy; JTAG/UART stable throughout. `zynq_xpart`/`zynq_agentctl`
untouched.

## EHW-3.3 — ICAP-baked spare-route repair (2026-07-01)

Firmware: `sw/ehw/spare_route_baked_post.c` (POST loop, re-reads the baked island and
publishes marker/mask/fitness), IMEM baked before the build. Bitstream:
`vivado/dfx/build_spare_route_baked.tcl` → impl_33 static + `rm_spare_route_baked_base`
(SRB0, timing met / DRC 0-err / bitgen OK). Same-route repaired bitstream:
`vivado/dfx/spare_route_baked_edit_repair.tcl` edited only the 11 target LUT/select INITs
(g0-g5,g7,g8,g11,g13,g14) on the routed baseline DCP — the marker register is intentionally
NOT edited.

Frame extraction (fresh bitstreams, prjxray part `xc7z010clg400-1`): `bitread -y` of
baseline vs repair → 144 set + 112 clr bits across **8 FARs** (0x40149a-0x40149d,
0x4014a0-0x4014a3) → `scripts/m75-build-frameseqs.py` → **8 envelopes, 8/8 self-checked**,
monotonic starts (341023 … 341932), one sync..DESYNC envelope per FAR.

Board: reset-to-U-Boot → `fpga loadb` baseline → set `PCAP_PR=0` (`mw 0xF8007000
0x4400e07f`) → `hwicap-uart.py readreg 12` = `0x13722093` (ICAP healthy) → **background**
`writeseq` of all 8 envelopes (each 233 words, SR=0x5 / CR=0, clean) → restore `PCAP_PR=1`
(`mw 0xF8007000 0x4c00e07f`). No PS/NEORV32 reset at any point.

Observed mailbox (`0xE331` marker-low24 / `0xE332` mask / `0xE333` fitness), steady, two
polls each:

| stage | marker word | mask word | fitness word | decode |
|---|---|---|---|---|
| baseline (broken) | `0xe3734230` | `0xe33200c8` | `0xe3330007` | SRB0, mask c8, **7/8** |
| after ICAP (repaired) | `0xe3734230` | `0xe33200e8` | `0xe3330008` | **SRB0 (unchanged)**, mask e8, **8/8** |

**Result: the live baked spare-route island was rewritten from the fault-degraded
phenotype (mask c8, 7/8) to the repaired phenotype (mask e8, 8/8) by an ICAP LUT-INIT
edit of only the 11 target INITs — the marker stayed `SRB0`, proving no RM reload, and
there was no PS/NEORV32 reset. This is the CGP-analogue of EHW-1.2 for the spare-routing
island: live ICAP self-repair of a fixed-route island, confirmed on silicon.** No DEVCFG
wedge; all 8 frames baked cleanly in the background; sibling projects untouched.
