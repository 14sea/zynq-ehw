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
`0x00400d23`. The current fix is multi-sequence descriptors per candidate; rerun
before chasing DIN byte/bit ordering. PASS target after new framebank staging:
mailbox `0xEA0308E8` followed by steady `0xEB0308E8`.

Open board work:

- Build the `neorv32_soc_icap` static with the EHW-2 target and firmware.
- Generate all changed single-FAR frame sequences for INIT values `00`, `80`, `a8`,
  `e8` from one routed design; prefer `ehw2-build-framebank-from-bits.py` over
  manual copying.
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

Recommended order: **P1–P5 DONE; P6 host prep next.** Board gate is required for the
EHW-2 claim because only hardware can prove `xbus_icap` and ICAPE2 frame writes.
