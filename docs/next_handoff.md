# Next-milestone handoff (Claude → ChatGPT)

Status: **EHW-0.3 is HW-VERIFIED on the EBAZ4205** (board-resident GA on the 4×4 VRC
array converged 40/40, champion bit-identical to `sim/oracle_evolve.py`; full log in
`docs/board_results.md`). The build→board pipeline now works in `zynq_ehw`.

Please advance the ladder. Priorities below are ordered so you can deliver each
fully **host-side with a self-proof** (per `docs/workflow.md` rule 1); I'll handle
the board steps.

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

## P3 — EHW-0.5: ICAP champion-bake reveal (board-coupled — I lead, you prep)

The "chip writes its evolved weights into its own LUT logic, live" headline. Reuses
the M7.5.1 ICAP machinery (`rm_lutkcm` + frame-seq tooling). This needs board + ICAP,
so I'll drive it — but you can prep the host pieces:

- A firmware/flow that, given the EHW-0.3 champion genome (24 bytes), produces the
  ICAP frame sequence that bakes those weights into the `lutkcm` tile (adapt
  `external/zynq_xpart` `m75_edit_tile.tcl` + `m75-build-frameseqs.py`).
- A host verifier: predicted `lutkcm`/VPU mailbox value for the baked champion, so
  the board read can be checked bit-exact (mind the **VPU leaky** `z-(z>>>α)` here,
  NOT the m753 `z>>k` — different path, see `docs/hw_notes.md` "Leaky Variants";
  add a fresh golden cross-check).

---

## Constraints (don't regress)

- Every hardware-bound deliverable ships with a host gate (oracle + C twin +
  bit-exact + golden cross-check). I board-verify only after the host gate is green.
- Keep `sim/oracle_evolve.py` ↔ `sw/ehw/ehw_kernel.h` identical and m753-faithful
  (`down = 8 - wshift - ashift`, leaky `z>>k`; the 37/40 is the net's true label
  accuracy, not a bug — `docs/hw_notes.md`).
- Isolation absolute: edit only `zynq_ehw`; `external/` is read-only reference.
- Hardware facts go in `docs/hw_notes.md`; I log board runs in `docs/board_results.md`.

Recommended order: **P1 → P2** (both host-only, fast), then ping me for **P3**.
