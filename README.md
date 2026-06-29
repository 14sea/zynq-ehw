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
- **EHW-0.2 bridge started** — `sw/ehw/ehw_eval_mbox.c` evaluates a compiled
  champion genome through the register-loaded VRC array and publishes mailbox
  score tags. True host-sent genomes still need a PS→PL command path because the
  copied DFX top does not pin out NEORV32 `uart0`.
- **EHW-0.3 bridge started** — `sw/ehw/ehw_ga_mbox.c` runs the deterministic GA
  resident on NEORV32 and publishes progress/champion tags; its host stub is
  byte-exact to the Python oracle.

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
- `sim/oracle_evolve.py` — EHW-0.0 host GA oracle; writes per-generation CSV logs
  under `runs/` (gitignored).
- `sw/ehw/` — EHW host/firmware C twin code; currently `ehw_kernel.h` and
  `ga_eval.c` for EHW-0.1, plus `ehw_eval_mbox.c` for the EHW-0.2 VRC/mailbox
  bridge and `ehw_ga_mbox.c` for the EHW-0.3 board-resident GA bridge.
- `host/ehw_watch.py` — U-Boot serial mailbox watcher for EHW `0xE*` status tags.
- `tests/compare_ehw0_twin.py` — builds the C twin and verifies Python/C
  bit-exact CSV curves plus the M7.5.3 golden bitmap guard.
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
