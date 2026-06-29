# Evolvable Hardware (EHW) on the EBAZ4205 — Feasibility Analysis

> Forward-looking planning doc (2026-06-29). The EHW work itself will live in a
> **separate standalone project** (next session) but builds directly on the
> primitives proven here in `zynq-xpart` (M4 / M6.5 / M7.3+ / M7.5). Verdict:
> **a scoped intrinsic *digital* EHW demo is HIGHLY feasible** — every primitive
> already exists and is HW-verified; the only new layer is a fitness-driven search
> loop. The biggest risk (bitstream contention) is structurally avoided because we
> only ever mutate LUT-INIT bits.

## 1. What EHW is (background)

Use an evolutionary algorithm (GA / genetic programming) to *automatically design*
hardware rather than hand-design or gradient-train it. The FPGA configuration (or a
subset) is the **genotype**; candidate circuits are evaluated for **fitness** and
selected / mutated / recombined.

- **Intrinsic vs extrinsic**: candidates evaluated on the **real FPGA** vs in a
  simulator. Thompson showed the two diverge — intrinsic evolution can exploit
  physical device behaviour a simulator can't model.
- **Canonical result**: Adrian Thompson, 1996 (Sussex), evolved a tone
  discriminator (1 kHz vs 10 kHz square waves) on a Xilinx **XC6216**, genotype =
  the 1800-bit config, ~3500 generations → **< 40 cells, no clock**, with a group
  of cells "logically disconnected yet essential" (analog/physical coupling).
- **CGP (Cartesian Genetic Programming, J. Miller)**: dominant modern encoding;
  often beats conventional synthesis on circuit area.
- **VRC (Virtual Reconfigurable Circuit, L. Sekanina)**: because ICAP reconfig is
  slow, build a *virtual* reconfigurable array in fabric whose config lives in
  **registers/BRAM**, and evolve that at memory speed (thousands of evals/sec) — no
  bitstream write per evaluation.

**Modern-FPGA constraint (stressed across the literature):** modern routing does
NOT allow safely mapping an arbitrary genotype onto the bitstream — random edits can
cause **net contention and potentially damage the device**. Constraints are
mandatory. Two recent works revived direct-bitstream EHW on modern parts:
- **CoBEA / IEEE CEC 2022** "Evolving Hardware by Direct Bitstream Manipulation of a
  Modern FPGA" — Thompson redux on a **Lattice iCE40** via the open toolchain,
  ~130× faster reconfiguration than prior work.
- **"Resurrecting/Intrinsic Evolution of Analog Circuits Using FPGAs"** (ALIFE 2021
  / Artificial Life 2022) — reviving Thompson-style analog intrinsic evolution.

## 2. Why this project is an unusually good EHW substrate

An intrinsic EHW system needs five things — **all already built and HW-verified
here**:

| EHW requirement | What we already have | Milestone |
|---|---|---|
| **Mutation operator** (edit logic live) | ICAP edit of LUT-INIT, no reset, reversible | M4 / M7.3+ / **M7.5.1** |
| **Genotype↔phenotype map** | prjxray locates every LUT bit exactly | used throughout |
| **Intrinsic fitness eval** (on real silicon) | 4×4 array compute + PS mailbox readout | M2 / M6 / M7 |
| **Evolution-loop controller** | NEORV32 soft-core + PS (ARM Linux) | throughout |
| **Safety gate / attestation** | measured-boot (M5) + ICAP `readreg` | M5 / M7.3+ |

In M7.5 the chip already **writes (gradient-trained) weights into its own LUT logic,
live**. EHW only swaps "gradient training" for a **fitness-driven evolutionary
search** wrapped around the exact same ICAP-mutate + mailbox-fitness primitives.

## 3. Risks / constraints (and how we already address them)

1. **Contention safety (biggest risk).** Arbitrary 7-series bitstream edits can short
   nets / damage the part. The field's answer — and ours — is to **mutate only
   LUT-INIT bits** (pure truth tables, always safe) located via prjxray. We already
   *only* edit LUT-INIT (M4, M7.5.1). ✅ structurally avoided.
2. **Reconfiguration speed.** ICAP frame writes are slow (our UART path ~seconds).
   EHW needs hundreds–thousands of evals. Two routes, both available to us:
   - **VRC route (recommended first):** use our **register-loadable 4×4 array** as
     the virtual reconfigurable array; an evaluation = write config regs + one
     compute → thousands/sec, no ICAP per eval.
   - **True-ICAP route:** NEORV32→ICAPE2 internal reconfig (proven, #8 pt.1) — much
     faster than UART, still frame-granular; the "real bitstream evolution" story.
3. **CRC/ECC.** Our ICAP `writeseq` already disables CRC (proven). ✅
4. **No Thompson-style unconstrained analog evolution.** 7-series LUT/routing is more
   constrained than the XC6200 (arbitrary connections risk contention). Scope to
   **digital LUT-INIT / VRC** evolution; do not chase analog physical exploitation.

## 4. Verdict

**High feasibility** for a scoped **intrinsic digital** EHW demo: all primitives are
built & verified; the only new code is the GA search layer; the dominant risk
(contention) is avoided by editing LUT-INIT only.

## 5. Candidate first demos (ranked by feasibility)

- **EHW-0 (recommended — fastest & safest): evolve weights of the LUT-KCM / array.**
  GA evolves the tile weights with **intrinsic fitness** (on-board classification
  accuracy / logic-function correctness). LUT-KCM weights *are* LUT-INITs, so
  "evolving the weights" = mutating LUT-INIT = literally EHW. Lets us contrast
  **evolution vs gradient training (M7)** on the same net. The **VRC variant**
  (register-loaded weights) runs thousands of evals/sec. Reuses the entire M7.5 stack.
- **EHW-1 (classic EHW): evolve a small combinational circuit** (e.g. 2-bit
  multiplier or a pattern/tone discriminator). Genotype = the LUT-INIT bits of N LUTs
  (fixed routing, CGP-style); on-board intrinsic fitness. Textbook EHW reproduced on
  our board.
- **EHW-2 (stretch, Thompson-style): routing/physics-exploiting unconstrained
  evolution** — high contention risk on 7-series; skip or heavily constrain.

## 6. Open decisions (to settle when we start)

1. **VRC** (register config, fast, in-fabric) vs **true-ICAP bitstream evolution**
   (slower, closer to Thompson) — or VRC first, then port the winner to ICAP.
2. **Task**: evolve classifier weights (continues M7) vs evolve a logic function vs a
   discriminator.
3. Intrinsic only, or also compare to extrinsic/simulated evolution.

**Recommendation:** start **EHW-0 via the VRC route** (minimal new code, safe,
thousands of evals/sec, clean "evolution vs training" narrative); once it works, do
**EHW-1** (classic logic-circuit evolution). Port to true-ICAP last if we want the
authentic "evolving the real bitstream" headline.

## 7. Next session

Create a **new standalone project dir** (e.g. `zynq_ehw/`), copy in the needed
bring-up + ICAP/prjxray plumbing from `zynq-xpart` (never modify zynq-xpart in
place), and implement EHW-0. Reusable assets to copy/reference:
- ICAP mutate: `scripts/hwicap-uart.py`, `scripts/m75-build-frameseqs.py`,
  `vivado/dfx/m753_edit_tile.tcl`, prjxray + `/home/test/prjxray-db`.
- Fitness substrate: the LUT-KCM RM (`rtl/dfx/lutkcm_array.v`, `lutkcm_pe.v`) and/or
  the register-loadable 4×4 array; mailbox readout (`scripts/m7-watch-*.py`).
- DFX build flow: `vivado/dfx/build_dfx.tcl`; reset-to-U-Boot + loadb recipes.

## Sources

- Evolvable hardware — Wikipedia: <https://en.wikipedia.org/wiki/Evolvable_hardware>
- Thompson, *An Evolved Circuit, Intrinsic in Silicon, Entwined with Physics*:
  <https://link.springer.com/chapter/10.1007/3-540-63173-9_61>
- evolvablehardware.org (tone discriminator / history): <https://evolvablehardware.org/tone.html>
- Sekanina, *Virtual Reconfigurable Circuits for Real-World Applications of EHW*:
  <https://link.springer.com/chapter/10.1007/3-540-36553-2_17>
- *Evolving Hardware by Direct Bitstream Manipulation of a Modern FPGA* (IEEE CEC 2022):
  <https://ieeexplore.ieee.org/document/9870297/>
- *CoBEA* (GECCO 2022, PDF):
  <https://nmi.informatik.uni-leipzig.de/wp-content/uploads/2022/08/cobea_gecco2022.pdf>
- *Intrinsic Evolution of Analog Circuits Using FPGAs* (Artificial Life, MIT Press 2022):
  <https://direct.mit.edu/artl/article/28/4/499/112726>
- *Semantically-Oriented Mutation Operator in CGP* (arXiv): <https://arxiv.org/pdf/2004.11018>
- f4pga/prjxray: <https://github.com/f4pga/prjxray>
