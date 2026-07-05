# Evolvable Hardware on a recycled Zynq-7010 — a writeup

A short, honest report of what `zynq_ehw` does, what it does not claim, how it
relates to prior work, how to reproduce it, and where it broke.

## 1. Problem

Evolvable Hardware (EHW) applies an evolutionary search to *hardware*: the circuit
configuration (or a subset) is the genotype; candidates are evaluated for fitness and
selected/mutated. **Intrinsic** EHW evaluates candidates on the *real device*. The
canonical result is Thompson (1996), who evolved a tone discriminator directly in the
bitstream of a Xilinx XC6200 — a part whose bitstream was documented. Modern FPGAs
encrypt/obscure their bitstreams and use rich switch-matrix routing, so arbitrary
bitstream edits risk net contention and device damage; the field largely retreated to
abstractions (virtual reconfigurable circuits, partial reconfiguration) or to the one
open-bitstream modern part, the Lattice iCE40.

This project asks a narrower, safer question on a **7-series Xilinx part** (XC7Z010, on
a ~$10 recycled EBAZ4205 bitcoin-miner board): *can a fitness-driven search design and
then physically instantiate on-chip digital logic, intrinsically and live, while staying
contention-safe?* We constrain every mutation to **LUT-INIT bits (truth tables), local
select functions, and register config** — never raw Xilinx switch-matrix routing — which
is structurally safe, and we evaluate on real silicon via a soft-core + a PS mailbox.

## 2. Approach - a milestone ladder, VRC -> ICAP -> internal-ICAPE2 -> memetic hybrid

Built on primitives from the sibling project `zynq-xpart` (DFX, live ICAP LUT-INIT edit,
prjxray bit-location, NEORV32 soft-core, a 4×4 INT8 array, measured-boot, the PS mailbox),
copied in read-only. The ladder separates *fast search* from *physical instantiation*:

- **EHW-0 (evolve weights).** A board-resident GA on NEORV32 evolves the INT8 weights of
  a tiny folded 2×2-MNIST classifier, evaluated on a register-loaded 4×4 array (a *virtual
  reconfigurable circuit*: config in FFs, deterministic, one fixed bitstream). The champion
  is then **ICAP-baked** into LUT-KCM fabric (EHW-0.5) — the chip writes its evolved weights
  into its own LUT logic, live, no reset.
- **EHW-1 (evolve a logic circuit).** A Cartesian-GP grid (fixed routing, evolved LUT-INITs)
  evolves a 2-bit multiplier — first with software evaluation on the soft-core (EHW-1.1-sw),
  then on a **true fabric VRC** (`rtl/cgp_vrc.v`, the grid as config-loaded LUTs, EHW-1.1-fabric),
  then **ICAP-rewritten** into a hardwired baked grid (EHW-1.2): a broken 7/16 multiplier
  becomes a perfect 16/16 by editing four LUT-INITs, live.
- **EHW-2 (authentic per-eval bitstream evolution).** The closest to Thompson: NEORV32 drives
  the **fabric ICAPE2** (`rtl/xbus_icap.v`) so that *every fitness evaluation* is a real on-chip
  LUT-INIT edit of a live LUT, scored in place. Small (a single LUT-INIT toward a target).
- **EHW-3 (spare-routing island).** A fixed-route island adds safe local path-select LUTs plus
  a spare node. The genome evolves both logic truth tables and local mux-select fields, without
  mutating global routing bits. It first demonstrates fault recovery in host/RTL fabric VRC form
  (EHW-3.0→3.2), then live ICAP repair of a baked broken island (EHW-3.3), then per-eval
  internal-ICAPE2 evolution over the spare-routing genome (EHW-3.4).
- **EHW-4 (GA x HW-SGD memetic weights).** A fixed-point train-unit from the
  `zynq-xpart` lineage is copied into this repo, reduced to fit the DFX pblock,
  and used as a hardware SGD inner loop. NEORV32 runs the GA while candidate
  adaptation uses the train-unit in fabric; same-boot board A/B compares
  Baldwinian and Lamarckian inheritance.
- **EHW-5 (safe structure + weights + HW-SGD).** The EHW-3 spare-route feature
  genome and EHW-4 INT8 weight genome are co-evolved. Candidate evaluation uses
  the spare-route VRC feature in fabric and the board-verified HW-SGD train-unit;
  EHW-5.4a closes the line with a same-image/same-boot four-arm ablation.

## 3. Results (all board-verified on the EBAZ4205)

| Milestone | Result on silicon |
|---|---|
| EHW-0.3 | board GA → INT8 classifier 40/40; champion bit-identical to the numpy oracle |
| EHW-0.5 | ICAP-bake evolved weights → mailbox `0x80AF7FF2` (bit-exact to the VPU model) |
| EHW-1.1-sw / -fabric | CGP GA → 2-bit multiplier 16/16 (software, then true fabric VRC) |
| EHW-1.2 | ICAP-rewrite evolved LUTs → 7/16 multiplier becomes 16/16, live |
| EHW-2 | per-eval on-chip ICAPE2 LUT-INIT evolution → converges to target `0xeb0308e8` |
| EHW-3.2 | spare-routing fabric VRC recovers injected `DISABLE_NODE(A1)` fault → 8/8, using spare AS |
| EHW-3.3 | ICAP-baked spare-route repair → broken `c8/7` island becomes `e8/8`, live |
| EHW-3.4 | per-eval internal-ICAPE2 spare-route evolution → steady `0xec0308e8` (repair, 8/8, mask `0xe8`) |
| EHW-4.4 | board GA + HW-SGD Lamarckian loop → steady `0xF4F00028` (40/40) |
| EHW-4.5 | same-boot Baldwinian/Lamarckian A/B → steady `0xF7F02828` (both 40/40; Lamarckian faster, Baldwinian lower SSE) |
| EHW-4.6a/b | one-boot 12-point parameter sweep, then PS-writable parameter window (`0x40000000` -> `0xF5000000`) |
| EHW-5.2 | combined spare-route VRC + lite train-unit RM → `0xF5F00000` at FCLK0=50 MHz |
| EHW-5.3 | full hybrid structure+weight+HW-SGD Lamarckian-pressure arm → `0xF5F30000`, 40/40, SSE 4513 |
| EHW-5.4a | same-boot four-arm ablation → `0xF5F40000`, all arms match host golden; arm1 best SSE 4513 |
| EHW-0.4 (host) | on the SAME 40-sample set, the evolved INT8 weights score 40/40 vs the gradient-trained tile's 37/40 |

Every board-bound artifact has a host self-proof (numpy oracle ↔ portable-C twin, bit-exact,
plus a golden cross-check) that must pass before the board run.

## 4. Relation to prior art

Two recent works revived intrinsic direct-bitstream EHW on a *modern* part (Lattice iCE40,
open IceStorm): **CoBEA** (Hoffmann et al., GECCO'22 — a framework, 130× faster reconfig;
it explicitly names "Xilinx 7-series + DPR" as future work) and **Whitley et al.** (ISAL'21 —
intrinsic *analog* evolution exploiting device physics; evolved circuits die on sibling chips).

This project differs deliberately: (a) it runs on the **7-series part they named but did not
target**, using prjxray as the open bit-location layer; (b) it is **self-reconfiguring** —
on-chip ICAP under a running soft-core, not a host+MCU rig; (c) it trades Thompson-style
physics-exploitation for **LUT-INIT/VRC safety + determinism + an ML application**. We do
*not* claim to exploit analog/physical effects, nor cross-device transfer; our edits are
pure truth-table changes and our results are deterministic and reproducible.

## 5. Limitations (honest)

- **EHW-0.4 is a deployment-set metric, not generalization.** The GA was scored on the same
  40 samples it optimized against; the 40/40-vs-37/40 gap is an INT8 deployment comparison,
  not a held-out/generalization claim.
- **Small problems.** XOR-scale weight nets, a 2-bit multiplier, a one-LUT EHW-2 target, and a
  3-input-majority spare-routing island; the LUT-KCM whole-net does not fit XC7Z010 spatially
  (documented in `zynq-xpart`), so EHW-0 uses a folded tile. EHW-2/EHW-3.4 use small pre-staged
  candidate sets — the mechanism is authentic per-eval ICAP, but the search space is tiny.
- **EHW-1.0 CGP is a fixed pass-through scaffold + evolved output LUTs** (n8..n11), not all 12
  LUTs freely evolved.
- **No analog / no raw-routing evolution** — by design (contention safety on 7-series). EHW-3
  evolves safe local path-select fields implemented as LUT/select INITs inside a fixed-route
  island, not arbitrary FPGA switch boxes.
- **EHW-1.1-sw evaluates the LUT grid in software**; only EHW-1.1-fabric puts it in fabric.
- **EHW-4/EHW-5 are same-set deployment/adaptation metrics.** They show that a
  fixed-route FPGA design can co-evolve safe local structure and INT8 weights,
  use a board-verified HW-SGD inner loop for adaptation, and pass same-boot
  ablations. They do not claim holdout generalization or arbitrary-scale EHW.
- **EHW-5 does not require ICAP baking to make its main claim.** EHW-5.4b
  parameter-window host prep is optional post-release polish with board staging
  pending; EHW-5.5 ICAP reveal remains an optional future demo, not a
  prerequisite for the structure+weight+HW-SGD result.

## 6. Reproducibility

- Host gates need only Python+numpy, a C compiler, and Icarus Verilog: `tests/run_host_gates.sh`.
- Board/ICAP flows need Vivado 2025.2, the RISC-V toolchain, prjxray + the xc7z010 DB, and the
  board; per-milestone steps are in `docs/BOARD_REPRO.md`, exact observed mailbox words in
  `docs/board_results.md`, board facts in `docs/hw_notes.md`. Versions: README "Dependencies".
  From EHW-5.2 onward, `scripts/board-set-fclk50.py` is mandatory before
  `fpga loadb`; older v1.0.0 board results were produced at the miner default
  125 MHz and remain valid because their observed outputs were bit-exact.
- The project never modifies its sibling source projects; reusable assets are copied in.

## 7. Failure cases & lessons (caught on silicon)

The host gates have blind spots; the board build/run is the final gate. Real bugs found and fixed:

1. **iverilog accepts what Vivado synth rejects** (no-input functions) → added an optional OOC
   `synth_design` gate to the RTL host tests.
2. **A firmware MBOX address bug** (PS `0x41200000` vs PL `0xF1000000`) the host stub could not
   see; only the board (all-zero mailbox) exposed it.
3. **Forgetting to bake the firmware into IMEM before the Vivado build** → the bitstream ran
   stale firmware.
4. **The mailbox was on AXI-GPIO channel 2** (`0x41200008`) in the ICAP SoC, not ch1.
5. **A multi-frame ICAP bake run in the foreground** was killed by a tool timeout mid-transfer
   and corrupted the ICAP FSM → always background it; recover with a full `fpga loadb` reload.
6. **The frame-anchoring tool gave two FARs the same start** when their diffs were identical →
   monotonic per-FAR start assignment.
7. **A LUT-INIT spanning two config FARs needs two ICAP envelopes** — 7-series FDRI commits all
   but the last frame of a burst (it's a pad), so a single envelope writes a *truncated phenotype*.
   This was the EHW-2 `0xeb020520` (wrong) → `0xeb0308e8` (correct) fix — **not** a DIN bit-ordering
   issue, as a host-side diagnosis correctly predicted before re-running on the board.
8. **Poking a PS-HWICAP path in an internal-ICAPE2 build** (which has no PS HWICAP) wedged the
   PL-AXI interconnect and required a **physical power-cycle** — the one time in the whole project.
9. **A board-pass framebank must be sized from real routed bitstreams, not fake host seqs.**
   EHW-3.4's real spare-route candidate bank required a 64KB frame buffer; the final board-pass
   bank used 5278 words padded to 16384 words. The host stub's tiny fake frame sequences cannot
   prove this resource bound.
10. **The real PL clock can differ from the Vivado signoff clock.** Miner U-Boot
    leaves FCLK0 at 125 MHz while the DFX designs sign off 50 MHz. EHW-5.2
    exposed placement-dependent wrong answers until FCLK0 was pinned to 50 MHz
    before `loadb`. This does not invalidate the earlier v1.0.0 milestones:
    those results were bit-exact at the clock they actually ran.

No hardware was damaged: every edit is a reversible LUT-INIT/config change.
