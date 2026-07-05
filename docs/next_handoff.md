# Next-milestone handoff (Claude → ChatGPT)

Status (2026-06-30): **THE WHOLE LADDER IS HW-VERIFIED — EHW-0.3, EHW-0.4, EHW-1.0,
EHW-1.1-sw, EHW-1.1-fabric, EHW-0.5, EHW-1.2, AND the EHW-2 stretch** (board runs in
`docs/board_results.md`). EHW-2 = per-eval on-chip ICAPE2 LUT-INIT evolution converged
to target (`0xeb0308e8`) — authentic Thompson live-bitstream evolution. **P1–P6 ALL
COMPLETE; no mandatory ladder item remains.** Next work is optional: writeup/release
polish, a tag, or deeper EHW-2 variants.

Priorities are ordered so you can deliver each fully **host-side with a self-proof**
(per `docs/workflow.md` rule 1); I'll handle the board steps.

---

## P1 — EHW-0.4: evolution vs gradient-training (host-only, do first) — DONE

The scientific headline of EHW-0. Same net, two ways to get the weights.

Status: implemented in `sim/ehw0_4_compare.py`; generated result in
`docs/ehw0_4_results.md`.

- **Compare** the GA champion (just verified on board) against the **M7-trained**
  genome on the *same* folded 4-4-2 net (`M753_TRAINED_GENOME` in
  `sim/oracle_evolve.py` is the gradient-trained tile; the GA champion is the
  evolved one).
- **Metrics to tabulate:** generations-to-converge (GA) vs epochs (M7 SGD);
  final label-accuracy on the same 40-sample evaluation set (the trained one is
  37/40 against labels); SSE; and the INT8-direct advantage (the GA optimises the
  quantised net directly, no Q8.8 master / QAT needed). Do not present this as a
  holdout/generalization win; GA fitness uses the same 40 samples.
- **Deliverable:** `sim/ehw0_4_compare.py` (deterministic, reproducible) that emits
  a small table + a short `docs/ehw0_4_results.md`. No board needed.
- **Host gate:** reproducible from a fixed seed; numbers must be regenerable.

## P2 — EHW-1.0: CGP 2-bit multiplier oracle (host-only) — DONE

Start the classic-logic-circuit rung (design doc §6).

Status: implemented in `sim/oracle_cgp.py` and `sw/ehw/cgp_eval.c`; host gate is
`tests/compare_cgp_twin.py`; generated result in `docs/ehw1_0_results.md`.

- **Genome** = LUT-INIT truth tables of an `R×C` fixed-routing CGP grid (routing is
  NOT evolved — contention-safe). The implemented first version is scaffolded:
  col0/col1 are fixed pass-through LUTs and only the four output LUTs in col2
  evolve, even though the full 12-word genome is logged/evaluated.
- **Fitness** = 64-bit Hamming match to the golden truth table (16 rows × 4 outs).
  Optional secondary objective: node count (CGP area minimisation).
- **Deliverable:** `sim/oracle_cgp.py` (deterministic GA over the grid) + a portable-C twin
  + a bit-exact test (`tests/compare_cgp_twin.py`), same pattern as EHW-0.
- **Host gate:** GA reaches 16/16 rows; Py↔C bit-exact. **No board yet** — the
  `rtl/cgp_vrc.v` substrate + board run is a later rung (I'll build it).
- Reference only (check license, vendor no RTL): tiny-tpu / CGP literature in
  `external/research/`.

## P3 — EHW-0.5: ICAP champion-bake reveal — DONE (HW-VERIFIED)

The "chip writes its evolved weights into its own LUT logic, live" headline. Built on
the M7.5.1 ICAP machinery: built static+`rm_lutkcm` in zynq_ehw (`build_lutkcm.tcl`),
firmware `sw/ehw/lutkcm_post.c`, frames via `m753_edit_tile.tcl` + prjxray diff +
`m75-build-frameseqs.py` (20 frames). On EBAZ4205: baseline `0x1019391F` → ICAP-bake →
`0x80AF7FF2`, bit-exact to the VPU-model golden (VPU leaky `z-(z>>>α)`), attested. Full
log + the "never foreground a multi-frame ICAP bake" gotcha in `docs/board_results.md`.

## P4 — EHW-1.1-fabric: `rtl/cgp_vrc.v` — DONE (HW-VERIFIED)

Promoted EHW-1.1 from software-eval to a **true fabric VRC**: the CGP grid as real
config-loaded LUTs, evaluated in fabric (not NEORV32 software).

Status: ✅ **HW-VERIFIED on EBAZ4205** — built static+`rm_cgp_vrc` (`build_cgp_vrc.tcl`,
impl_10), firmware `sw/ehw/cgp_vrc_mbox.c` evaluates fitness over the `cgp_vrc` MMIO
regs; on silicon the GA reached 2-bit multiplier 16/16 rows (mailbox `0xdc000010`),
champion bit-identical to host (`docs/board_results.md`). Synth-compat fix to
`rtl/cgp_vrc.v` (no-input functions → dummy input; iverilog allowed it, Vivado didn't).

- **RTL `rtl/cgp_vrc.v`:** an `R×C` grid (start 3×4 LUT4) of config-loadable nodes;
  each node's truth table (16-bit INIT) loaded from a config register (so the genome
  is written into registers per eval, VRC-style — like `systolic_array_4x4.v` loads
  weights). Fixed feed-forward routing (column→column), contention-safe. Expose on the
  NEORV32 xbus (model on `wb_tpu_accel.v`/`tpu_accel.v` register map) so firmware can:
  write the 12 node INITs, drive the 16 input combinations, read back the 4 outputs.
- **Firmware:** a board CGP GA like `cgp_ga_mbox.c` but the fitness eval drives the
  `cgp_vrc` registers (config + apply inputs + read outputs) instead of `cgp_eval_grid`
  software. Same GA (keep bit-exact to `cgp_eval.c`).
- **Host + board gates:** `tests/compare_cgp_vrc.py` proves the register protocol
  host-side; board run proves TT 16/16 on the fabric substrate.
- Watch the M7.2 build-lottery (new fabric logic in the RP); keep the grid small. **[2026-07-02: the "build-lottery" was later root-caused to the image_gen toolchain bug (see sw/patches/image_gen_lma_fix/) — no P&R lottery exists; grid size is bounded only by real RP resources.]**

## P5 — EHW-1.2: ICAP-rewrite the evolved multiplier's LUTs — DONE (HW-VERIFIED)

✅ On EBAZ4205: ICAP rewrote only the 4 logic LUTs (n8..n11) of the baked CGP grid,
transforming a broken 7/16 multiplier into a perfect 16/16, live. mailbox
`0xe3000007→0xe3000010`. Fixed a real `scripts/m75-build-frameseqs.py` anchoring bug
(two FARs with identical diff patterns got the same frame start → wrong bake; fix =
monotonic per-FAR start assignment). Full log + the bug/fix in `docs/board_results.md`.

<details><summary>original P5 spec</summary>

Bake the EHW-1.1-fabric champion LUT INITs into a hardwired/baked LUT variant and
show the multiplier running live after ICAP update, mirroring the EHW-0.5 champion
reveal but for the CGP logic circuit.

- Reuse the proven P4 champion genome:
  `aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0 4c00 8000`.
- Add a host gate that predicts the baked multiplier truth table exactly before
  any board run.
- Include a quick Vivado OOC `synth_design` check for new RTL; `iverilog` alone
  missed the no-input-function incompatibility in P4.

Status: host prep complete in `rtl/cgp_baked.v`,
`rtl/dfx/tpu_rp_rm_cgp_baked_base.v`,
`rtl/dfx/tpu_rp_rm_cgp_baked_champ.v`, `sw/ehw/cgp_baked_post.c`,
`tests/compare_cgp_baked.py`, `vivado/dfx/build_cgp_baked.tcl`, and
`vivado/dfx/cgp_baked_edit_champ.tcl`. `docs/ehw1_2_results.md` records the
baseline/champion truth-table expectations and frame-sequence generation flow.
</details>

---

## P6 — EHW-2 stretch: per-eval on-chip ICAPE2 evolution — DONE (HW-VERIFIED)

✅ Converged to target on EBAZ4205 (mailbox `0xeb0308e8`). Fix: LUT INIT spans 2 FARs → multi-FAR 8KB framebank (each candidate writes both frames; a single envelope left the 2nd frame as a non-committed pad). See `docs/board_results.md`.


Goal: every fitness eval performs a real in-fabric ICAP LUT-INIT rewrite through
`rtl/xbus_icap.v`, then measures the live edited LUT. The PS stages the candidate
bank once and grants ICAP ownership; it is not in the eval loop.

Current scoped experiment:

- `rtl/ehw2_lut_target.v`: one DONT_TOUCH LUT6 with firmware-controlled inputs.
- `rtl/neorv32_soc_icap.vhd`: `0xF4000000` writes the LUT input row and reads bit0.
- `rtl/axil_framebuf.vhd`: 2048x32 framebuf, enough for up to eight 233-word ICAP
  sequences.
- `sw/ehw/ehw2_icap_micro.c`: evaluates four staged candidates by streaming each
  candidate's 0..2 single-FAR sequences through `xbus_icap`, sweeping 8 truth-table
  rows, and selecting the best.
- `sim/ehw2_micro_oracle.py` + `tests/compare_ehw2_micro.py`: host gate for the
  3-input majority target (`0xe8`, max 8/8).
- `scripts/ehw2-framebank-pack.py` / `scripts/ehw2-framebank-load.py`: pack and
  stage the candidate frame sequences.
- `scripts/ehw2-build-framebank-from-bits.py`: builds the correct multi-FAR
  candidate framebank from same-route `.bit` files plus prjxray `.bits` outputs.
- `vivado/icap_ehw2/build_ehw2_icap.tcl`: zynq_ehw-local T2.3-style static build
  and four same-route INIT bitstreams.

First board run proved the internal ICAPE2 mechanism but failed fidelity with
`0xEB020520`. Host-side diagnosis found the candidate framebank had truncated each
candidate to one FAR (`0x00400d22`), while INIT `80/a8/e8` also require FAR
`0x00400d23`. The fix was multi-sequence descriptors per candidate; the rerun
passed with mailbox `0xEA0308E8` followed by steady `0xEB0308E8`.

Verified board flow:

- Build the `neorv32_soc_icap` static with the EHW-2 target and firmware.
- Generate all changed single-FAR frame sequences for INIT values `00`, `80`, `a8`,
  `e8` from one routed design; prefer `ehw2-build-framebank-from-bits.py` over
  manual copying. Its `.bits` inputs must come from the exact `.bit` files used for
  extraction; stale bitread output from an older build will mis-anchor frames.
- Stage the framebank, clear `PCAP_PR`, and run the NEORV32 eval loop.

---

## Constraints (don't regress)

- Every hardware-bound deliverable ships with a host gate (oracle + C twin +
  bit-exact + golden cross-check). I board-verify only after the host gate is green.
- Keep `sim/oracle_evolve.py` ↔ `sw/ehw/ehw_kernel.h` identical and m753-faithful
  (`down = 8 - wshift - ashift`, leaky `z>>k`; the 37/40 is the net's true label
  accuracy, not a bug — `docs/hw_notes.md`).
- Isolation absolute: edit only `zynq_ehw`; `external/` is read-only reference.
- Hardware facts go in `docs/hw_notes.md`; I log board runs in `docs/board_results.md`.

Recommended next step: optional writeup/release polish or tag. The mandatory
EHW-0 → EHW-2 ladder is complete and board-verified.

---

## P7 — generalize the EHW-2 candidate / framebank generation (ChatGPT)

`scripts/ehw2-build-framebank-from-bits.py` already generalizes the *extraction*: arbitrary
candidate INIT bytes (`nargs="+"`), auto-locates the target LUT's FAR(s) per build, and emits
the multi-FAR framebank. What is still hardcoded / manual and worth generalizing:

- **Candidate set is hand-listed (`00 80 a8 e8`).** Generalize to a search-space spec: a target
  truth-table mask + the GA's actual candidate stream (so the bank holds the GA's real
  population, not 4 didactic points), or a "generate K candidates between baseline and target".
- **Per-INIT bitstreams are pre-built** by editing one routed design's LUT INIT in
  `build_ehw2_icap.tcl`. Generalize to: given a target genome/mask, auto-emit the needed INIT
  variants (and, ideally, a single-frame edit on the routed DCP like `cgp_baked_edit_champ.tcl`
  instead of N full builds).
- **Bigger genome:** more than one LUT (a small CGP grid through ICAPE2), which makes the
  framebuf-size / per-eval-frame-count budget real.
- Keep the host gate (`tests/compare_ehw2_micro.py`) covering the new descriptor/contract, and
  remember: the target LUT moves every build → frames must be re-extracted from the fresh
  bitstreams (the `.bits` must match the `.bit` build).

## P8 — EHW-3 evolved spare-routing island (optional next research line)

Plan: `docs/ehw3_plan.md`. First rung result: `docs/ehw3_0_results.md`.

This is the controlled next step beyond LUT-INIT-only evolution: keep the outer Xilinx
routing fixed, but build a small spare-routing island whose safe local path-selection
bits and LUT truth tables are evolved. The intended claim is **spare-route selection
inside a fixed-route island**, not arbitrary vendor routing-bit mutation.

Status: EHW-3.0 host oracle is complete in `sim/oracle_spare_routing.py`.
EHW-3.1 Python/C twin is complete in `sw/ehw/spare_route_kernel.h`,
`sw/ehw/spare_route_eval.c`, and `tests/compare_spare_route_twin.py`.

EHW-3.2 is **BOARD-VERIFIED** on the EBAZ4205 (2026-07-01): built by
`vivado/dfx/build_spare_route_vrc.tcl` (static + `rm_spare_route_vrc`), loaded via
U-Boot `fpga loadb`, and the full fault→recovery narrative captured on silicon —
mailbox `0xe321..0xe328`: no-fault 8/8 mask `0xe8`, degraded 7/8 mask `0xc8`,
repaired 8/8 mask `0xe8` using spare AS (`docs/board_results.md`). POP=128, firmware
`bss=6176` (fits 16k DMEM).

EHW-3.3 is **BOARD-VERIFIED** on the EBAZ4205 (2026-07-01): the baked island's
8-FAR frame diff was ICAP-rewritten live, moving `SRB0` from `c8/7` to `e8/8` with
the marker unchanged and no PS/NEORV32 reset (`docs/board_results.md`). The full
EHW-3 ladder EHW-3.0→3.4 is now board-verified.

EHW-3.4 is complete: `sim/ehw34_icap_oracle.py`,
`sw/ehw/ehw34_icap_spare_route.c`, `rtl/ehw34_spare_route_target.v`,
`rtl/neorv32_soc_icap_sr.vhd`, `scripts/ehw34-framebank-pack.py`,
`scripts/ehw34-build-framebank-from-bits.py`, `tests/compare_ehw34_icap.py`, and
`vivado/icap_ehw34/build_ehw34_icap.tcl`. This is the per-eval internal-ICAPE2
spare-route stretch: staged candidates base/logic/route/repair, expected best
`0xEA0308E8` and steady `0xEC0308E8`. The board-pass bank uses 5278 words, so the EHW-3.4
SoC uses a 16384-word / 64KB framebuf. The build intentionally has no PS-HWICAP; do
not poke PS-HWICAP registers on this bitstream.

**EHW-3.4 is now BOARD-VERIFIED (2026-07-02):** on the EBAZ4205 the per-eval ICAPE2
loop converged to the repair candidate — mailbox (AXI-GPIO **ch2 `0x41200008`**, not
ch1) steady `0xEC0308E8` (best idx 3, 8/8, mask 0xe8); build timing met, DRC 0-err,
BRAM 37/60 (`docs/board_results.md`). **The whole EHW-3.0→3.4 ladder is board-verified;
EHW-3 is complete.** No mandatory rung remains — next work is optional (writeup/release,
a tag, or a new research line).

Post-v1.0.0 hygiene is complete through `6da55d2`: the pinned NEORV32 v1.12.9
`image_gen` LMA-gap fix is tracked and auto-applied by `scripts/setup-deps.sh`,
`sw/ehw/Makefile` has `verify-image`, and stale M7.2/settle narratives are retired.
Workflow now requires three root-cause checks before blaming silicon/P&R: flat
controls where feasible, golden-from-oracle/ELF rather than the artifact under test,
and same-firmware cross-build comparisons.

Next research line is **EHW-4 GA × HW-SGD memetic evolution** (`docs/ehw4_memetic_plan.md`).
EHW-4.0 is host-only complete in `sim/oracle_memetic.py` and
`docs/ehw4_0_results.md`: deterministic curves compare pure GA, pure HW-SGD,
Baldwinian, and Lamarckian modes on the same fixed-point deployment set.
EHW-4.1 is also host-only complete in `sw/ehw/memetic_kernel.h`,
`sw/ehw/memetic_eval.c`, and `tests/compare_memetic_twin.py`: Python and C
per-generation curves are byte-exact for all four modes (`docs/ehw4_1_results.md`).

EHW-4.2 is host-prep complete in `rtl/memetic_train_unit.v`,
`rtl/dfx/tpu_rp_rm_memetic_train.v`, `sw/ehw/memetic_train_mbox.c`, and
`tests/compare_memetic_train_unit.py` (`docs/ehw4_2_results.md`). It adapts the
M7.2 train-unit idea to the EHW-4 4→4→2 / 24-master-weight contract and proves the
RTL against a generated full 40-sample Python-oracle epoch trace. The firmware host
stub uses the same MMIO protocol as the board path and matches `mem_adapt()`.
Isolated firmware build passed `verify-image` with `text=3880 data=0 bss=0`.
Post-review note: the first train-unit RTL synthesized to 48 DSP48E1 total and
would not fit the 20-DSP RP pblock. The fixed RTL removes generic leaky-path
multipliers and targets `array 16 + train_unit <=4` DSP48E1.

EHW-4.3 is board-verified: OOC/resource/place passed (`18/20` DSP total, train unit
`2` DSP), and `memetic_train_mbox.c` reached steady mailbox `0xF4F00000` on the
EBAZ4205, proving the full 40-sample train-unit epoch is bit-exact to `mem_adapt()`
on silicon (`docs/board_results.md`).

EHW-4.4 is board-verified: `memetic_ga_train_mbox.c` ran the full Lamarckian
GA-with-HW-SGD loop on silicon (`POP=16`, `GENS=8`, `adapt_epochs=1`) and reached
steady mailbox `0xF4F00028`, matching the host curve (`40/40` by generation `3`,
final SSE `6116`).

EHW-4.5 is board-verified: `memetic_ab_train_mbox.c` ran Baldwinian then
Lamarckian arms in one firmware image / same boot (`POP=16`, `GENS=32`,
`adapt_epochs=1`) and reached final mailbox `0xF7F02828`. Host/board result:
Baldwinian first reaches `40/40` at gen `29` (SSE `4678`), Lamarckian at gen `3`
(SSE `6116`).

EHW-4.6a is board-verified in `sw/ehw/memetic_sweep_mbox.c` and
`tests/compare_memetic_sweep.py` (`docs/ehw4_6a_results.md`). One firmware image
bakes a 12-point parameter table and runs Baldwinian/Lamarckian arms sequentially
for each point. On board, one build/one boot collected all 48 carousel words:
24/24 point/mode rows, zero mismatches vs host-golden encoding, and 16/24 rows
reach 40/40. Isolated firmware build was `text=4792 data=0 bss=6080`.

EHW-4.6b is board-verified: the memetic static has a PS-writable 8 KB parameter
window. PS writes AXI `0x40000000`; NEORV32 reads XBUS `0xF5000000`; mailbox probe
proved AXI readback, soft-core readback, and live `mw` update without reboot.

EHW-5.0/5.0b is host-only complete in `sim/oracle_memetic_struct.py` and
`tests/check_memetic_struct_oracle.py` (`docs/ehw5_0_results.md`). It combines the
EHW-3 safe spare-route structure genome with the EHW-4 24-byte weight genome and
fixed-point SGD adaptation. Result: unpressured hybrid plumbing is deterministic
but can exploit degenerate features; the structural-pressure arm then reaches
`40/40`, SSE `4513`, first_40 `2`, with a non-constant `15/40` feature mask.

EHW-5.1 is host-only complete in `sw/ehw/memetic_struct_kernel.h`,
`sw/ehw/memetic_struct_eval.c`, and `tests/compare_memetic_struct_twin.py`
(`docs/ehw5_1_results.md`). The gate byte-compares full Python/C per-generation
curves and summary rows across the weight baseline, all pressured/unpressured
hybrid couplings, and the no-adapt ablation; structural-pressure penalties are
part of the golden.

EHW-5.2 is board-verified in `rtl/dfx/tpu_rp_rm_memetic_struct.v`,
`rtl/memetic_train_unit_lite.v`, `sw/ehw/memetic_struct_train_mbox.c`,
`tests/compare_memetic_struct_train.py`, and `tests/vivado_ooc_memetic_struct.tcl`
(`docs/ehw5_2_results.md`). The combined RM keeps the train-unit window at
`0xF0000800` and adds spare-route VRC at `0xF0000400`; host RTL sim and firmware
stub pass. First review failed OOC LUT budget (`5049/4400`) with the full
train-unit, so the current handoff uses the lite train unit: fixed `LR_SHIFT=7`,
fixed `K=2`, serialized W1/W2 updates, and `TU_BUSY` at word 77.

First board runs failed in several placement-dependent ways, while VRC marker/mask
and CPU-golden paths stayed clean. The final root cause was outside the RM:
miner U-Boot leaves FCLK0 at 125 MHz, but the DFX design signs off `clk_fpga_0`
at 50 MHz. Same bitstreams failed at 125 MHz and passed at 50 MHz. Final EHW-5.2
board result: `0xF5F00000`, `mism=0`, `got_sse=gold_sse=4560`, `correct=38`,
marker `SRV0`, mask `0xa0`. The held-read-data wrapper change is kept as bus
hygiene, not as the root-cause fix.

From now on, Claude must set and verify FCLK0=50 MHz before any board `loadb`
(`scripts/board-set-fclk50.py`; see `docs/hw_notes.md`).

EHW-5.3 is BOARD-VERIFIED (2026-07-04, first roll): `sw/ehw/memetic_struct_ga_mbox.c`
ran on-chip and the steady carousel matched the host golden on every acceptance
field (see `docs/board_results.md`). The arm:
`hybrid_lamarckian_pressure / bias_x3`, seed `3`, `POP=16`, `GENS=32`, one
adaptation epoch. The host gate byte-compares the entire per-generation firmware
stub curve against `sw/ehw/memetic_struct_eval.c`; expected summary is `40/40`,
SSE `4513`, first_40 `2`, feature_ones `15`, penalty `0`.

Board leg DONE by Claude 2026-07-04 (commit `5ea9ae9`): host gates 18/18,
verify-image OK (exe 5580 B, data+bss 3648 B), clean `ws_53` rebuild of the
combined RM with the new IMEM (WNS +1.026), FCLK0 preflight
`0x00200a00` captured in-session, carousel `0xf5302028 / 0xf53111a1 /
0xf5320f00 / 0xf53f0002 / 0xf5f30000` == host golden.

EHW-5.4a is BOARD-VERIFIED (2026-07-05, first roll) in
`sw/ehw/memetic_struct_ab_mbox.c`, `tests/compare_memetic_struct_ab_train.py`,
and `docs/ehw5_4_results.md`. It ran four arms in one firmware image and one
boot: weight-only Lamarckian, hybrid-pressure `bias_x3`, hybrid no-adapt
`gate_x3`, and unpressured hybrid `bias_x3`. Board carousel:
arm0 `f5400028/f55017e4/f5600003/f5700000`; arm1
`f5400128/f55111a1/f5600102/f5710f00`; arm2
`f5400228/f5521207/f560020b/f5722700`; arm3
`f5400328/f55316cd/f5600305/f5730000`; final `f54f0004/f5f40000`.

Decision: close the main EHW-5 claim at EHW-5.4a. After v1.1.0, EHW-5.4b was
started as optional post-release polish. Host prep is implemented in
`sw/ehw/memetic_struct_ab_mbox.c`, `tests/compare_memetic_struct_ab_train.py`,
and `scripts/ehw54-param-pack.py`.

5.4b board handoff:

- rebuild the same EHW-5.4 firmware/RM lineage;
- run `python3 scripts/board-set-fclk50.py --port /dev/ebaz-uart` immediately
  before `fpga loadb`;
- with no param magic, confirm the built-in 5.4a table still publishes the known
  four-arm carousel;
- generate a staged block, for example:

```bash
python3 scripts/ehw54-param-pack.py --preset pressure-short --generations 4 \
  --out runs/ehw54/param_pressure_short.bin
python3 scripts/ehw2-framebank-load.py runs/ehw54/param_pressure_short.bin 0x40000000
```

- confirm the steady carousel source word becomes staged/valid
  (`0xF54E0101`), arm count changes to the staged block, and result rows match
  the host-golden curve fields for that block without rebuilding or reloading.

5.5 remains optional ICAP reveal polish. It requires fresh routed bitstreams and
frame extraction; do not claim board evidence from host-prep alone.
