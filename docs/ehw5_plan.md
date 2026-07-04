# EHW-5 Plan — Structure + Weights + HW-SGD Hybrid Evolution

Status: **CLOSED at EHW-5.4a.** EHW-5.2, EHW-5.3, and EHW-5.4a are
board-verified at FCLK0=50 MHz. EHW-5.4a's same-boot ablation met the stop rule;
5.4b parameter-window scans and 5.5 ICAP reveal are optional future demos.

EHW-5 is the "complete" hybrid line: combine the EHW-3 safe spare-routing island
with the EHW-4 HW-SGD memetic loop. A candidate genome carries both a small
contention-safe structure and the 24-byte INT8 seed weights; each candidate can
learn briefly through the board-verified train unit before selection.

The intended claim is deliberately narrow:

> A fixed-route FPGA design can evolve safe local structure and INT8 weights
> together, while a hardware SGD inner loop accelerates candidate evaluation.

It is **not** arbitrary routing-bit evolution, and it is **not** a holdout
generalization claim unless a separate holdout set is added.

## Inputs From Completed Lines

- EHW-3.2 proves a register-configured spare-routing VRC island on board.
- EHW-3.3 proves ICAP-baked repair of that island.
- EHW-3.4 proves per-eval internal ICAPE2 evolution of the spare-route genome.
- EHW-4.3 proves the EHW-local train unit on board.
- EHW-4.5 proves same-boot Baldwinian vs Lamarckian A/B.
- EHW-4.6a proves that parameter sweeps can be run as one baked table in one
  firmware image; the 12-point sweep is board-verified and byte-exact.
- EHW-4.6b proves a PS-writable parameter window: PS writes AXI `0x40000000`,
  NEORV32 reads XBUS `0xF5000000`, and live updates are visible without reboot.

These results meet the threshold for EHW-5: structure, weight evolution, and
hardware adaptation have all been proven separately on the same board family.

## Scientific Question

EHW-4 answers how weight inheritance semantics behave under HW-SGD. EHW-5 asks a
different question:

> Does evolving a small hardware-visible feature structure together with the seed
> weights produce candidates that adapt faster, converge with lower SSE, or recover
> better after an injected structural fault than weight-only memetic evolution?

The first metric is still the same 40-sample deployment/adaptation set used by
EHW-0/EHW-4. Say "same-set metric" unless a real split is introduced.

## First Hybrid Substrate

Start with the smallest structure that can matter while fitting the existing
EHW-4.3 `rm_memetic_train` resource envelope.

### Feature Island

Reuse the EHW-3 spare-routing genome contract:

```text
16 bytes = LUT4/LUT8 INITs + safe local path-select fields
```

The island computes one Boolean feature:

```text
phi = island(bit0, bit1, bit2)
```

The three input bits are derived from three scalar input features using fixed
thresholds. The first host oracle should keep thresholds fixed, for example:

```text
bit_i = (x_i >= 8)
```

Do not evolve thresholds in the first rung; it expands the search space and
weakens the evidence chain.

### Network Coupling

The current EHW-4 network is fixed at `4→4→2`, so the least invasive coupling is
a feature replacement:

```text
x'_0 = x_0
x'_1 = x_1
x'_2 = x_2
x'_3 = phi ? 16 : 0
```

This forces the evolved structure to matter. It also makes EHW-5 comparable to
EHW-4 only as a controlled ablation, not as a drop-in replacement for the original
four raw inputs. If host results show this coupling is too restrictive, EHW-5.0
may compare a small fixed menu before freezing the contract:

| Coupling | Meaning |
|---|---|
| `replace_x3` | use `[x0,x1,x2,phi]` |
| `bias_x3` | use `[x0,x1,x2,sat_i8(x3 + (phi ? +8 : -8))]` |
| `gate_x3` | use `[x0,x1,x2,phi ? x3 : 0]` |

Freeze exactly one coupling before the C twin.

## Hybrid Genome

First-rung genome:

```text
bytes  0..15   sr_genome          EHW-3 spare-route island
bytes 16..39   weight_genome      EHW-4 24-byte INT8 seed weights
```

Optional later extension:

```text
byte 40        coupling_mode or feature_slot
```

Do not add this byte until the host oracle proves it is needed. Keeping the first
contract at 40 bytes makes the C twin, firmware buffers, and mailbox packing
simple.

## Candidate Evaluation

For each candidate:

1. Decode and configure/evaluate the spare-route island to compute `phi`.
2. Transform the 40-sample input set into `x'`.
3. Load the 24-byte seed weights as Q8.8 master weights.
4. Run `K` HW-SGD adaptation epochs through the EHW-4 train-unit path.
5. Score the adapted genome on the transformed deployment set.
6. Select using the post-adapt score.
7. In Lamarckian mode, write adapted weights back; the structural genome is not
   gradient-updated and changes only through GA mutation/crossover.

For the first proof, run Lamarckian only after host A/B confirms the expected
behavior. Baldwinian can be restored as an ablation once the pipeline is stable.

## Resource And Address Plan

EHW-4.3 uses `array 16 DSP + train_unit 2 DSP = 18/20 DSP` in the RP pblock.
The EHW-3 spare-route island is LUT-based and should not add DSPs, but place is
the real gate.

Proposed combined RM address map:

```text
0xF0000000 + 0x000..0x3ff   existing 4x4 array / TPU accelerator
0xF0000000 + 0x400..0x7ff   spare-route VRC feature island
0xF0000000 + 0x800..0x930   memetic_train_unit
```

This is a new RM wrapper, not a static redesign. It forwards the existing array
window, claims a middle window for the spare-route VRC, and keeps the train unit
at the already board-verified `0xF0000800` window.

Mandatory gate before board:

- Vivado OOC synth for the combined RM.
- Place-level resource report for the RP pblock.
- DSP must stay `<=20`; expected DSP remains `18`.
- LUT/route congestion is a watch item because the train-unit RM was already tight.

## Milestone Ladder

### EHW-5.0 / 5.0b — Host Hybrid Oracle — DONE (HOST-ONLY)

Deliver:

- `sim/oracle_memetic_struct.py`
- `docs/ehw5_0_results.md`

Gate:

- deterministic fixed-seed run;
- weight-only EHW-4 baseline reproduced;
- hybrid structure+weights run reported with the same metrics as EHW-4.6a:
  final correct, final SSE, first 40/40, saturation count;
- structural fault injection optional but useful: disable A1 and show whether the
  hybrid can repair structure while preserving weight adaptation.

Result:

- EHW-5.0 proved the hybrid plumbing and exposed the caveat: unpressured feature
  evolution can exploit constants or near-constants.
- EHW-5.0b added feature-balance pressure. The best pressure arm
  (`hybrid_lamarckian_pressure` / `bias_x3`) reaches `40/40`, SSE `4513`,
  first_40 `2`, with a non-constant `15/40` feature mask and zero pressure
  penalty. This is enough to justify the EHW-5.1 C twin, while still remaining a
  same-set host result rather than a board claim.

### EHW-5.1 — Portable-C Twin — DONE (HOST-ONLY)

Deliver:

- `sw/ehw/memetic_struct_kernel.h`
- `sw/ehw/memetic_struct_eval.c`
- `tests/compare_memetic_struct_twin.py`

Gate:

- Python and C per-generation curves byte-exact;
- hybrid genome, feature transform, mutation, crossover, and HW-SGD fixed-point
  adaptation all covered;
- no board claim.

Result:

- `tests/compare_memetic_struct_twin.py` byte-compares Python and C curve CSVs
  and summary CSVs across the weight baseline, all three unpressured hybrid
  couplings, all three pressured hybrid couplings, and the no-adapt ablation.
- Feature-balance pressure is covered as selection semantics: the gate asserts
  the useful pressure arm (`bias_x3`, 15/40 feature ones, zero penalty) and the
  penalized degenerate `gate_x3` arm (`400000` penalty).
- The C twin preserves the key EHW-5.0b result: `hybrid_lamarckian_pressure /
  bias_x3` reaches `40/40`, SSE `4513`, first_40 `2`.
- See `docs/ehw5_1_results.md`.

### EHW-5.2 — Combined VRC + Train-Unit RM — DONE (HW-VERIFIED)

Deliver:

- `rtl/dfx/tpu_rp_rm_memetic_struct.v`
- optional small wrapper around `spare_route_vrc.v` if the address map needs a
  clean sub-window;
- `sw/ehw/memetic_struct_train_mbox.c` host stub;
- `tests/compare_memetic_struct_train.py`;
- Vivado OOC script for the combined RM.

Gate:

- Icarus RTL sim exercises spare-route config, feature readback, and train-unit
  MMIO adaptation;
- firmware host stub byte-exact vs the C twin;
- Vivado OOC and place/resource gates pass before any board run.

Result:

- `rtl/dfx/tpu_rp_rm_memetic_struct.v` maps the spare-route VRC feature island to
  `0xF0000400..0xF000047f` and maps the EHW-5.2 lite train-unit window to
  `0xF0000800..0xF0000934`.
- The first full-train-unit wrapper failed Claude's mandatory OOC resource gate
  (`5049/4400` LUT). The corrected wrapper uses `rtl/memetic_train_unit_lite.v`,
  with fixed `LR_SHIFT=7`/`K=2` and serialized W1/W2 updates behind a `BUSY` word.
- `tests/compare_memetic_struct_train.py` simulates the combined RM wrapper,
  loading the EHW-5.0b `bias_x3` feature genome and checking VRC marker/mask/output
  plus train-unit register access and serialized update behavior.
- `sw/ehw/memetic_struct_train_mbox.c` uses the same MMIO protocol to run one
  `bias_x3` transformed SGD epoch through the train unit and compares against a
  CPU golden.
- The feature genome's 8-row VRC mask is `0xa0`; this is intentional because it is
  a dataset feature from EHW-5.0b, not the EHW-3 majority target `0xe8`.
- Vivado is unavailable in the ChatGPT host environment, so OOC/place/resource
  gates remain mandatory Claude-side checks via `tests/vivado_ooc_memetic_struct.tcl`.
- Board result: PASS at the real signoff clock after pinning FCLK0 to 50 MHz from
  U-Boot. Final mailbox `0xF5F00000`: `mism=0`, `got_sse=gold_sse=4560`,
  `correct=38`, VRC marker `SRV0`, mask `0xa0`.
- Root cause of the earlier board FAILs was not the RM: miner U-Boot left FCLK0
  at 125 MHz while Vivado signed off 50 MHz. Future board claims must run
  `scripts/board-set-fclk50.py` before `fpga loadb`.
- See `docs/ehw5_2_results.md`, `docs/board_results.md`, and `docs/hw_notes.md`.

### EHW-5.3 — Board Hybrid Memetic Loop

Detailed task: `docs/ehw5_3_task.md`.

Delivered:

- board firmware running the selected EHW-5.1 contract;
- `docs/ehw5_3_results.md`;
- `docs/board_results.md` section.

Board gate result:

- one clean rebuilt bitstream, one boot;
- FCLK0 verified at 50 MHz before `fpga loadb`;
- NEORV32 evaluated hybrid candidates;
- spare-route feature was evaluated in fabric VRC, not software;
- HW-SGD inner loop used the board-verified train unit;
- final mailbox carousel matched host golden encoding bit-for-bit:
  `0xf5302028 / 0xf53111a1 / 0xf5320f00 / 0xf53f0002 / 0xf5f30000`.

Host + board result:

- `sw/ehw/memetic_struct_ga_mbox.c` implements the single-arm
  `hybrid_lamarckian_pressure / bias_x3` board firmware and host stub.
- `tests/compare_memetic_struct_ga_train.py` byte-compares the full
  per-generation curve against `sw/ehw/memetic_struct_eval.c`.
- Board summary matched the locked host golden: `40/40`, SSE `4513`,
  first_40 `2`, feature_ones `15`, penalty `0`, final `0xF5F30000`.
- See `docs/ehw5_3_results.md` and `docs/board_results.md`.

Suggested first parameters:

```text
POP=16
GENS=16 or 32
adapt_epochs=1
mode=Lamarckian
```

Do not start with POP=32 until firmware `.bss` and stack are audited.

### EHW-5.4 — Same-Boot Ablation

Detailed task: `docs/ehw5_4_task.md`.

Status: **BOARD-VERIFIED.** Deliverables: `sw/ehw/memetic_struct_ab_mbox.c`,
`tests/compare_memetic_struct_ab_train.py`, and `docs/ehw5_4_results.md`.

Run a single firmware image with at least three arms:

| Arm | Structure | Weights | Adaptation |
|---|---|---|---|
| W-only | fixed passthrough feature | evolved 24-byte genome | HW-SGD |
| S+W | evolved spare-route feature | evolved 24-byte genome | HW-SGD |
| S+W no-adapt | evolved spare-route feature | evolved 24-byte genome | none |

This answers whether the structural genome helps beyond EHW-4 and whether the
benefit depends on HW-SGD.

Preferred first table, locked by `sw/ehw/memetic_struct_eval.c` with seed `3`,
`POP=16`, `GENS=32`, `ADAPT=1`:

| Arm | Expected result |
|---|---|
| W-only Lamarckian | `40/40`, SSE `6116`, first_40 `3` |
| S+W pressure `bias_x3` | `40/40`, SSE `4513`, first_40 `2`, feature_ones `15`, penalty `0` |
| S+W no-adapt `gate_x3` | `40/40`, SSE `4615`, first_40 `11`, feature_ones `39` |

The fixed same-boot A/B is green. A future 5.4b can switch the arm/scan table to
the board-verified 4.6b parameter window (`PS 0x40000000` ->
`NEORV32 0xF5000000`) so parameter sweeps do not require firmware rebuilds, but
it is optional and not part of the closed EHW-5 claim.

### EHW-5.5 — Optional ICAP Reveal

EHW-5.3 showed a useful first hybrid result, and EHW-5.4a confirmed it under
same-boot ablation. A future optional demo may:

- bake the best structural feature into the spare-route island;
- optionally bake the adapted weights into LUT-KCM as in EHW-0.5;
- show a live transition from baseline to hybrid best without PS/NEORV32 reset.

This should reuse the EHW-3.3 and EHW-0.5 ICAP discipline. It is optional and
not required for the v1.1.0 EHW-5 closeout.

## Mailbox Sketch

Reserve `0xF5xxxxxx` for EHW-5:

```text
0xF5000005                  reached EHW-5 firmware
0xF510aagg                  arm aa, generation gg
0xF520aacc                  arm aa, best correct cc
0xF530ssss                  best SSE low16
0xF540mmmm                  feature truth mask / spare-route mask
0xF55pppcc                  point/candidate heartbeat, optional sparse
0xF5F0aacc                  arm final: arm aa, correct cc
```

Exact fields should be frozen in the result document before board work.

## Risks

- The first feature coupling may be too restrictive. Treat EHW-5.0 as a substrate
  search, not as a guaranteed board-bound result.
- Combined RM place may fail even if OOC synth passes. Board-side place is a hard
  gate because the EHW-4 train-unit RM already uses 18/20 DSP.
- Firmware memory can grow quickly: hybrid genomes are 40 bytes before evaluation
  buffers. Keep POP=16 first and report `text/data/bss`.
- Scientific framing: same-set deployment/adaptation metric only.
- Do not let the structural genome mutate raw Xilinx routing bits. The only legal
  structural fields are EHW-3 spare-route INIT/select bytes.

## Reviewer Notes

The cleanest first claim is not "better accuracy" because EHW-4 already reaches
40/40. Better citable outcomes are:

- fewer generations to first 40/40 at a fixed POP/adapt budget;
- lower final SSE at 40/40;
- fewer saturated INT8 weights after Lamarckian adaptation;
- graceful recovery under an injected spare-route fault;
- same-boot ablation where the only changed variable is structural evolution.
