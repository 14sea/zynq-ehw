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
    host-in-the-loop version is validated bit-exact against a numpy oracle.

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
- **Oracle twin:** a numpy `sim/oracle_evolve.py` (extend the `sim/oracle_*.py`
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
| **EHW-0.0** | numpy `oracle_evolve.py` GA evolves 4-4-2 weights to target acc (host only) | — | host acc curve |
| **EHW-0.1** | C twin GA bit-exact to oracle (shared kernel, host-seeded) | host | mism=0 |
| **EHW-0.2** | board: NEORV32 evaluates host-sent genomes on VRC array; fitness bit-exact | VRC, host-in-loop | mailbox == oracle |
| **EHW-0.3** | board-resident GA on NEORV32; full evolution curve via mailbox, bit-exact | VRC, on-board GA | curve == oracle |
| **EHW-0.4** | **evolution-vs-training** table (GA champion vs M7 SGD, same net) | VRC | comparison written |
| **EHW-0.5** | ICAP-bake GA champion into `lutkcm` tile, classify live, attest | ICAP reveal | board == champion |
| **EHW-1.0** | numpy CGP GA evolves 2-bit multiplier (truth-table fitness) | — | 16/16 rows |
| **EHW-1.1** | `cgp_vrc.v` + C eval; board evolves multiplier on VRC, bit-exact | VRC | TT 16/16 on board |
| **EHW-1.2** | ICAP-bake evolved multiplier's LUT-INITs, run live, attest | ICAP reveal | board TT 16/16 |
| **EHW-2** *(stretch)* | small run with **per-eval on-chip ICAPE2** edits (authentic bitstream evolution) | true-ICAP | a few gens converge live |

Gates mirror M7's discipline: host bit-exact first, then on-board, then the ICAP
reveal verified against the VRC/oracle result.

## 8. New code vs reused (delta over zynq-xpart)

**Reused as-is:** `systolic_array_4x4.v` (VRC array), `lutkcm_array.v`/`lutkcm_pe.v`
(+ M7.5.1 `m75_edit_tile.tcl`, `m75-build-frameseqs.py`, `hwicap-uart.py`) for the
ICAP reveal, mailbox decoders `m7-watch-*.py`, `build_dfx.tcl`, reset-to-U-Boot +
`uboot-fpga-load.py`, measured-boot gate (M5), `xbus_icap.v` for EHW-2.

**New (lives in `zynq_ehw/`):**
- `sim/oracle_evolve.py` — numpy GA + fitness oracle (both tasks).
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
