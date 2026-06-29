# Next-milestone handoff (Claude → ChatGPT)

Status (2026-06-29): **EHW-0.3, EHW-0.4, EHW-1.0, EHW-1.1-sw, EHW-0.5 all done**
(board runs in `docs/board_results.md`; pushed to github.com/14sea/zynq-ehw). Board-
verified on EBAZ4205: EHW-0.3 (GA classifier 40/40), EHW-1.1-sw (CGP 2-bit multiplier
16/16, **software** LUT-grid eval), EHW-0.5 (ICAP-bake evolved champion into LUT-KCM
fabric → `0x80AF7FF2`). P1/P2/P3 below are all COMPLETE.

**▶ Next real target = EHW-1.1-fabric (P4 below): `rtl/cgp_vrc.v`** — promote EHW-1.1
from software-eval to a true fabric VRC (the CGP grid as config-loaded LUTs). New RTL
= your deliverable; I build + board it.

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

## P4 — EHW-1.1-fabric: `rtl/cgp_vrc.v` (NEXT — your deliverable)

Promote EHW-1.1 from software-eval (DONE) to a **true fabric VRC**: the CGP grid as
real config-loaded LUTs, evaluated in fabric, not in NEORV32 software.

- **RTL `rtl/cgp_vrc.v`:** an `R×C` grid (start 3×4 LUT4) of config-loadable nodes;
  each node's truth table (16-bit INIT) loaded from a config register (so the genome
  is written into registers per eval, VRC-style — like `systolic_array_4x4.v` loads
  weights). Fixed feed-forward routing (column→column), contention-safe. Expose on the
  NEORV32 xbus (model on `wb_tpu_accel.v`/`tpu_accel.v` register map) so firmware can:
  write the 12 node INITs, drive the 16 input combinations, read back the 4 outputs.
- **Firmware:** a board CGP GA like `cgp_ga_mbox.c` but the fitness eval drives the
  `cgp_vrc` registers (config + apply inputs + read outputs) instead of `cgp_eval_grid`
  software. Same GA (keep bit-exact to `cgp_eval.c`).
- **Host gate first:** a Verilog testbench (or a host model of the register protocol)
  proving the grid computes the truth table for a known genome, **before** the board
  build. Then I build (new RM or reuse the tpu_rp partition) + board-verify TT 16/16.
- Watch the M7.2 build-lottery (new fabric logic in the RP); keep the grid small.

---

## Constraints (don't regress)

- Every hardware-bound deliverable ships with a host gate (oracle + C twin +
  bit-exact + golden cross-check). I board-verify only after the host gate is green.
- Keep `sim/oracle_evolve.py` ↔ `sw/ehw/ehw_kernel.h` identical and m753-faithful
  (`down = 8 - wshift - ashift`, leaky `z>>k`; the 37/40 is the net's true label
  accuracy, not a bug — `docs/hw_notes.md`).
- Isolation absolute: edit only `zynq_ehw`; `external/` is read-only reference.
- Hardware facts go in `docs/hw_notes.md`; I log board runs in `docs/board_results.md`.

Recommended order: P1–P3 DONE. **Next = P4 (`rtl/cgp_vrc.v` fabric CGP VRC)** — write
the RTL + host gate, then ping me to build + board it.
