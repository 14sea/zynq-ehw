# EHW-3 Plan — Evolved Spare-Routing Island

Status: **OPTIONAL NEXT LINE**. EHW-0 through EHW-2 remain the completed,
board-verified baseline. EHW-3.0/EHW-3.1 are host-only complete, and
EHW-3.2 / EHW-3.3 / EHW-3.4 are all board-verified — the whole EHW-3 ladder is
board-verified on the EBAZ4205.

## Goal

EHW-3 explores a controlled step beyond "LUT-INIT only" evolution without directly
mutating Xilinx 7-series routing bits.

The target claim:

> A fixed outer FPGA route contains a small spare-routing island. A fitness-driven
> search evolves both LUT truth tables and local spare-path selection bits, so the
> island can recover or reconfigure function after injected faults while staying
> contention-safe.

This is not arbitrary routing evolution. The FPGA placer/router still creates the
outer design once. EHW-3 evolves only configuration points inside a deliberately
constructed, safe island: LUT INITs, mux-select LUTs, enable masks, and spare-cell
selection.

## Why This Is The Right Next Boundary

EHW-2 proved per-eval on-chip ICAPE2 LUT-INIT evolution on live silicon. The
obvious next question is whether the phenotype can change more structurally than
a single truth table. Directly editing vendor routing bits is too risky for this
project's current evidence standard:

- routing-bit semantics are incomplete and device-specific;
- bad edits can create contention, floating nets, timing failures, or wedges;
- Vivado cannot validate timing/DRC after a live routing mutation;
- third-party reviewers would struggle to verify the safety boundary.

A spare-routing island gives most of the research value with a tractable proof:
the physical wires are fixed, but the active path through those wires is evolved.

## Research Questions

1. Can a small fixed-route island recover a target logic function after a disabled
   node, stuck-at output, or broken local path is injected?
2. How much spare capacity is needed for recovery: one spare LUT, one spare column,
   or a small crossbar?
3. Can the same host oracle, C twin, RTL sim, and board mailbox pattern used in
   EHW-0..2 prove the phenotype and recovery path cleanly?
4. Can the evolved repair be ICAP-baked into the island, preserving the live
   self-reconfiguration story?

## Non-Goals

- Do not mutate raw Xilinx routing bits.
- Do not claim arbitrary topology evolution.
- Do not depend on analog effects, metastability, or undocumented timing behavior.
- Do not replace the completed EHW-0..2 baseline or weaken its "LUT-INIT only"
  safety claim.
- Do not require a larger board before a minimal proof exists on XC7Z010.

## Candidate Substrate

Start with a small island, sized so it can be simulated, host-modeled, and
board-verified quickly.

Suggested first island:

- 3 input bits, 1 output bit for the minimal EHW-3.0 proof.
- 4 to 8 logic LUTs.
- 1 or 2 spare LUTs.
- A fixed local interconnect implemented from safe primitives:
  - LUT-based 2:1 muxes;
  - optional one-hot selects;
  - registered config words for VRC mode;
  - baked LUT/mux INITs for ICAP mode.
- Fault injection hooks:
  - force a node output stuck-at-0 or stuck-at-1;
  - disable one node;
  - force one local path select unavailable.

The important design rule: every possible config word must map to a legal,
single-driver circuit. Invalid select encodings should either be masked or decode
to a harmless default.

## Genome

The EHW-3 genome should be explicit and small:

```text
logic_init[i]     = LUT truth table for node i
route_sel[j]      = local mux selection for edge j
enable_mask[k]    = optional enable/bypass bit for node or spare path k
```

For the first proof, keep route choices coarse. A good starting point is:

- each non-input node chooses two sources from the previous column plus spares;
- output chooses one source from the final column;
- only LUT INITs and mux-select LUTs/config registers are evolved.

Use a validity layer in the oracle so mutation never produces contention. The
host model and RTL must share the same decode.

## Milestone Ladder

### EHW-3.0 — Host Oracle For Spare-Routing Recovery — DONE (HOST-ONLY)

Build a deterministic Python oracle for a tiny spare-routing island.

Deliverables:

- `sim/oracle_spare_routing.py`
- target functions: start with 3-input majority or 2-bit parity fragment;
- fault modes: none, stuck-at-0, stuck-at-1, disabled node;
- GA reaches the target function before and after one injected fault;
- result document: `docs/ehw3_0_results.md`.

Host gate:

- deterministic fixed-seed run;
- prints best genome, truth-table mask, fitness, and fault mode;
- confirms repaired phenotype equals the target truth table.

Implemented first target: 3-input majority `0xe8`. The no-fault run reaches
`8/8`; injecting `FAULT_DISABLE_NODE(A1)` drops the champion to `6/8`; a second
run with the fault active recovers `8/8` using spare `AS` / local rerouting. See
`docs/ehw3_0_results.md`.

### EHW-3.1 — Portable-C Twin And Firmware Stub — DONE (HOST-ONLY)

Port the island evaluator and GA contract to C.

Deliverables:

- `sw/ehw/spare_route_kernel.h`
- `sw/ehw/spare_route_eval.c`
- `tests/compare_spare_route_twin.py`

Host gate:

- Python vs C bit-exact generation curve;
- identical champion genome and repaired truth-table mask;
- fault injection contract tested in both implementations.

Implemented in `sw/ehw/spare_route_kernel.h`, `sw/ehw/spare_route_eval.c`, and
`tests/compare_spare_route_twin.py`. The gate compares both the no-fault and
post-fault recovery GA curves byte-for-byte, including direct fault-model masks.
See `docs/ehw3_1_results.md`.

### EHW-3.2 — Fabric VRC Island — BOARD-VERIFIED

Implement the island as register-configured fabric logic, analogous to
`rtl/cgp_vrc.v`, but with evolved local path selection.

Deliverables:

- `rtl/spare_route_vrc.v`
- optional DFX wrapper under `rtl/dfx/`
- firmware mailbox test, e.g. `sw/ehw/spare_route_vrc_mbox.c`
- RTL test: `tests/compare_spare_route_vrc.py`

Host gate:

- Icarus Verilog exercises config load, fault injection, input sweep, and output
  readback;
- firmware host stub matches the Python/C oracle;
- optional Vivado OOC synth gate, because previous RTL issues escaped iverilog.

Board gate:

- NEORV32 evaluates the island in fabric;
- with no fault: target truth table passes;
- with injected fault: GA recovers the target using spare route/logic choices;
- exact mailbox words logged in `docs/board_results.md`.

Implemented in `rtl/spare_route_vrc.v`, `rtl/dfx/tpu_rp_rm_spare_route_vrc.v`,
`sw/ehw/spare_route_vrc_mbox.c`, and `tests/compare_spare_route_vrc.py`; built by
`vivado/dfx/build_spare_route_vrc.tcl`. RTL sim + firmware host stub + Py/C oracle
gate pass; Vivado OOC synth passes (0 errors). **BOARD-VERIFIED on the EBAZ4205
(2026-07-01):** the full board gate is met — no-fault target passes (`0xe8`, 8/8),
injected `FAULT_DISABLE_NODE(A1)` degrades it (`0xc8`, 7/8), and the on-board-evolved
repair recovers (`0xe8`, 8/8) using the spare node AS. Exact mailbox words in
`docs/board_results.md`; see `docs/ehw3_2_results.md`.

### EHW-3.3 — ICAP-Baked Repair — BOARD-VERIFIED

Bake the evolved repair into LUT INITs and local select LUTs, then use ICAP to
change the live island from broken to repaired.

Deliverables:

- baked baseline and repaired RTL variants;
- Vivado build/edit script;
- frame-sequence extraction tooling reused from EHW-1.2/EHW-2;
- result document: `docs/ehw3_3_results.md`.

Host gate:

- predicts baseline broken mask and repaired mask exactly;
- proves only intended LUT/select INITs differ;
- checks multi-FAR framebank generation if any edited INIT spans multiple FARs.

Board gate:

- baseline island reports broken fitness under injected or baked fault;
- ICAP update changes only intended INIT/select bits;
- repaired island reports target fitness without PS/NEORV32 reset.

Implemented in `rtl/spare_route_baked.v`,
`rtl/dfx/tpu_rp_rm_spare_route_baked_base.v`,
`rtl/dfx/tpu_rp_rm_spare_route_baked_repair.v`,
`sw/ehw/spare_route_baked_post.c`, `tests/compare_spare_route_baked.py`,
`tests/vivado_ooc_spare_route_baked.tcl`, `vivado/dfx/build_spare_route_baked.tcl`,
and `vivado/dfx/spare_route_baked_edit_repair.tcl`. The host gate proves the
baseline hard-fault phenotype is `mask=c8`, `7/8`, the repaired phenotype is
`mask=e8`, `8/8`, and only the intended g0/g1/g2/g3/g4/g5/g7/g8/g11/g13/g14
LUT/select INITs differ. **BOARD-VERIFIED on the EBAZ4205 (2026-07-01):** 8 FAR
envelopes rewrote live `SRB0` from `mask=c8`, `7/8` to `mask=e8`, `8/8` without
PS/NEORV32 reset; see `docs/board_results.md` and `docs/ehw3_3_results.md`.

### EHW-3.4 — Per-Eval ICAPE2 Spare-Routing Evolution — BOARD-VERIFIED

Optional stretch: combine EHW-2's per-eval ICAPE2 loop with the spare-routing
genome. Each candidate evaluation writes the candidate's logic and local route
config through `rtl/xbus_icap.v`, then scores the live island.

This should only start after EHW-3.3 is hardware-verified. It multiplies the
framebank complexity and should reuse the generalized EHW-2 framebank generator.

Implemented host-prep deliverables in `sim/ehw34_icap_oracle.py`,
`sw/ehw/ehw34_icap_spare_route.c`, `rtl/ehw34_spare_route_target.v`,
`rtl/neorv32_soc_icap_sr.vhd`, `scripts/ehw34-framebank-pack.py`,
`scripts/ehw34-build-framebank-from-bits.py`, `tests/compare_ehw34_icap.py`, and
`vivado/icap_ehw34/build_ehw34_icap.tcl`. The host gate proves the four-candidate
bank reaches the repaired phenotype (`mask=e8`, `8/8`) and validates the 16-byte
genome framebank descriptor. The board-pass framebank uses 5278 words and is padded
to the 16384-word / 64KB EHW-3.4 framebuf. Board ICAPE2 verification passed on
2026-07-02 with steady mailbox `0xEC0308E8` on AXI-GPIO channel 2 (`0x41200008`):
best candidate index 3 (`repair`), fitness `8/8`, mask `0xe8`. This substrate has no
PS-HWICAP; PS only stages the framebank and reads GPIO mailboxes.

## Suggested First Target

Use a 3-input majority target first, not a multiplier:

```text
target mask = 0xe8
fitness max = 8/8 rows
```

Reasons:

- tiny truth table;
- already used in EHW-2;
- easy to decode from mailbox words;
- enough to prove recovery after one broken node/path.

After the first proof, add a 2-bit multiplier fragment or a 2-bit comparator.

## Fault Model

Use injected faults first. Real damage is neither needed nor desirable.

Minimum fault modes:

- `FAULT_NONE`
- `FAULT_STUCK0(node)`
- `FAULT_STUCK1(node)`
- `FAULT_DISABLE_NODE(node)`
- `FAULT_DISABLE_ROUTE(edge)`

The board result should state clearly that these are modeled hardware faults
inside the island, not physical destruction of FPGA resources.

## Verification Standard

EHW-3 should keep the existing project discipline:

- every board-bound step has a deterministic host oracle;
- Python, C, RTL, firmware stub, and board mailbox agree on the same truth table;
- Vivado synth/OOC is used for any new RTL when available;
- board facts and gotchas go into `docs/hw_notes.md`;
- exact board mailboxes go into `docs/board_results.md`;
- completed status is not claimed until hardware is run.

## Safety Rules

- No raw routing-bit edits.
- No frame writes outside the extracted intended LUT/select INIT frames.
- No ICAP foreground transfers for multi-frame writes.
- Always regenerate frame diffs from the fresh routed build; the target LUTs can
  move between builds.
- Keep PCAP/ICAP ownership rules from `docs/hw_notes.md`.
- Keep sibling projects read-only.

## Success Criteria

Minimal success:

- host oracle evolves a repaired island after an injected stuck-at or disabled
  node fault;
- C twin and RTL sim match exactly.

Hardware success:

- board fabric VRC island reports failure under fault and recovery after
  evolution, with exact mailbox evidence.

Full EHW-3 success:

- live ICAP update changes the broken island into the repaired island by editing
  only LUT/select INIT bits;
- optional stretch: per-eval ICAPE2 evaluates multiple repair candidates on live
  silicon.

## Likely Risks

- The route-select genome may make the search space too large for the small
  NEORV32 loop. Keep EHW-3.0 tiny and deterministic.
- A mux-rich island may cost more LUTs than expected on XC7Z010. Prefer a small
  proof over a broad architecture.
- Fault recovery can look trivial if the island has too much spare capacity.
  Report spare resources explicitly.
- It is easy to overclaim "routing evolution." Use "spare-route selection inside
  a fixed-route island" unless raw vendor routing bits are actually edited.
