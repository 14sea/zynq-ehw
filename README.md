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
- **Build pending** — no search loop implemented yet. Next: extend a numpy GA
  oracle, get the host GA bit-exact, then go on-board.

## Layout

- `docs/ehw_feasibility.md` — feasibility verdict (every EHW primitive already
  exists in zynq-xpart; only the GA search layer is new).
- `docs/ehw_design.md` — detailed design: VRC↔ICAP duality, GA engine, throughput
  budget, the EHW-0/EHW-1/EHW-2 milestone ladder, risk register, reuse map.
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
