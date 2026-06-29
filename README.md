# zynq_ehw — Evolvable Hardware on the EBAZ4205 (XC7Z010)

Intrinsic **digital** evolvable-hardware (EHW) demos on the EBAZ4205 (Xilinx
Zynq-7010). A fitness-driven evolutionary search designs/adapts on-chip logic by
mutating only **LUT-INIT bits** (truth tables) and **register config** — never
routing — so it is contention-safe on a 7-series part.

This is a **standalone project**. It builds on primitives already HW-verified in
the sibling project [`zynq-xpart`](../zynq_xpart) (DFX, ICAP LUT-INIT edit,
prjxray, measured-boot, the 4×4 array + LUT-KCM, PS mailbox). Per project policy
those assets are **copied in here, never edited in place** — `zynq-xpart` is left
untouched.

## Status

- **Design done** — see `docs/`. Decisions: task = the full ladder (EHW-0 evolve
  weights → EHW-1 evolve a logic circuit); substrate = **VRC first → true-ICAP
  finish**.
- **EHW-0.0 done** — `sim/oracle_evolve.py` implements a deterministic GA over the
  M7.5.3-lite 24-byte INT8 weight genome, using the same fixed-point inference
  task as `zynq_xpart`.
- **EHW-0.1 host gate done** — `sw/ehw/ga_eval.c` mirrors the Python oracle with a
  C/xorshift GA path; `tests/compare_ehw0_twin.py` checks Python vs C CSV output
  byte-for-byte and checks the M7.5.3 trained genome against its golden
  classification bitmap.
- **EHW-0.3 HW-VERIFIED on EBAZ4205** — `sw/ehw/ehw_ga_mbox.c` runs the GA resident
  on NEORV32 over the 4×4 register-loaded VRC array; on silicon it converged 40/40
  with the champion genome **bit-identical to the host oracle** (`docs/board_results.md`).
- **EHW-0.4 host comparison done** — `sim/ehw0_4_compare.py`: GA champion 40/40 labels
  vs the M7.5.3 gradient-trained tile's 37/40 — evolution beats gradient on this INT8
  net (`docs/ehw0_4_results.md`).
- **EHW-0.5 HW-VERIFIED on EBAZ4205** — the EHW-0.3 evolved W1 tile was **ICAP-baked
  into the LUT-KCM fabric, live** (PS/NEORV32 never reset); mailbox
  `0x1019391F → 0x80AF7FF2`, bit-exact to the VPU-model golden, attested
  (`sw/ehw/lutkcm_post.c`, `vivado/dfx/build_lutkcm.tcl`).
- **EHW-1.0 host oracle done** — `sim/oracle_cgp.py` + `tests/compare_cgp_twin.py`:
  CGP GA evolves a fixed-routing 3×4 LUT4 grid into a 2-bit multiplier, 16/16 rows,
  Python/C bit-exact.
- **EHW-1.1 HW-VERIFIED on EBAZ4205 (software-eval)** — `sw/ehw/cgp_ga_mbox.c` runs
  the CGP GA resident on NEORV32 → 2-bit multiplier 16/16, champion bit-identical to
  host. ⚠️ **This evaluates the LUT grid in NEORV32 *software*, not in a fabric VRC
  substrate.** The fabric-CGP version (`rtl/cgp_vrc.v`, the grid as real config-loaded
  LUTs) is the NEXT milestone — see `docs/next_handoff.md`.
- **EHW-1.1-fabric HW-VERIFIED on EBAZ4205** — `rtl/cgp_vrc.v` implements the CGP grid
  as real config-loaded fabric LUTs behind an XBUS register map; the board-resident GA
  evaluated fitness **on the fabric VRC** (MMIO drive) and evolved the 2-bit multiplier
  to 16/16 rows, champion bit-identical to host (`docs/board_results.md`). This is the
  true fabric substrate — the evolved circuit *is* hardware, vs EHW-1.1-sw's software eval.

## Layout

- `docs/ehw_feasibility.md` — feasibility verdict (every EHW primitive already
  exists in zynq-xpart; only the GA search layer is new).
- `docs/ehw_design.md` — detailed design: VRC↔ICAP duality, GA engine, throughput
  budget, the EHW-0/EHW-1/EHW-2 milestone ladder, risk register, reuse map.
- `docs/reference_map.md` — which files to mine from `/home/test/zynq_xpart` and
  `/home/test/zynq_agentctl`, without editing those projects in place.
- `docs/hw_notes.md` — board facts used as codegen constraints: mailbox addresses,
  fixed-point conventions, VRC register map, ICAP/PCAP handoff, settle/build
  variance notes.
- `docs/board_results.md` — exact board observations and mailbox words for each
  hardware gate.
- `docs/ehw0_4_results.md` — evolution-vs-gradient-training table for EHW-0.
- `docs/ehw1_0_results.md` — host-only CGP 2-bit multiplier result.
- `docs/ehw1_1_fabric_results.md` — host gate for the fabric CGP VRC substrate.
- `sim/oracle_evolve.py` — EHW-0.0 host GA oracle; writes per-generation CSV logs
  under `runs/` (gitignored).
- `sim/ehw0_4_compare.py` — reproducible EHW-0.4 comparison generator.
- `sim/oracle_cgp.py` — EHW-1.0 CGP/LUT-INIT oracle for a 2-bit multiplier.
- `rtl/cgp_vrc.v` — EHW-1.1-fabric register-configured CGP VRC core and XBUS wrapper.
- `rtl/dfx/tpu_rp_rm_cgp_vrc.v` — DFX RM wrapper exposing the CGP VRC in the existing
  `0xF0000000` NEORV32 peripheral window.
- `sw/ehw/` — EHW host/firmware C twin code; currently `ehw_kernel.h` and
  `ga_eval.c` for EHW-0.1, plus `ehw_eval_mbox.c` for the EHW-0.2 VRC/mailbox
  bridge, `ehw_ga_mbox.c` for the EHW-0.3 board-resident GA bridge, and
  `cgp_kernel.h`/`cgp_eval.c` for the EHW-1.0 CGP twin, `cgp_ga_mbox.c` for the
  EHW-1.1-sw board-resident software-eval CGP GA, `cgp_vrc_mbox.c` for the
  EHW-1.1-fabric board-resident fabric-eval CGP GA, and `lutkcm_post.c` for the
  EHW-0.5 ICAP-bake POST.
- `host/ehw_watch.py` — U-Boot serial mailbox watcher for EHW `0xE*` status tags.
- `tests/compare_ehw0_twin.py` — builds the C twin and verifies Python/C
  bit-exact CSV curves plus the M7.5.3 golden bitmap guard.
- `tests/compare_cgp_twin.py` — builds the CGP C twin and verifies Python/C
  bit-exact CGP curves.
- `tests/compare_cgp_vrc.py` — builds/runs the CGP VRC RTL host gate and firmware
  host stub.
- `external/` — local snapshots of selected reference files copied from
  `/home/test/zynq_xpart` and `/home/test/zynq_agentctl`. These make this project
  independent; edit only the copies here, never the source projects. Also includes
  `external/research/cobea/`, a shallow-cloned CoBEA research-code snapshot for
  architecture study.
- `ref/` — the two direct-bitstream-EHW reference papers (CoBEA, GECCO '22;
  Whitley et al., ISAL '21). **gitignored** (copyrighted PDFs), like zynq-xpart's
  `references/`.

## Positioning vs prior art (one line)

CoBEA and Whitley do intrinsic **bitstream** EHW on **Lattice iCE40** (open
IceStorm), embracing unconstrained edits to exploit device physics (Thompson
lineage). We move that to the **7-series** part they named as future work, make it
**self-reconfiguring** (on-chip ICAP under a running soft-core), and trade
physics-exploitation for **LUT-INIT/VRC safety + determinism + an ML application**
(evolution vs gradient training). See `docs/ehw_design.md`.
