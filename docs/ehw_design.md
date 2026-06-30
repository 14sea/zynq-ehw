# Evolvable Hardware (EHW) on the EBAZ4205 — Detailed Design

> Companion to `ehw_feasibility.md` (the verdict doc). This is the **detailed
> design / refinement** pass (2026-06-29). Decisions taken with the user this
> session:
> - **Task = the full ladder:** detail **both** EHW-0 (evolve weights) **and**
>   EHW-1 (evolve a logic circuit), as one milestone ladder.
> - **Substrate = VRC first → true-ICAP finish:** evolve fast in-fabric on a
>   register-configured virtual reconfigurable circuit, then port the headline to
>   real ICAP bitstream edits.
>
> The EHW build itself lives in a **new standalone dir `zynq_ehw/`** next session
> (copy plumbing in from `zynq-xpart`, never modify it in place). This doc is the
> blueprint that dir implements.

---

## 0. The one insight that makes "VRC → ICAP" almost free

The VRC↔ICAP duality **already exists in the RTL**, as two implementations behind
one identical port list:

| | `rtl/systolic_array_4x4.v` (VRC) | `rtl/dfx/lutkcm_array.v` (ICAP) |
|---|---|---|
| weights | loaded into **FF registers** at runtime (`load_weight`, `w_row_sel`, `w_col_sel`, `w_data`; 16 cycles) | baked as **LUT6 INIT** constants (`lutkcm_pe.v`, `dont_touch`), edited live by ICAP |
| ports | `clk,rst_n,en,load_weight,w_row_sel,w_col_sel,w_data,x_in[31:0],result[127:0]` | **identical** (load_* ports kept as ignored stubs for drop-in compat) |
| reconfig cost / eval | ~16 cycles, no bitstream write | 18–128 ICAP frame writes |

So **the genotype is the same 128-bit weight vector** for both. An evaluation on
VRC = write 16 weights into FFs + one compute. The *same* champion genome is then
realised on silicon by baking those 128 bits into the `lutkcm` LUT-INITs with the
**already-proven M7.5.1 machinery** (`m75_edit_tile.tcl` + `m75-build-frameseqs.py`
+ `hwicap-uart.py`). "VRC first → ICAP finish" is therefore not two designs — it is
**one genome driving two array backends we already have.**

This also quietly **dodges the M7.2 gremlin** (build-dependent in-context routing
that made multi-epoch `rm_train` miscompute): VRC fitness is computed inside **one
fixed bitstream** with weights in FFs, so every evaluation is deterministic and
routing-invariant. Only the *final* champion bake re-touches the fabric, and we
verify it on-board the same way M7.5 does (board result == VRC-predicted == oracle).

---

## 1. System shape (who runs the GA, who evaluates fitness)

Three tiers, all reused from the agentctl / M7 stack:

```
 PS (ARM Linux, Python)            NEORV32 soft-core              PL fabric
 ── EVOLUTION CONTROLLER ──        ── EVAL SEQUENCER ──           ── FITNESS SUBSTRATE ──
 GA: population, select,    →      load genome → array,     →     VRC array (EHW-0)
 mutate, crossover, log     ←      run test vectors,        ←     or CGP grid (EHW-1)
 (drives board as oracle)          accumulate fitness,             result → mailbox
                                   publish via mailbox 0x41200000
```

- **GA loop on PS** (Python, like `host/agentctl.py`): owns population + RNG + logs;
  cheap, flexible, easy to compare against the M7 oracle. Sends a genome, reads back
  a scalar fitness.
- **Eval on NEORV32**: the inner loop that loads a genome into the array and streams
  the test vectors, summing fitness, then writes `{generation, individual, fitness}`
  to the PS mailbox (`md 0x41200000`, decoded like `scripts/m7-watch-*.py`).
- **Two speed regimes** (this is the whole VRC-vs-ICAP tradeoff, quantified in §3):
  - **Host-in-the-loop:** PS sends each genome over UART, NEORV32 evaluates, returns
    fitness. Simple; throughput bounded by UART round-trips (~ms/eval).
  - **Board-resident GA (fast variant):** the entire GA runs *on NEORV32* (genome
    RAM + xorshift RNG + tournament), PS only seeds the run and reads the champion
    + loss curve. No per-eval UART → thousands of evals/sec. Recommended once the
    host-in-the-loop version is validated bit-exact against a host oracle.

---

## 2. Genotype encoding

### EHW-0 (evolve weights)
- Genome = the INT8 weight vector of the net being evolved.
- Folded 4-4-2 net (the M7.5.3-lite net, 2×2 MNIST 0/1): L1 `W1[4][4]` + L2
  `W2[2][4]` = 24 weights × 8 bit = **192 bits** (each tile is the 16-weight,
  128-bit array genome; L2 uses 8 of 16 lanes).
- Genes are **bounded INT8** (clamp to ±127); mutation = per-byte ±δ or bit-flip.
- This genome is directly the array's weight load **and** directly the LUT-INIT bake
  payload — no transcoding.

### EHW-1 (evolve a logic circuit, CGP-style)
- Genome = the **LUT-INIT truth tables** of an `N`-node feed-forward grid with
  **fixed routing** (routing is *not* evolved → no net contention, the field's
  mandatory constraint).
- Encoding (Cartesian Genetic Programming, Miller): `R×C` grid of k-input LUT nodes;
  each node gene = `(INIT bits)`; in the constrained variant each node also has a
  small **input-select** field choosing among the *previous column's* outputs only
  (a fixed mux, so all "wiring" is legal by construction).
- Sizing for the first target (2-bit multiplier, 4-in→4-out): a 3×4 grid of LUT4
  nodes ⇒ 12 nodes × 16 INIT bits = **192 genome bits** (+ optional 12×log2(prev)
  select bits). Small enough for fast GA, large enough to be non-trivial.

---

## 3. Throughput budget — the honest VRC-vs-ICAP reconciliation

Population `P=32`, generations `G=200` ⇒ **~6400 evaluations** (typical for these
toy benchmarks).

| Path | per-eval cost | 6400 evals | verdict |
|---|---|---|---|
| **VRC, host-in-loop** | UART round-trip ≈ 1–5 ms | ~30 s – few min | fine for bring-up |
| **VRC, board-resident** | 16-cyc load + ~40-cyc eval @50 MHz ≈ 1–10 µs | **< 0.1 s** compute (UART only for final report) | **the search engine** |
| **true-ICAP per eval, UART** | 18–128 frame writes ≈ seconds | **hours** | infeasible per-eval |
| **true-ICAP per eval, on-chip ICAPE2** | frame writes at fabric clk ≈ 100s µs–ms | minutes–hours | only for a *small* "authentic" demo |

**Conclusion (defines the ladder):**
1. **Search on VRC** (board-resident GA) — thousands of evals/sec, deterministic,
   contention-free.
2. **Reveal on ICAP** — bake *only the champion* genome into the `lutkcm` fabric
   (one M7.5.1-style job) and show it computing live = "evolution wrote its answer
   into the chip's own logic." This is the practical headline.
3. **Authentic-bitstream stretch (EHW-2):** a *small* run (P≈8, G≈20) where **each**
   eval is a real on-chip **ICAPE2** LUT-INIT edit (`rtl/xbus_icap.v` already gives
   us internal ICAP) — the true Thompson "evolving the live bitstream" story, run at
   a generation count the slow loop can afford.

---

## 4. The GA engine (shared by EHW-0 and EHW-1)

Deliberately textbook so the result is legible:
- **Representation:** fixed-length bit/byte genome (§2).
- **Init:** uniform random (host-seeded, like M7's host-seeded init — reproducible,
  no board RNG needed for the oracle twin).
- **Selection:** tournament, k=3.
- **Crossover:** uniform (per-gene) or 1-point, rate ~0.7.
- **Mutation:** per-gene bit-flip (EHW-1) / ±δ INT8 (EHW-0), rate ~1–3 %.
- **Elitism:** keep top 1–2 unchanged.
- **Fitness:** task-specific scalar (below), maximised.
- **Termination:** target fitness reached or `G` generations.
- **Oracle twin:** a deterministic `sim/oracle_evolve.py` (extend the `sim/oracle_*.py`
  pattern) runs the *identical* GA in fixed-point host-side; the board run
  must track it **bit-exact** (same seed, same selection order), exactly as M7.0
  validated training. This is how we trust the on-board GA.

Fitness handshake reuses the M7.5.3-lite **probe-wait + incrementing heartbeat**
pattern (`scripts/m753-demo.py`) so the host knows the board finished an eval/gen
and the loop is provably live, not hung.

---

## 5. EHW-0 — evolve weights (the "evolution vs gradient training" story)

- **Substrate:** `systolic_array_4x4.v` VRC (weights in FFs).
- **Net / task:** the **same folded 4-4-2 net on 2×2 MNIST 0/1** as M7.5.3-lite — so
  the comparison to M7 gradient training is apples-to-apples on the identical
  network and dataset.
- **Scale guard:** M7.5.3's software-net array downshift is `8 - WSHIFT - ashift`
  (`>>4` for L1, `>>3` for L2), matching `oracle_m753.py::array_mm`. Its trained
  reference tile matches the M7.5.3 golden classification bitmap exactly, while
  that bitmap is 37/40 against labels; EHW-0's GA can still evolve a different
  24-byte genome that reaches 40/40 labels under the same fixed-point net.
- **Fitness:** classification accuracy over the test set (40 digits), read via
  mailbox; tie-break by negative SSE for a smooth gradient.
- **The experiment (the headline):**
  1. GA-evolve the 24-weight genome on VRC to high test accuracy.
  2. Tabulate **evolution vs training**: GA generations-to-converge & final acc vs
     M7's SGD epochs & final acc, on the same net — does blind fitness search reach
     what backprop reached? cost? robustness to INT8 quantisation (GA optimises the
     *quantised* net directly — no QAT master needed, a genuine EHW advantage)?
  3. **ICAP reveal:** bake the GA champion's weights into the `lutkcm` tile (M7.5.1
     machinery), classify live on-board, attest (`readreg` IDCODE/STAT). Verify
     board result == VRC fitness == oracle before trusting (M7.5 discipline).
- **Why it's a strong first demo:** ~zero new RTL (array exists both ways), reuses
  100 % of the M7.5 ICAP/mailbox stack, and yields a clean scientific contrast.

## 6. EHW-1 — evolve a logic circuit (classic CGP EHW)

- **Substrate:** a **new small VRC**: an `R×C` CGP grid of LUT nodes whose INIT (and
  constrained input-select) live in **config registers** — `rtl/cgp_vrc.v` (new),
  modelled on `rtl/lut_probe.v`. Fixed legal routing ⇒ contention-free by design.
- **Task (recommended): 2-bit unsigned multiplier** (`a[1:0]*b[1:0] = p[3:0]`): 4
  inputs, 4 outputs, fully specified 16-row truth table — the canonical EHW
  benchmark (Miller/Vasicek), trivially & completely verifiable.
  - *Rejected alt:* tone/pattern **discriminator** — needs timing/analog behaviour
    and drifts toward Thompson physics-exploitation, which we explicitly scope out
    (7-series contention risk). Keep EHW-1 purely combinational.
- **Fitness:** 64-bit Hamming match of the grid's outputs to the golden truth table
  (16 rows × 4 outputs = 64 bits correct); optional secondary objective = node count
  (CGP's famous area-minimisation — evolve a *smaller* multiplier than synthesis).
- **VRC search → ICAP reveal:** same two-stage flow — evolve config-register genome
  fast, then bake the champion's LUT-INITs into a baked-LUT variant via ICAP and show
  the evolved multiplier running in hardwired logic.

## 7. Full milestone ladder

| ID | Deliverable | Substrate | Status gate |
|---|---|---|---|
| **EHW-0.0** | `oracle_evolve.py` GA evolves 4-4-2 weights to target acc (host only) | — | ✅ host acc curve |
| **EHW-0.1** | C twin GA bit-exact to oracle (shared kernel, host-seeded xorshift) | host | ✅ mism=0 via `tests/compare_ehw0_twin.py` |
| **EHW-0.2** | board: NEORV32 evaluates genomes on VRC array; fitness bit-exact | VRC | bridge started (`sw/ehw/ehw_eval_mbox.c`); superseded by EHW-0.3 (true host-in-loop still awaits a PS→PL command path) |
| **EHW-0.3** | board-resident GA on NEORV32; full evolution curve via mailbox, bit-exact | VRC, on-board GA | ✅ **HW-VERIFIED** — board 40/40, champion bit-identical to oracle (`docs/board_results.md`) |
| **EHW-0.4** | **evolution-vs-training** table (GA champion vs M7 SGD, same net) | VRC | ✅ `docs/ehw0_4_results.md` (GA 40/40 > gradient 37/40) |
| **EHW-0.5** | ICAP-bake GA champion into `lutkcm` tile, classify live, attest | ICAP reveal | ✅ **HW-VERIFIED** — mailbox `0x1019391F→0x80AF7FF2` bit-exact, attested (`docs/board_results.md`) |
| **EHW-1.0** | CGP GA evolves 2-bit multiplier (truth-table fitness) | — | ✅ 16/16 rows via `tests/compare_cgp_twin.py` |
| **EHW-1.1-sw** | board-resident CGP GA on NEORV32, **software** LUT-grid eval | on-board GA, SW eval | ✅ **HW-VERIFIED** — 2-bit multiplier 16/16, champion bit-identical (`sw/ehw/cgp_ga_mbox.c`, `docs/board_results.md`) |
| **EHW-1.1-fabric** | `rtl/cgp_vrc.v` = CGP grid as real config-loaded **fabric LUTs**; board evolves the multiplier on the VRC | VRC (fabric) | ✅ **HW-VERIFIED** — board TT 16/16, champion bit-identical (`docs/board_results.md`) |
| **EHW-1.2** | ICAP-rewrite the evolved multiplier's LUT-INITs, run live, attest | ICAP reveal | ✅ **HW-VERIFIED** — ICAP rewrote n8..n11 → broken 7/16 multiplier became perfect 16/16, live (`docs/board_results.md`) |
| **EHW-2** *(stretch)* | small run with **per-eval on-chip ICAPE2** edits (authentic bitstream evolution) | true-ICAP | a few gens converge live |

> **NB (EHW-1.1 distinction):** EHW-1.1-sw (DONE) runs the GA *and* evaluates the LUT
> grid in NEORV32 software — proves on-chip evolution of a logic circuit, but the grid
> is not in fabric. EHW-1.1-fabric (the original `cgp_vrc.v` intent) puts the grid in
> real config-loaded LUTs so the evolved circuit *is* hardware. Don't conflate them.

Gates mirror M7's discipline: host bit-exact first, then on-board, then the ICAP
reveal verified against the VRC/oracle result.

## 8. New code vs reused (delta over zynq-xpart)

**Reused as-is:** `systolic_array_4x4.v` (VRC array), `lutkcm_array.v`/`lutkcm_pe.v`
(+ M7.5.1 `m75_edit_tile.tcl`, `m75-build-frameseqs.py`, `hwicap-uart.py`) for the
ICAP reveal, mailbox decoders `m7-watch-*.py`, `build_dfx.tcl`, reset-to-U-Boot +
`uboot-fpga-load.py`, measured-boot gate (M5), `xbus_icap.v` for EHW-2.

**New (lives in `zynq_ehw/`):**
- `sim/oracle_evolve.py` — deterministic GA + fitness oracle (both tasks).
- `sw/ehw/ga_eval.c` (+ shared `ehw_kernel.h`) — NEORV32 eval + optional on-board GA.
- `host/ehw_eA.py` — PS evolution controller (host-in-loop variant), reuses
  agentctl/mailbox/probe-wait patterns.
- `rtl/cgp_vrc.v` — the EHW-1 CGP grid VRC (EHW-0 needs **no** new RTL).
- champion-bake + attest scripts: thin wrappers over the M7.5 ICAP tooling.

## 9. Risk register (refined)

1. **Net contention / device damage** — avoided structurally: VRC evolves only
   *register* config; ICAP edits only *LUT-INIT* (M4/M7.5.1 proven safe); routing is
   never evolved. ✅
2. **M7.2 in-context-routing gremlin** — VRC eval is one fixed bitstream → immune.
   Only the champion ICAP-bake re-touches fabric; verify board == VRC == oracle
   before trusting (M7.5 protocol). ✅ mitigated.
3. **On-board GA ≠ oracle** — enforce host-seeded init + identical selection order +
   bit-exact C twin (M7.0 method) before believing any on-board curve.
4. **INT8 fitness plateaus / GA stagnation** — standard cures: tournament size,
   mutation-rate schedule, elitism, fitness shaping (acc + −SSE); the oracle lets us
   tune all of this off-board first.
5. **ICAP reveal silently no-ops** — the M7.5.3-lite lessons apply verbatim: verify
   `readreg 12 == 0x13722093` and `PCAP_PR==0` before baking; one FAR-set per
   sync..DESYNC envelope.

## 10. Recommendation (narrowed)

Build the ladder **EHW-0.0 → EHW-0.5 first** (fastest, safest, near-zero new RTL,
clean evolution-vs-training narrative on the exact M7 net), then **EHW-1.0 → 1.2**
(the textbook CGP logic-circuit result with one new small RTL block). Treat **EHW-2**
(per-eval on-chip ICAPE2 "authentic bitstream evolution") as the stretch headline
once `xbus_icap` throughput is characterised. Start next session by extending
`sim/oracle_*.py` into `oracle_evolve.py` and getting the host GA bit-exact before
any board work — same opening move that made every M7 milestone land.

## 11. Related work & positioning (vs CoBEA and Whitley et al.)

Two recent works revived direct-bitstream intrinsic EHW on a *modern* part (Lattice
iCE40, open IceStorm toolchain). Both PDFs are in `ref/`. This section pins down how
our design relates to them — and, by contrast, justifies our three core decisions.

- **CoBEA** — Hoffmann, Fritzsch, Bogdan, *GECCO '22 Companion*
  ([doi:10.1145/3520304.3528821](https://doi.org/10.1145/3520304.3528821)). A Python
  framework for EHW by **direct bitstream manipulation**; its headline result is a
  **130× faster reconfiguration** (integrated direct-reconfig + bitstream compaction
  vs IceStorm+flash; single reconfig ~3.5 s → ~86 ms). Philosophically it **rejects
  VRC and DPR** as "safe abstractions [that] neglect the huge potential for solutions
  outside their abstraction model." Future work it names explicitly: *"support Xilinx
  7-Series … they support dynamic partial reconfiguration … update only parts of the
  chip"* and security (bind a design to one device).
- **Whitley, Yoder, Carpenter** — *"Resurrecting FPGA Intrinsic Analog Evolvable
  Hardware," ISAL 2021* ([doi:10.1162/isal_a_00448](https://doi.org/10.1162/isal_a_00448)).
  Intrinsic **analog** EHW: evolves **unclocked** circuits that exploit device physics
  (Thompson redux). Genome = CLB bitstream (864 b/CLB, 96 CLBs); to tame the space and
  contention they constrain **routing** (8-neighbour Moore, ≤2 inputs, one Boolean op
  per CLB). GA pop 50 / 100 gens, ~3.5 s/eval, MCU does 12-bit ADC of the analog
  output for fitness. Tasks: amplitude maximisation, pulse oscillation. Key finding:
  evolved circuits are **device-entwined** — dead on 2 of 3 other same-model FPGAs,
  temperature-coupled.

### Per-axis comparison

| Axis | Whitley (ISAL'21) | CoBEA (GECCO'22) | **This project** |
|---|---|---|---|
| FPGA part | Lattice iCE40 HX1K | iCE40 HX1K/HX8K | **Xilinx 7-series XC7Z010** (prjxray) — CoBEA's named future work |
| Reconfig mechanism | host→flash/SRAM, whole chip | integrated direct reconfig + compaction, whole chip (no DPR on iCE40) | **(a) VRC: register-loaded, no bitstream write/eval; (b) on-chip ICAP/DPR self-reconfig** under a running soft-core |
| Cost / eval | ~3.5 s | ~86 ms | **VRC ~µs–ms; ICAP paid once (champion only)** |
| Genome / what's mutated | constrained **routing** + Boolean op | raw bitstream (philosophically unconstrained) | **LUT-INIT / register config only — routing never evolved** |
| Intrinsic? | yes, **exploits analog physics** | yes | yes (real silicon) but **digital, clocked, physics-exploitation scoped out** |
| Controller arch. | host CPU + external MCU + passive FPGA | host-driven | **GA/eval on-chip** (NEORV32 + PS + mailbox): chip = controller + substrate + measurement |
| Cross-chip transfer | **no** (2/3 dead) — the physics proof | n/a | **yes / deterministic**, bit-exact to a host oracle |
| Safety / attestation | none | future work | **already have** measured-boot (M5) + ICAP-readback attest |
| Application | analog oscillators/discriminators | framework + reconfig speed + security | **ML: evolve NN weights, evolution-vs-gradient-training**; + CGP logic circuit |

### Three readings that justify our decisions

1. **We deliberately take the road CoBEA criticises — and that is the right call on
   7-series.** CoBEA's thesis is that VRC/DPR abstractions throw away reachable
   solutions; our entire EHW-0 line is VRC + LUT-INIT-only. The justification is the
   substrate: on 7-series, unconstrained routing edits risk **net contention / device
   damage** (the literature's own repeated warning). iCE40's bitstream is fully
   documented and less catastrophic to poke; 7-series is not. We trade Thompson-style
   "reach outside the abstraction" for **structural safety + determinism**. This is a
   genuine values fork, stated openly — not a claim of superiority.
2. **On mechanism we are strictly ahead of both — and it is exactly CoBEA's wish
   list.** CoBEA's future work is "7-series + DPR, reconfigure only part of the chip";
   that is our *starting point*, already HW-verified (M7.3+/M7.5). Both papers use a
   three-box rig (host GA + external MCU + passive FPGA); we **self-reconfigure**
   (on-chip ICAP under a live NEORV32, fitness via on-chip mailbox). In the strong
   sense of "intrinsic," our loop is more self-contained.
3. **The M7.2 gremlin is the very thing Whitley prizes — we just invert the stance.**
   Whitley's headline is circuits "entwined with physics" that die on a different
   die. Our **M7.2 build-dependent in-context-routing** (placement changes → STA-clean
   but functionally wrong array) is an *uninvited* dose of the same effect. They treat
   such physical coupling as the **feature to exploit**; we documented it as a **bug**
   and route around it with VRC (single fixed bitstream → deterministic, immune). Same
   phenomenon, opposite worldview — worth remembering if we ever want a "Thompson
   mode" demo: M7.2 says this part *will* give us device-entwined behaviour for free.

### Honest maturity caveat

Both papers are peer-reviewed and show **real evolved circuits on silicon** (loss/
fitness curves, cross-chip and temperature robustness data). We currently have the
**substrate + this design**: the M7.5 ICAP/VRC/mailbox primitives are HW-verified,
but the EHW search loop is **not built and has evolved nothing yet**. Our claim is
feasibility + blueprint, not a demonstrated evolution run. EHW-0.0→0.3 is what closes
that gap.

### One-line positioning

We are not reproducing these papers: we move direct-bitstream/intrinsic EHW onto the
**7-series part they named but did not target**, make it **self-reconfiguring**, and
swap **physics-exploitation for LUT-INIT/VRC safety + an ML application** (evolution
vs gradient training). Our EHW-2 stretch (on-chip ICAPE2 per-eval LUT-INIT edits) is
the closest point of contact — still LUT-INIT-only and self-hosted, on harder silicon.

## 12. Lessons adopted from prior art (actionable)

§11 is about how we *differ*; this section is what we should *borrow*. Each item is
tagged with its source and where it lands in our plan.

### A. Adopt directly (changes our code/RTL)

1. **Non-zero seed search** *(Whitley)* — random circuits almost all score 0 (output
   disconnected / sparse), so do a short random search for ONE non-zero-fitness
   individual and seed the initial population from it. **→ EHW-1 (CGP):** random
   LUT-INIT genomes mostly give all-wrong truth tables (flat landscape); seed first.
   **→ EHW-0 (weights):** random INT8 weights can saturate to a constant output stuck
   at 50% — same cure. Implement in `oracle_evolve.py` init; mirror on-board.
2. **Expect plateau→leap landscapes; give partial credit; watch the underlying metric,
   not the shaped fitness** *(Whitley)* — they saw long flat stretches punctuated by
   jumps (a subnet binds to the output in one mutation), and with a peaky `1/|f−n|`
   fitness the *mean fitness* read ≈0 while the population was actually progressing
   (visible only in mean *frequency*). **→ §4 fitness:** use a continuous shaped
   fitness (`acc + (−SSE)` term), and **log/plot the raw metric (acc, SSE)** alongside
   fitness so we don't misread a plateau as stagnation.
3. **Reconfig is the only bottleneck — optimise it separately from the app, and keep
   the inner loop in one resident process** *(CoBEA)* — their 130× came from (i) sending
   only changed data, (ii) direct SRAM config (no flash), (iii) **no external-tool
   calls per eval** (Whitley's 3.5 s/eval was mostly packer+programmer spawns).
   **→ host-in-loop variant:** hold the serial port open in ONE long-lived process;
   never `spawn` vivado/openocd per eval (a real trap in our current script set).
   **→ EHW-2 ICAP:** send only diff frames (our prjxray-diff + single-FAR-envelope
   already does this — make it explicit), never round-trip a host file.
4. **Fixed evolvable region inside a constant "basic bitstream"** *(CoBEA)* — synthesise
   the static once, mutate only the evolvable region each cycle. **→** this is exactly
   our DFX static + RP/RM; keep the evolvable substrate a small fixed-location tile
   (the RP / LUT-KCM tile), everything else constant.
5. **Standardised per-generation logging (CSV/HDF5)** *(CoBEA)* — cheap, and essential
   for the **evolution-vs-gradient-training** table (EHW-0.4) to be clean and
   reproducible. **→** log `{gen, individual, genome, fitness, acc, SSE}` per eval.

### B. Validates choices we already made (no change, just confidence)

- **Standard GA + tournament selection** at pop≈50 / ~100 gens *(both papers)* →
  confirms our textbook-GA, k=3 tournament, pop≈32 scale.
- **Device-coupling is real** *(Whitley: evolved circuit dead on 2/3 sibling FPGAs,
  temperature-sensitive)* → **bake robustness checks into the gates**: after the
  champion ICAP-bake, verify across power-cycles / re-loads. Our M7.2 build-variance
  is the local analogue of their cross-chip death.
- **Guard against reward-hacking** *(Whitley's upside-down-but-passing waveform)* →
  every gate must verify the champion does the **task**, not merely hits the fitness
  proxy.

### C. Reference assets to mine

- **CoBEA source:** `github.com/nmi-leipzig/cobea` — Python, clean-architecture GA
  framework with EA engine decoupled from the FPGA backend. Borrow its
  **`evaluate(genome) -> fitness` interface** so one GA engine serves EHW-0 (weights,
  VRC/ICAP) and EHW-1 (CGP) behind a common substrate API.
- **Whitley:** `evolvablehardware.org` — tutorials + source for reproducing Thompson
  tasks; engineering detail on the measurement rig and seed search.

### D. The inversion worth keeping in our back pocket

Whitley *exploits* device-coupling; we route around it. But if we ever want a
"Thompson-mode" easter egg, **M7.2 is free device-entwined behaviour on this exact
part** — a placement-dependent, STA-clean-yet-functionally-distinct effect. Normally a
bug we dodge with VRC; on demand, a feature to show.

> Cross-refs: A2 feeds **§4 (GA engine — fitness shaping & monitoring)**; A1 feeds
> **§4 (init)**; B (robustness, reward-hacking) and A3/A4 feed **§9 (risk register)**;
> A5 feeds **§7 EHW-0.4** (the evolution-vs-training table needs the logs).
