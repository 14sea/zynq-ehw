# EHW-4 Plan — GA × HW-SGD Memetic Evolution

Status: **EHW-4.0/4.1 host gates done; no board claim.** This is the next research
line after the board-verified `v1.0.0` EHW-0→EHW-3.4 ladder. It deliberately reuses
the proven `zynq_xpart` M7 training stack as a read-only reference and keeps this
repository independent by copying any required RTL/firmware into `zynq_ehw`.

## Goal

Combine the two successful lines:

- `zynq_ehw`: evolutionary search over hardware-visible genomes.
- `zynq_xpart` M7: on-board QAT-style training with the 4×4 INT8 array and
  `train_unit.v` loss / derivative / SGD-update hardware.

The experiment is a **memetic / Lamarckian EHW loop**:

1. A GA proposes a structural or seed-weight genome.
2. Each candidate gets a short, fixed budget of on-chip HW-SGD adaptation.
3. Fitness is measured after adaptation.
4. In Lamarckian mode, the adapted weights are written back into the genome before
   selection; in Baldwinian mode, adaptation affects fitness only.

The scientific question is not "GA beats SGD"; it is: **does evolutionary search over
initialization, structure, or safe local hardware config become more useful when each
candidate can learn briefly on the chip?**

## Non-Goals

- No raw Xilinx routing mutation.
- No claim of held-out generalization unless a real train/evolution/holdout split is
  added.
- No NEORV32 upgrade inside this repo; the baseline stays pinned to v1.12.9 plus the
  tracked `image_gen` patch for reproducibility.
- No direct edits to `/home/test/zynq_xpart` or `/home/test/zynq_agentctl`.

## Starting Substrate

Use the smallest path first:

- Network: the existing EHW-0 / M7.5.3-lite style `4→4→2` INT8 deployment net, or an
  even smaller `2→4→1` XOR bring-up if firmware size or board time needs a tighter
  first rung.
- Hardware: the 4×4 array for forward / transpose matmuls, plus a copied EHW-local
  `train_unit` for loss, leaky derivative, and SGD update.
- Genome v0: 24 INT8 seed weights for the `4→4→2` net.
- Candidate evaluation: load genome as initial weights, run `K` mini-epochs of
  HW-SGD on a fixed training subset, then score on the fixed deployment/evaluation
  set.

This keeps the first version close to EHW-0.4 and M7.2/M7.4, so the expected numbers
are independently checkable before any new ambition is added.

## Modes To Test

| Mode | Selection fitness | Genome after candidate eval | Why it matters |
|---|---|---|---|
| Pure GA | direct deployment score | original mutated genome | EHW-0 baseline |
| Pure HW-SGD | trained seed score | trained weights only | M7 baseline |
| Baldwinian | post-SGD score | original genome | tests whether learnability evolves |
| Lamarckian | post-SGD score | adapted weights copied back | tests whether GA+SGD accelerates search |

Run all four in the same host oracle before any board work. Do not compare against
holdout behavior unless a separate holdout set is explicitly introduced.

## Frozen Contracts

These contracts should be copied into the first host oracle and C twin headers:

- RNG: same XorShift32 family already used by EHW-0/EHW-3.
- Fixed-point: match M7 QAT conventions exactly (`Q8.8` master, INT8 forward view,
  leaky shift, array downshift, saturating arithmetic).
- Candidate budget: fixed `K` SGD epochs per eval; no early-stop unless the host and
  board twins both implement it bit-exactly.
- Fitness tuple: primary `label_correct`, secondary `-SSE`, optional tertiary
  "adaptation cost" if two modes tie.
- Logging: per generation, per candidate, record pre-adapt fitness, post-adapt
  fitness, SSE, number of SGD epochs, genome before, genome after.

The golden source is the Python oracle and ELF/objcopy image checks, not any generated
firmware image or board artifact.

## Milestone Ladder

| ID | Deliverable | Gate |
|---|---|---|
| **EHW-4.0** | host-only Python oracle for pure-GA, pure-SGD, Baldwinian, and Lamarckian modes on the small net | ✅ deterministic CSV + documented curves (`docs/ehw4_0_results.md`) |
| **EHW-4.1** | portable-C twin sharing the same fixed-point kernel and RNG | ✅ Py↔C bit-exact for all modes (`docs/ehw4_1_results.md`) |
| **EHW-4.2** | EHW-local RTL/firmware prep: copy/adapt `train_unit`, XBUS map, firmware stubs, optional OOC synth | RTL sim + firmware host stub + `verify-image` |
| **EHW-4.3** | board run: NEORV32 evaluates candidates with short HW-SGD inner loops | board mailbox curve matches host model |
| **EHW-4.4** | optional ICAP reveal: bake the best adapted weights into LUT-KCM or a spare-route island | board result equals post-adapt oracle |

EHW-4.0 and EHW-4.1 are complete. EHW-4.2 is the first board-bound preparation rung.

## Board Mailbox Sketch

Use compact tags so a slow U-Boot poll can still reconstruct the run:

```text
0xF4000000 | mode              boot / mode marker
0xF4100000 | gen<<8 | best_ok  generation best label count
0xF4200000 | sse_low16         generation best SSE
0xF4300000 | cand<<16 | ok     per-candidate post-adapt score, optional sparse
0xF4400000 | epochs            K actually applied
0xF45xxxxx                    final genome chunks / hash
0xF4F00000 | status            done / error code
```

Exact tags can change during implementation, but they must be documented in
`docs/board_results.md` before any board claim.

## Risks

- Firmware memory: EHW-4 combines GA population storage with training vectors and
  master weights. Keep `POP` small first and run `verify-image`; audit `.data+.bss`
  against 16 KB DMEM.
- False scientific claim: without holdout, this is a same-set deployment/adaptation
  metric. Say that plainly.
- Toolchain regression: use the tracked `image_gen` patch and `make verify-image`.
- Board-time blowup: HW-SGD inside every candidate multiplies eval time. Start with
  `POP<=16`, `GENS<=16`, `K<=1..4`, then scale only after mailbox evidence.
- Overfitting the inner loop: compare Baldwinian and Lamarckian modes to separate
  "evolves learnability" from "just runs SGD repeatedly."

## Next Task For ChatGPT

Implement EHW-4.2 board-bound prep:

- copy/adapt an EHW-local `train_unit` path rather than editing `zynq_xpart`;
- add RTL and firmware host stubs for one short HW-SGD inner loop;
- preserve the EHW-4.1 fixed-point contract exactly;
- keep firmware image verification in the build path;
- add host gates before any board claim.

The EHW-4.1 host twin already proves deterministic Python/C agreement for the
current oracle defaults, including Lamarckian writeback. A useful EHW-4.2
sub-experiment is a writeback clamp/decay comparison, because the current
Lamarckian best reaches 40/40 but leaves three INT8 weights on the saturation
boundary.
