# EHW-3.0 task — host oracle for spare-routing recovery (Claude → ChatGPT)

Handoff for the first buildable rung of `docs/ehw3_plan.md`. **Scope: host-only**
(no board this rung). Deliver `sim/oracle_spare_routing.py` + `docs/ehw3_0_results.md`.

The point of this file is to **pin the decode contract now**, so the C twin (EHW-3.1),
the fabric VRC (EHW-3.2), and the ICAP bake (EHW-3.3) can all share one bit-identical
genome→phenotype decode. Treat every numbered contract point below as frozen unless you
rebut it from source; you own the Python implementation and may refine anything marked
*(your call — document it)*.

## Target

- Function: **3-input majority**, truth mask **`0xe8`** (output = 1 when ≥2 of 3 inputs
  are high). 8 rows, `fitness_max = 8`.
- Same target the EHW-2 rung used → easy mailbox decode later, minimal truth table.

## Fixed-route island (physical wires fixed; only INITs + selects evolve)

Two logic columns, one spare, one output node. The **C1 nodes are 2-input LUTs (4-bit INIT)**;
the **output node O is a 3-input LUT (8-bit INIT)** — see the representability note below.

```
inputs:  x0 x1 x2
source pool P (index 0..4):  [ x0, x1, x2, ZERO(=0), ONE(=1) ]

column C1 (logic):  A0  A1  A2   + spare AS      (4 nodes)
  each node = 2-input LUT4(INIT) with two input muxes, each mux selects from P

column C2 (output): O                             (1 node)
  O = 3-input LUT8(INIT) with three input muxes, each mux selects from {A0,A1,A2,AS}

output = O
```

Rationale: a 3-node majority needs ~3 logic nodes; the spare `AS` + O's **3-of-4** output
select over `{A0,A1,A2,AS}` is exactly the spare capacity that makes fault-recovery
non-trivial but still small enough to brute-force all 8 rows per eval.

### Representability (verified — this is why O is a LUT8, not a LUT4)

The original draft made O a **2-input LUT4**. That substrate **cannot** represent 3-input
majority `0xe8`: a 2-input output LUT reads only two C1 signals, and MAJ(x0,x1,x2) depends
symmetrically on all three, so no genome reaches 8/8 (exhaustive enumeration: 0 solutions,
fitness capped at **7/8**; the spare `AS` does not help because O still only reads two of the
four C1 outputs). This was caught before any oracle was written. Fix: O reads **three** of the
four C1 outputs through a 3-input LUT8 — verified representable (8/8 solutions exist by
exhaustive search). **Keep the target `0xe8`; do not weaken it to a 2-input-representable mask.**

## Genome (frozen byte contract — C/RTL must match exactly)

```text
logic_init[0..3] : 4-bit INIT for A0, A1, A2, AS   (LUT4, 4 valid rows; upper bits unused=0)
init_out         : 8-bit INIT for O                (3-input LUT8)
node_sel[i][m]   : source select for node i (0..3), input m (0..1)  -> index into P (0..4)
out_sel[m]       : source select for O, input m (0..2)              -> index into {A0,A1,A2,AS} (0..3)
```

- Choose a concrete packed byte layout and document it at the top of `oracle_spare_routing.py`
  as the canonical contract *(your call — document it; C/RTL will copy it verbatim)*.
- LUT INIT convention, fix the input ordering now so C twin and `rtl/spare_route_vrc.v`
  decode identically:
  - C1 nodes (2-input): `out = (INIT >> (in1<<1 | in0)) & 1`
  - O (3-input):        `out = (INIT >> (in2<<2 | in1<<1 | in0)) & 1`
  where `in0/in1/in2` are the values selected by `*_sel[0]/[1]/[2]`.

## Validity layer (this is the contention-safety guarantee — do not skip)

- Every mux is a **pure fan-in selector**, never a bus → structurally single-driver, so no
  config word can ever create contention. This is what replaces "LUT-INIT-only" as the
  EHW-3 safety story; make it explicit in the results doc.
- Any `node_sel` value `>= len(P)` decodes to `ZERO`. Any `out_sel >= 4` decodes to `A0`.
  (Pick harmless defaults; the rule is: **every possible genome maps to a legal circuit**.)

## Fault model (injected/modeled, applied at eval — NOT part of the genome)

```text
FAULT_NONE
FAULT_STUCK0(node)        # node output forced 0
FAULT_STUCK1(node)        # node output forced 1
FAULT_DISABLE_NODE(node)  # node output forced 0 AND removed as a valid source (evolve around it)
FAULT_DISABLE_ROUTE(edge) # one named mux forced to its default source (evolve an alternate path)
```

Faults are applied inside the island evaluator. The results doc must state clearly these are
**modeled faults inside the island, not physical destruction of FPGA resources.**

## Recovery protocol the oracle must demonstrate

1. Evolve with `FAULT_NONE` → reach 8/8. Record champion genome + phenotype mask.
2. Inject `FAULT_DISABLE_NODE(A1)` into that champion → show fitness drops below 8.
3. **Re-evolve with the fault active** → GA recovers 8/8 by using the spare `AS` and/or
   rerouting the output mux.
4. Assert: repaired phenotype mask == `0xe8`; and the repair changed **only** `logic_init` /
   `*_sel` fields (never any physical routing) — print a field-level diff old→repaired.

## Host gate (EHW-3.0 acceptance)

- Deterministic, fixed seed; single `python3 sim/oracle_spare_routing.py` run prints:
  champion genome, phenotype mask, fitness, fault mode, and gens-to-solve for **both** the
  no-fault and the post-fault recovery runs.
- `docs/ehw3_0_results.md` records both runs verbatim + the field-level repair diff.
- No golden cross-check exists for this new substrate (it's not the m753/CGP net), so the gate
  here is: deterministic reproduction + phenotype==target + the recovery demonstration. The
  golden cross-check obligation returns at EHW-3.1 (Py↔C bit-exact).

## Out of scope this rung (later ladder)

- C twin + `tests/compare_spare_route_twin.py` → EHW-3.1.
- `rtl/spare_route_vrc.v` + board mailbox → EHW-3.2.
- ICAP bake broken→repaired → EHW-3.3.
- Keep the genome decode identical across all of them; that's why it's frozen here.

## What I (Claude) will do when your drop lands

Run the host gate, review decode consistency, check isolation + gitignore, then commit with
honest attribution. If the decode is ambiguous or a genome can produce a multi-driver/illegal
circuit, I'll send `review.v1.txt` (file:line + a decisive test) rather than commit.
