# Next-milestone handoff (Claude → ChatGPT)

Status (2026-06-30): **EHW-0.3, EHW-0.4, EHW-1.0, EHW-1.1-sw, EHW-1.1-fabric,
EHW-0.5, EHW-1.2 ALL HW-VERIFIED** (board runs in `docs/board_results.md`).
Board-verified on EBAZ4205: EHW-0.3 (GA classifier 40/40), EHW-1.1-sw (CGP 2-bit
multiplier 16/16, **software** LUT-grid eval), EHW-1.1-fabric (CGP multiplier 16/16
on true fabric VRC), EHW-0.5 (ICAP-bake evolved weights into LUT-KCM →
`0x80AF7FF2`), EHW-1.2 (ICAP-rewrite the evolved logic circuit's LUTs → broken 7/16
multiplier becomes perfect 16/16, live). **P1–P5 ALL COMPLETE.**

**▶ Next real target:** no mandatory ladder item remains. Options are polish/writeup,
tag/release, or EHW-2 stretch (small per-eval on-chip ICAPE2 evolution).

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
  final label-accuracy (both 40/40? the trained one is 37/40 against labels — note
  the GA *found a better-than-gradient INT8 solution*, that's the interesting
  result); SSE; and the INT8-direct advantage (the GA optimises the quantised net
  directly, no Q8.8 master / QAT needed).
- **Deliverable:** `sim/ehw0_4_compare.py` (deterministic, reproducible) that emits
  a small table + a short `docs/ehw0_4_results.md`. No board needed.
- **Host gate:** reproducible from a fixed seed; numbers must be regenerable.

## P2 — EHW-1.0: CGP 2-bit multiplier oracle (host-only) — DONE

Start the classic-logic-circuit rung (design doc §6).

Status: implemented in `sim/oracle_cgp.py` and `sw/ehw/cgp_eval.c`; host gate is
`tests/compare_cgp_twin.py`; generated result in `docs/ehw1_0_results.md`.

- **Genome** = LUT-INIT truth tables of an `R×C` fixed-routing CGP grid (routing is
  NOT evolved — contention-safe). Suggested 3×4 grid of LUT4 nodes (~192 genome
  bits) for a 4-in→4-out 2-bit unsigned multiplier.
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
- Watch the M7.2 build-lottery (new fabric logic in the RP); keep the grid small.

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

## Constraints (don't regress)

- Every hardware-bound deliverable ships with a host gate (oracle + C twin +
  bit-exact + golden cross-check). I board-verify only after the host gate is green.
- Keep `sim/oracle_evolve.py` ↔ `sw/ehw/ehw_kernel.h` identical and m753-faithful
  (`down = 8 - wshift - ashift`, leaky `z>>k`; the 37/40 is the net's true label
  accuracy, not a bug — `docs/hw_notes.md`).
- Isolation absolute: edit only `zynq_ehw`; `external/` is read-only reference.
- Hardware facts go in `docs/hw_notes.md`; I log board runs in `docs/board_results.md`.

Recommended order: **P1–P5 DONE.** Next work should be explicitly scoped: either
writeup/release polish or the EHW-2 stretch.
