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
    hardware-verified. The 300000-count settle was sufficient for `rm1_tpu`. **[2026-07-02: later shown unnecessary — the "post-config settle" requirement inherited from xpart M7.1 was an artifact of the image_gen bug (zynq-xpart docs/m7_2_dcpdiff.md FOLLOW-UP); a zero-settle cold start is bit-exact on a correct image. Kept in the firmware as a harmless ~6 ms delay to preserve the board-verified binary.]**

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

## EHW-3.4 — per-eval internal-ICAPE2 spare-route evolution (2026-07-02)

Firmware: `sw/ehw/ehw34_icap_spare_route.c` (internal-ICAPE2, **NO PS-HWICAP**), IMEM
baked before the build. SoC `rtl/neorv32_soc_icap_sr.vhd` (MBOX 0xF1000, xbus_icap
0xF3000, baked island 0xF4000, frame BRAM 0xF5000; framebuf `rtl/axil_framebuf.vhd`
parameterized to ADDR_BITS=14 = **16384 words / 64KB** for this build). Bitstreams:
`vivado/icap_ehw34/build_ehw34_icap.tcl` — one route, 4 same-route bitstreams
(base/logic/route/repair via apply_genome). Rebuild after the framebuf enlargement:
timing **met** (WNS +6.83 ns), DRC **0 errors**, **BRAM 37/60** (the 64KB framebuf
fits with headroom).

Framebank: prjxray `bitread -y` of the 4 fresh bitstreams →
`scripts/ehw34-build-framebank-from-bits.py` → **used 5278 words, padded to 16384**
(cand frame counts base=0, logic=6, route=8, repair=8; the fresh build placed the LUTs
in fewer FARs than the earlier 2048-word attempt).

Board: reset-to-U-Boot → `fpga loadb` base → set `PCAP_PR=0` (`mw 0xF8007000
0x4400e07f`) → stage framebank to the framebuf AXI window `0x40000000` with
`scripts/ehw2-framebank-load.py` (word0 written last as the trigger; readback
confirmed word0=`0x45483334` "EH34", word1=`4` candidates, word4=`0x0a08010f`) →
NEORV32 runs the per-eval ICAPE2 loop → restore `PCAP_PR=1`. **No PS-HWICAP
readreg/writeseq was issued** (this build has none; doing so would wedge PL-AXI).

**GOTCHA (cost a confusing poll): the mailbox is on AXI-GPIO channel 2 = `0x41200008`
(`soc_0/mbox_o → gpio2_io_i`); channel 1 `0x41200000` carries `lut_o` (read a red-herring
`0x00000001`). `host/ehw_watch.py` hardcodes `0x41200000`, so read `0x41200008` directly.**
(Same ch2 convention as EHW-2.)

Observed mailbox `0x41200008`, steady, 25/25 then 12/12 reads (incl. after PCAP_PR
restore): **`0xec0308e8`** = firmware steady endpoint `0xEC | best_idx=03 | fit=08 |
mask=e8` → best candidate = index 3 (**repair**), **fitness 8/8, mask 0xe8**. (The
one-shot converge word `0xEA0308E8` flashed by before polling; the steady loop republishes
`0xEC0308E8` with the same result.)

**Result: authentic per-eval on-chip ICAPE2 evolution over the EHW-3 spare-routing
substrate, on real silicon — the NEORV32 rewrote the live baked island through the fabric
`xbus_icap` for each candidate evaluation and converged to the repair genome (8/8, mask
0xe8). This is the Thompson-style live-bitstream analogue of EHW-2, now on the spare-route
island. The whole EHW-3.0→3.4 ladder is board-verified.** No DEVCFG wedge; sibling
projects untouched.

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

## EHW-4.3 — memetic train-unit on-board smoke (2026-07-03)

- Build: `vivado/dfx/m43_add_memetic.tcl` adds `rm_memetic_train` (cfg10/impl_10)
  to the existing project; static IMEM = `memetic_train_mbox.c` (VHD 3880 B,
  `verify-image` OK). impl_1 + impl_10 `write_bitstream Complete!`.
- Resource gate (Claude-side, mandatory): OOC synth 18 DSP48E1 total; **place-level
  RP utilization 18/20 DSPs (90.0%)** — the v1 blocker (48 DSP) confirmed fixed in
  silicon flow, unit itself uses 2 (the two err² squares).
- On EBAZ4205 via `fpga loadb` of `impl_10/dfx_top.bit`: mailbox `0x41200000`
  settled at **`0xF4F00000`** (14/14 samples) = firmware's final verdict word:
  RTL full-40-sample epoch trace vs `mem_adapt()` golden — **mism == 0 AND
  gold_sse == got_sse, bit-exact on silicon**. (Fail word would be `0xF4F00001`.)
- EHW-4.3 smoke: **PASS**. The HW memetic train unit is board-verified; next rung
  (EHW-4.4+) can wire it into the GA adaptation inner loop.

## EHW-4.4 — Lamarckian GA × HW train-unit on-board (2026-07-03)

- Firmware `memetic_ga_train_mbox.c` (VHD 4096 B, verify-image OK, .bss 2560 B
  population arrays within the 16 KB budget): full Lamarckian GA — POP=16,
  GENS=8, every candidate's fitness eval runs 1 HW-SGD adaptation epoch through
  the EHW-4.3-verified train-unit MMIO window at 0xF0000800, adapted weights
  written back to the genome.
- Same rm_memetic_train bitstream lineage (cfg10/impl_10 rebuilt with the new
  static IMEM; RP still 18/20 DSPs).
- On EBAZ4205 via `fpga loadb`: mailbox `0x41200000` settled at **`0xF4F00028`**
  (14/14 samples) — final steady word, low byte 0x28 = **best_correct 40/40**,
  matching the host twin exactly (host: 40/40, first_40=gen 3, SSE 6116; the
  curve gate `compare_memetic_ga_train.py` is byte-exact vs `memetic_eval`).
- **EHW-4.4 PASS — the GA × HW-SGD memetic inner loop is now a silicon fact:**
  evolution (GA selection/crossover/mutation on NEORV32) and gradient descent
  (loss/leaky'/SGD in fabric hardware) running fused, on-chip, in one power-on.

## EHW-4.5 — same-boot Baldwinian vs Lamarckian A/B on-board (2026-07-03)

- Firmware `memetic_ab_train_mbox.c` (VHD 4232 B, verify-image OK): ONE image, ONE
  boot — Baldwinian arm (adaptation for fitness only, no writeback) runs first,
  then Lamarckian (writeback), POP=16 GENS=32 adapt_epochs=1, shared seeding; the
  only variable between arms is writeback semantics. Both arms drive the
  EHW-4.3/4.4-verified train-unit MMIO window (0xF0000800).
- Same rm_memetic_train lineage (impl_1+impl_10 rebuilt, `write_bitstream Complete!` ×2).
- On EBAZ4205 via `fpga loadb`, mailbox `0x41200000`:
  - live arm checkpoints observed: `0xF5300027`, `0xF53F0025` (Baldwinian),
    `0xF6380028`, `0xF63E0025` (Lamarckian);
  - **final steady `0xF7F02828` (6/6 tail samples) = Baldwinian best 0x28 (40/40)
    AND Lamarckian best 0x28 (40/40)** — matching the byte-exact host twin
    (Baldwinian first_40 = gen 29, SSE 4678; Lamarckian first_40 = gen 3, SSE 6116).
- **EHW-4.5 PASS.** The A/B answers EHW-4's core scientific question on silicon:
  with 1-epoch HW-SGD adaptation, acquired-weight inheritance (Lamarckian)
  converges ~10× earlier (gen 3 vs gen 29) at the cost of higher final SSE
  (6116 vs 4678, saturation pressure), while pure learnability selection
  (Baldwinian) converges slower but to a lower-SSE genome — both on one boot,
  same silicon, same seeds.

## EHW-4.6a — compile-time memetic parameter sweep on-board (2026-07-03)

- Firmware `memetic_sweep_mbox.c` (VHD 4792 B, verify-image OK, .bss 6080 B in
  budget): ONE image bakes a 12-point parameter table (population/generations/
  adapt_epochs/seed), runs Baldwinian AND Lamarckian per point sequentially in
  one boot, then carousels 48 packed result words (0xF8 = point|mode|first_40|
  correct, 0xF9 = point|mode|SSE16).
- Same rm_memetic_train lineage (impl_1+impl_10 rebuilt).
- On EBAZ4205 via `fpga loadb`, ~2 min sweep, then paced mailbox sampling:
  **48/48 distinct carousel words collected, every one bit-identical to the
  host-golden `runs/tests/ehw4_6a_fw_sweep.csv` encoding — 24/24 (point, mode)
  rows, ZERO mismatches.** (16/24 rows reach 40/40, matching host.)
- **EHW-4.6a PASS.** The citable sweep table (convergence vs POP/GENS/
  adapt-budget for both inheritance modes) is now silicon-backed in one build +
  one boot — no per-point rebuilds. 4.6b (PS-writable params via the proven
  `axil_framebuf` window) remains the optional interactive upgrade.

## EHW-4.6b — PS-writable parameter window, board-verified (2026-07-03)

- Static change (Claude lane): `axil_framebuf` (proven EHW-2/3 component,
  ADDR_BITS=11) added to the ps BD as a module reference @AXI `0x40000000`
  (8 KB); read port exported through `dfx_top` into `neorv32_soc_dfx`'s new
  XBUS window @`0xF5000000` (1-cycle registered ack). `build_dfx.tcl` (from
  scratch) + `m46b_add_framebuf.tcl` (incremental) both carry it; impl_1 +
  impl_10 rebuilt clean.
- Probe firmware `sw/ehw/fb_probe.c` (VHD 2448 B, verify-image OK).
- On EBAZ4205:
  1. PS AXI self-test: `mw 0x40000000 0x123456; mw +4 0xABCDEF` → `md` reads
     back both — window write+readback OK;
  2. NEORV32 side: mailbox carousels **`0xFB123456` / `0xFCABCDEF`** — the
     soft-core reads exactly what the PS staged;
  3. **live update**: `mw 0x40000000 0x777001` with firmware running → mailbox
     flips to **`0xFB777001`** within one carousel period, word1 untouched —
     no reboot, no reload.
- **EHW-4.6b PASS.** Interactive parameter staging is ready: future sweeps /
  EHW-5 firmware read params from `0xF5000000` instead of a baked table; one
  bitstream, unlimited re-parameterization. (One power-cycle was needed before
  this run — board wedged during a failed first load attempt, cause consistent
  with a CH340-brownout-missed intercept; recovered by Type-C replug, standard.)

## EHW-5.2 — combined VRC + lite-train-unit RM: board FAIL; static/build lineage SUSPECTED, clean repro pending (2026-07-04)

Status: **board FAIL, not root-caused.** The control experiment below shifts
suspicion from the RM RTL toward the static/build side, but does NOT convict
fb_0 specifically — see the confounds paragraph. No board claim for 5.2.

Board result (deterministic across reloads + a full power-cycle):
- `impl_12` (combined RM, firmware `memetic_struct_train_mbox.c`) → mailbox
  `0xF5F00001` FAIL; the HW-train-unit arm diverged from the CPU golden
  (mism=2 genome bytes, got_sse 4611 vs gold 4560). VRC island itself PERFECT
  (marker "SRV0", mask 0xa0); board CPU-golden path bit-exact to host stub.

Control experiment (workflow rule #6 — same-firmware cross-build):
- Rebuilt the **proven EHW-4.3** config on the **current** static:
  `rm_memetic_train` RTL unchanged since 4.2 (`440f6ee`), same 4.3 firmware
  (`verify-image` OK), reused RM netlist (`rm_memetic_train_synth_1` dated
  2026-07-03 05:41, predates the rebuild). Expected `0xF4F00000` (14/14
  bit-exact, as board-verified at `7901cc0`). **Got `0xF4F00001` FAIL,
  12–13/13 reads.**
- **User power-cycled the whole board; reloaded the same 4.3 `impl_10` unchanged
  → still 12/12 FAIL. Physical board ruled out.**

What this establishes vs what it doesn't:
| layer | verdict | evidence |
|---|---|---|
| physical board | exonerated | full power-cycle → same deterministic FAIL |
| 4.3 RM RTL | likely exonerated | RTL unchanged since 4.2; post-synth + post-route funcsim (RM cell + ideal XBUS tb) PASS; same netlist previously board-PASS |
| 5.2 RM RTL | suspicion reduced, NOT cleared | its divergence (mism=2) is a separate observation; only the 4.3 RM was cross-checked |
| **current static/build lineage** | **prime suspect** | a previously board-verified config now reproducibly FAILs on it |
| fb_0 specifically | **suspected, unproven** | see confounds |

Confounds that block a "root-caused to fb_0" verdict:
1. The control ran in the **dirty incremental live project**, where stale-RM
   netlist relinking has already been caught once this session (the
   `m52_add_struct.tcl` reset_run fix exists because of it).
2. The control rebuilt static `impl_1` from scratch (2026-07-03 23:11) — vs the
   4.3-era PASS static, the delta is fb_0 **plus a wholesale re-place/re-route**.
   fb_0-the-module and rebuild placement variance are not separated.
3. The clean-workspace repro (`/home/test/ehw52_clean/prepare_clean_52.sh`) was
   written but **never executed** — no `ws/`, no `manifest.txt`, no build
   products exist. Until it runs and its artifacts persist, this section stays
   "suspected".

Still-plausible mechanism (hypothesis only): `6d2fada` added `or fb_ack` to the
`xbus_ack` aggregation, shifting real XBUS ack timing so the RM's
`tu_pending`/`sr_pending` handshake goes borderline under the real NEORV32 bus
cadence (the blind spot flagged in `review.v1.txt`: RM handshake never verified
at real bus cadence, only against an ideal tb).

Ownership + next steps: fb_0 is a Claude solo-lane change (`6d2fada`); Claude
drives the repro. Decision order: (1) clean DFX build **with** fb_0 at `465b9c7`
— if it PASSes, the dirty project was the fault, fb_0 walks; (2) clean build
**without** fb_0 (prepare_clean_52.sh as written) — only the pair attributes
blame; (3) board retest; workspace + manifest must persist for re-audit.
Note: the shared-lane RM (`465b9c7`) already routed in the RP on the live
project — `impl12_rp_util.rpt` 2026-07-03 22:57, Slice LUTs 3355/4400 (76.25%),
resolving the `a512e8b` OOC HOLD (LUT 5296 → shared lane). Build-infra fix
found in passing: `m52_add_struct.tcl` must `reset_run rm_memetic_struct_synth_1`
or impl_12 relinks a stale RM netlist.

## [SUPERSEDED — RM later exonerated; real culprit = FCLK0 signoff mismatch, see root-cause section below] EHW-5.2 clean-repro legs A/B/C: "5.2 RM convicted", fb_0 + static + dirty-project exonerated (2026-07-04)

> **2026-07-04 later the same day:** the "RM convicted at the physical level"
> verdict below was itself overturned — the placement-lottery fingerprint was
> real, but the physics was the PL running at 125 MHz against a 50 MHz signoff
> (miner-FSBL FCLK0), not an RM race. The A/B/C eliminations of fb_0, the
> static, and the dirty project remain valid. Kept unedited below for the
> honest evidence trail.

The pending clean repro above was executed same day as a THREE-leg matrix.
Every leg: fresh workspace from `git archive` of tracked sources at `465b9c7`,
own NEORV32 copy, firmware baked from tracked sources with `verify-image OK`,
manifest persisted, full Vivado build from nothing (synth_1 → impl_1 → impl_N),
timing met, then `fpga loadb` + carousel sampling at PS `0x41200000`.
Workspaces + manifests + reports persist under `/home/test/ehw52_clean/`
(`ws_withfb`/`ws_nofb`/`ws_43ctrl`, `manifest_*.txt`, `clean_build_*.log`,
`impl1*_*_{rp_util,timing}.rpt`); manifests also copied to
`docs/evidence_ehw52/` in-repo.

| leg | static | RM / firmware | build | board verdict |
|---|---|---|---|---|
| A `ws_withfb` | clean, fb_0 KEPT | 5.2 `rm_memetic_struct` + struct fw | WNS +0.869, LUT 3355/4400 | **FAIL** `0xF5F00001` — mism=1, correct=38, got_sse 4556 vs gold 4560 (−4) |
| B `ws_nofb` | clean, fb_0 stripped (pre-`6d2fada` SoC/top) | 5.2 same | WNS +0.776, LUT 3354/4400 | **FAIL** `0xF5F00001` — mism=7, correct=20, got_sse 4955 vs gold 4560 (+395; delta word `f562fe75` = −395 self-consistent) |
| C `ws_43ctrl` | clean, fb_0 stripped | **proven 4.3** `rm_memetic_train` + 4.3 fw (both byte-identical to `7901cc0`, verified by git diff) | WNS +0.166 | **PASS** `0xF4F00000`, 30/30 reads |

All legs' VRC island evidence stayed perfect where applicable (marker "SRV0",
mask 0xa0); the CPU-golden path matched host throughout.

Verdicts, each now backed by a persisted artifact chain:
- **Dirty-project 4.3 control FAIL (2026-07-03) was an ARTIFACT.** Leg C: the
  same proven config passes on a *freshly re-placed* clean static. The previous
  section's "static/build lineage prime suspect" is withdrawn.
- **fb_0 EXONERATED** (A vs B: both fail, stripping fb_0 made it *worse*).
  4.6b remains a board-verified feature.
- **Static re-place is safe** for a known-good RM (leg C is a brand-new
  placement and passes).
- **The 5.2 combined RM (`rm_memetic_struct` + lite TU) is CONVICTED at the
  physical level.** It fails on every clean build, with placement-dependent
  divergence magnitude across four independent builds/placements:
  mism=2/+51 (dirty impl_12, ab53136-era RM) → mism=1/−4 (leg A) →
  mism=7/+395 (leg B), each perfectly deterministic *within* a build.
  Timing is formally met everywhere (WNS ≥ +0.166), and the full-epoch RTL
  replay gate (a512e8b) passes at ideal cadence — so this is not a plain
  logic bug and not a reported timing violation: the profile fits an
  unconstrained/race path or real-bus-cadence handshake marginality inside
  (or at the boundary of) the lite train-unit arm. Caveat: the dirty-run RM
  predates the shared-lane rework, so the RM-internal *location* may have
  moved between runs; the *class* (placement-sensitive, TU-arm-only) is
  consistent.

Handoff: evidence package + next-probe proposal (post-synth funcsim of the
epoch replay on the RM netlist — attacks synth semantics; then
cadence-accurate bus tb) in `review.v4.txt`. RM-side diagnosis/fix is
ChatGPT's lane per workflow; board + build gates stay here.

## EHW-5.2 ROOT CAUSE CONFIRMED ON SILICON: FCLK0 signoff mismatch, 125 MHz vs 50 MHz (2026-07-04)

Continuation of the A/B/C matrix, same day. After ChatGPT's held-rdata RM fix
(`a327a9f`) passed host gates + my OOC gate (LUT 3455, DSP 18/20) + a clean
build (`ws_fix`, WNS +1.026, best yet) but STILL failed on board
(mism=14, got_sse 4738 vs 4560), the elimination went one layer deeper:

1. **P1 post-synth funcsim**: full-epoch replay tb vs the a327a9f OOC netlist
   (iverilog + unisims) → PASS. Synthesis semantics exonerated.
2. **P2a post-route funcsim**: same tb vs the *routed RM cell netlist from the
   very build that failed on board* (`u_soc_wb_tpu_inst_rm_memetic_struct_routed.dcp`)
   → PASS. Implementation netlist transforms exonerated.
3. **check_timing on the routed design**: all categories zero, single clock
   `clk_fpga_0` 20 ns. Nothing unconstrained.
4. Contradiction spotted: a *functional* bus-cadence bug would diverge
   identically across placements (NEORV32 cadence is cycle-deterministic),
   but the four builds diverged differently (+51/−4/+395/+178). That profile
   demands physics — yet timing was "met"… **unless the signoff clock is not
   the real clock.**
5. **SLCR read from U-Boot**: `md 0xF8000170` → `0x00200400` =
   IOPLL(1000 MHz)/4/2 = **FCLK0 = 125 MHz**. We never run our FSBL; the miner
   FSBL's 125 MHz has been driving every PL design in this project's history.
   Signoff is 50 MHz. The PL has been running 2.5× overclocked all along.
6. **A/B on silicon, both directions, same bitstreams** (SLCR unlock +
   `mw 0xF8000170 0x00200A00` = 50 MHz, reload, poll):
   - `ws_fix` (a327a9f): FAIL@125 (mism=14, +178) → **PASS@50**
     (`0xF5F00000`, mism=0, got_sse=gold_sse=4560, correct=38, delta word 0).
   - `ws_withfb` (465b9c7, pre-fix control): FAIL@125 (mism=1, −4) →
     **PASS@50**, identical evidence words.

Verdict revisions this forces (honesty pass on my own earlier sections):
- "5.2 RM convicted at the physical level" → the RM was the *victim* with the
  longest paths, not the culprit. No RM bug existed. The a327a9f held-rdata
  change is kept as hygiene (better WNS, stricter bus protocol), not as a fix.
- The 2026-07-03 dirty-project 4.3-control FAIL: explained by the same clock
  lottery (that rebuild's placement had ≥1 path in the 8–20 ns band; leg C's
  placement didn't). "Stale-netlist artifact" is withdrawn as the explanation;
  the `reset_run` build-hygiene fix (`933935f`) stays on its own merits.
- Leg A vs leg B divergence difference (mism 1 vs 7) was placement lottery,
  not an fb_0 effect. fb_0 remains exonerated.
- All prior board-verified milestones stand: they were bit-exact vs golden,
  i.e. genuinely correct at 125 MHz (short-path luck, now understood).

New mandatory recipe (also in hw_notes.md): pin FCLK0 to 50 MHz from U-Boot
before every loadb session; NAND boot/power-cycle restores 125 MHz.

Board state at close: U-Boot, FCLK0 = 50 MHz, PL = ws_withfb impl_12 PASSING.
Evidence preserved: /home/test/ehw52_clean/{ws_withfb,ws_nofb,ws_43ctrl,ws_fix}
+ manifests + build logs + check_timing/clocks rpts (ws_fix_check_timing.rpt,
ws_fix_clocks.rpt); funcsim runs under zynq_ehw/runs/tests/.

**EHW-5.2 board leg: PASS `0xF5F00000` (a327a9f, clean static with fb_0,
FCLK0=50 MHz) — mism=0, got_sse=gold_sse=4560, correct=38, marker "SRV0",
mask 0xa0. The combined spare-route-VRC + lite-train-unit RM is board-verified.**

## scripts/board-set-fclk50.py — full-path board verification (2026-07-04)

The mandatory pre-loadb tool (ChatGPT-authored at `3059658`, untestable on its
side) was exercised on the real board through its mutation path, not just the
verify path: FCLK0 was deliberately reset to the miner default
(`mw 0xF8000170 0x00200400` = 125 MHz), then the script detected it
(`before FPGA0_CLK_CTRL=0x00200400`), rewrote and verified
(`after FPGA0_CLK_CTRL=0x00200a00`, `PASS: FCLK0 pinned to 50 MHz`), exit 0.
Its opening bare-CR sync also absorbs the residual-`d` intercept gotcha.
The running PL design was reloaded afterwards (live FCLK divisor changes
glitch the PL clock — treat any design state from before the switch as
invalid); mailbox re-confirmed `0xF5F00000` PASS. Tool is board-verified.

## EHW-5.3 board hybrid memetic loop — PASS, first roll (2026-07-04)

**The full hybrid structure+weight memetic GA ran on-chip and matched host
golden on every acceptance field.** Single arm per docs/ehw5_3_task.md:
`hybrid_lamarckian_pressure` / `bias_x3`, seed 3, POP 16, GENS 32, ADAPT 1.
Firmware `sw/ehw/memetic_struct_ga_mbox.c` (ChatGPT, `1ec6ea1`): per-candidate
fabric evaluation — sr[16] into the VRC window `0xF0000400`, per-sample
`SR_INPUT`→`SR_OUTPUT` phi with `bias_x3` coupling, weight genome + one HW-SGD
epoch through the lite train-unit `0xF0000800`, Lamarckian writeback,
feature-balance pressure in selection.

Pre-board gates (all rerun by Claude, not taken on trust): host gates 18/18
incl. the new byte-exact curve gate `compare_memetic_struct_ga_train.py`;
isolated firmware build exe 5580 B, data+bss 3648 B « 16 KiB DMEM,
`verify-image OK`; clean workspace `ws_53` from tracked sources at `1ec6ea1`
(manifest_53.txt, IMEM md5 bd3f9f98…); full clean DFX build impl_1+impl_12,
WNS +1.026, RP LUT 3348/4400, 0 errors. RM untouched (board-verified 5.2
netlist lineage rebuilt from identical sources).

Board flow (the now-standard recipe): `board-set-fclk50.py` preflight →
`before/after FPGA0_CLK_CTRL=0x00200a00, PASS` → `fpga loadb` → carousel at
PS `0x41200000`, 60 samples over ~2.5 min, five distinct words, all steady:

| word | decode | host golden |
|---|---|---|
| `0xf5302028` | gen=32 (completion replay tag), best_correct=40 | 40/40 ✓ |
| `0xf53111a1` | best_sse=4513 | 4513 ✓ |
| `0xf5320f00` | feature_ones=15, penalty_bucket=0 | 15 / 0 ✓ |
| `0xf53f0002` | first_40=2 | 2 ✓ |
| `0xf5f30000` | final verdict PASS | ✓ |

Acceptance per ehw5_3_task.md: host-stub curve byte-exact ✓ (gate),
board == host-golden summary ✓, FCLK0=50 MHz captured in-session ✓,
exact words recorded here ✓. Same-set deployment/adaptation metric as the
whole EHW-5 line — no holdout generalization claim.

First-roll pass with zero board debugging: the 5.2 root-cause work (FCLK0
pinning + verified RM + preflight tooling) did exactly what it was for.

## EHW-5.4a same-boot hybrid ablation — PASS, first roll (2026-07-05)

**The structural-contribution ablation ran as one image / one boot / one seed
and every arm matched host golden.** Four arms per docs/ehw5_4_task.md
(seed 3, POP 16, GENS 32, ADAPT 1), firmware
`sw/ehw/memetic_struct_ab_mbox.c` (ChatGPT, `2a0481a`), shared population
buffers across arms (data+bss 6240 B), RM untouched (5.2/5.3 lineage).

Pre-board gates rerun by Claude: host gates 19/19 incl. the four-arm
byte-exact curve gate `compare_memetic_struct_ab_train.py`; oracle/eval diff
audited (weight-only curve emission is additive, GA semantics untouched);
isolated build exe 7472 B, `verify-image OK`; clean workspace `ws_54` at
`2a0481a` (manifest_54.txt), full DFX build WNS +1.026, 0 errors;
`board-set-fclk50.py` preflight `0x00200a00` in-session.

Steady carousel (70 samples, all 18 expected words, no strays):

| arm | mode/coupling | correct | SSE | first_40 | ones/penalty |
|---|---|---|---|---|---|
| 0 | weight_only_lamarckian/none | `f5400028`=40 | `f55017e4`=6116 | `f5600003`=3 | `f5700000`=0/0 |
| 1 | hybrid_lamarckian_pressure/bias_x3 | `f5400128`=40 | `f55111a1`=4513 | `f5600102`=2 | `f5710f00`=15/0 |
| 2 | hybrid_no_adapt/gate_x3 | `f5400228`=40 | `f5521207`=4615 | `f560020b`=11 | `f5722700`=39/0 |
| 3 | hybrid_lamarckian/bias_x3 | `f5400328`=40 | `f55316cd`=5837 | `f5600305`=5 | `f5730000`=0/0 |

plus `f54f0004` (arm count 4) and `f5f40000` (final PASS). Per-arm heartbeats
(`0xF51x/0xF52x`) observed during the run (arm0 hit 40/40 within the first
poll window).

Same-boot science readout (same-set deployment/adaptation metric, no holdout
claim), all three now cross-build-confound-free:
- structure+pressure improves both convergence and SSE (arm1 vs arm0:
  first_40 2 vs 3, SSE 4513 vs 6116);
- HW-SGD adaptation drives convergence speed (arm1 vs arm2: first_40 2 vs 11);
- the pressure term prevents feature degeneration and yields the best SSE
  (arm1 vs arm3: feature_ones 15 vs 0, SSE 4513 vs 5837).

Acceptance per ehw5_4_task.md: all met. Per the task's stop rule, EHW-5.4a
passing means the EHW-5 line is strong enough to close; 5.4b (param-window
scan) and 5.5 (ICAP reveal) remain optional.

## EHW-5.4b param-window staging — PASS, both legs (2026-07-05)

**A single bitstream re-parameterized from the PS without rebuild or reload.**
Firmware `memetic_struct_ab_mbox.c` at `8e28a77` reads the 4.6b window once at
boot: magic `0xE5400001` present → staged table; absent → built-in 5.4a table;
present-but-invalid → explicit FAIL. Clean build `ws_54b` (WNS +0.281), host
gates 21/21, verify-image OK (exe 8220 B, bss 6336 B), FCLK0 preflight
`0x00200a00` in-session.

Leg 1 — built-in (no magic): all 19 expected words steady — the full 5.4a
18-word set verbatim (all four arms == host golden) plus source word
`f54e0001` (staged=0, valid=1).

Leg 2 — staged short scan (single arm `hybrid_lamarckian_pressure/bias_x3`,
GENS=4, the exact block the host gate validated): staged 9 words to
`0x40000000`, verified by `md`, then **restarted the NEORV32 without touching
the bitstream** via a PL logic reset — SLCR unlock + `mw 0xF8000240 1` /
`mw 0xF8000240 0` (FPGA_RST_CTRL pulses FCLK_RESET0_N → `rst_0`; BRAM contents
incl. IMEM and the staged fb_0 block survive, only logic resets). Steady
carousel, all 7 expected words:
`f5400028` (arm0 40/40), `f55011a1` (SSE 4513), `f5600002` (first_40=2),
`f5700f00` (ones=15, penalty=0), `f54e0101` (staged=1, valid=1),
`f54f0001` (arm count 1), `f5f40000` (PASS) — matching the host-gate golden
for the staged block (converges to the same summary as the full run by gen 2;
the gate separately proves the 4-gen curve differs byte-wise from the 32-gen
curve, so the parameter change demonstrably changed the run).

Acceptance (ehw5_4_task.md 5.4b): PS staged at `0x40000000` ✓, NEORV32 read
via `0xF5000000` ✓, staged parameters changed the subsequent run without
rebuilding or reloading the bitstream ✓.

### Board gotchas caught this leg (worth keeping)
1. **`ehw2-framebank-load.py` is big-endian-word oriented** (framebank format).
   Staging the little-endian `ehw54-param-pack.py` image through it byte-swaps
   every word — the window ended up with `0x010040E5` instead of the magic.
   For ≤~16-word param blocks, stage with direct U-Boot `mw` per word (done
   here); for bulk use, the loader needs an LE mode or the pack tool a BE flag.
2. **The restart step was undocumented**: the firmware reads the window once at
   boot, and a full `fpga loadb` would wipe the staged BRAM back to zeros. The
   FPGA_RST_CTRL logic-reset (above) is the correct — and now board-verified —
   mechanism, satisfying the "no rebuild, no reload" acceptance clause.
