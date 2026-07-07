# Future Plan After zynq-ehw v1.2.0

Status: planning note, not a board claim.

`zynq-ehw` is considered complete at v1.2.0. It should remain the
board-verified reference ladder for EHW-0 through EHW-5.5. Future work should
prefer a new repository unless it is a small bug fix, documentation correction,
or reproduction aid for the existing releases.

## Project Boundary

`zynq-ehw` should stay focused on the completed evidence chain:

- EHW-0..3.4: LUT/VRC/ICAP/internal-ICAPE2/spare-route ladder.
- EHW-4: memetic GA plus HW-SGD.
- EHW-5: structure plus weight hybrid evolution, A/B ablations, parameter
  window, and ICAP reveal.
- Board evidence, host gates, hardware notes, release notes, and replayable
  results.

New research lines should not be added to this repository by default. The next
project should be a clean continuation that imports lessons and selected tools
from `zynq-ehw` but does not blur the release history.

Recommended repository split:

```text
cyclone-fabric-cartographer
  first implementation continuation: autonomous loop on top of the validated
  Cyclone_CRAM_Mapper map/toolchain

zynq-autoehw
  follow-on autonomous Zynq node: live ICAP, long-running runtime, real demos
```

Alternative Zynq-side names:

- `zynq-ehw-autonode`
- `zynq-route-ehw`
- `zynq-evofabric`
- `ehw-autonode`
- `evofabric`

The first implementation repo should be `cyclone-fabric-cartographer` if this
roadmap is followed literally. It should be an autonomous continuation of
`Cyclone_CRAM_Mapper`, not a restart of that reverse-engineering work.
`zynq-autoehw` should start after that as the stronger runtime platform for
autonomous evolution on one Zynq board, with live ICAP and long-running
phenotype adaptation as later capabilities.

## Top-Level Direction

The long-term target is an FPGA node that can evolve without a PC in the
decision loop:

- adapt to peripherals and environment drift;
- adapt compute substrates for small models or signal-processing tasks;
- log, attest, recover, and replay its own evolution;
- later exchange genomes with other FPGA nodes.

The next project should start with one board. Multi-board island evolution
should remain future work in a separate project.

## Core Research Claim

The deeper target is autonomous fabric cartography:

> The system can characterize the reconfigurable physical phenotype of this
> chip inside a constrained island, build a device-local routing/logic map, and
> autonomously evolve hardware from that map.

This is different from building a full public bitstream database. The goal is
not to decode an entire FPGA family. The goal is to let one board learn which
local configuration tokens, templates, and combinations are safe, observable,
useful, stable, fast, slow, or fault-tolerant on this specific device.

The local map can be behavioral rather than symbolic. For example, it does not
need to name a route as a vendor PIP if it can reliably record:

```text
token_017:
  diff_hash: ...
  observed_behavior: source_A reaches sink_B
  delay_score: 31
  stable_50mhz: yes
  stable_100mhz: no
  compatible_with: [token_003, token_011]
```

This framing is important for FPGAs that do not have a prjxray-like open
routing database. It is also a fallback. Where a backend already has a stronger
symbolic model, such as `Cyclone_CRAM_Mapper`'s FASM/per-pip-cell map, the
symbolic model should be inherited and the behavioral map should record
measurements, stability, compatibility, and blacklist evidence around it.

## Execution Roadmap

The execution order should reduce risk before increasing phenotype freedom.
The recommended sequence is:

```text
0. Preserve zynq-ehw as the completed reference ladder.
1. Start a new repo and define common artifacts: local-map schema, run log,
   replay bundle, safety whitelist, and recovery protocol.
2. Run the Cyclone IV autonomous-continuation line first, inheriting the
   already-validated `Cyclone_CRAM_Mapper` map/toolchain and adding the closed
   loop around it.
3. Build the Zynq autonomous node using already-safe phenotype mechanisms.
4. Add open-routing evolution on the XC7K70T line after the safety/runtime
   vocabulary exists. Keep the single Zynq/EBAZ board to replay-only route
   validation unless a sacrificial duplicate board exists.
5. Feed route tokens into the autonomous node and run peripheral adaptation.
6. Add compute adaptation only after the control plane and map format are
   stable.
7. Treat Intel/Altera PR/CvP backends and multi-board work as later ports.
```

This means the first concrete work should not be new open-routing mutation and
should not be a large compute demo. The first concrete work should be a small,
auditable cartography loop that proves the map/log/replay abstraction on top of
an already validated backend.

The reason to put Cyclone IV early is practical: `Cyclone_CRAM_Mapper` and
`rot_tpu_handoff` already provide a non-prjxray, low-resource fabric-surgery
and mapping base. The new work is the autonomous loop:

```text
configure -> stimulate -> measure -> attest -> blacklist -> champion/replay
```

Zynq then becomes the stronger platform for the long-running autonomous node.

## Technical Lines

### A1: Autonomous On-Board Evolution Node

Do this before opening routing bits on Zynq.

Goal: prove the control plane.

The board should run the full loop locally:

```text
generate genome -> validate -> configure phenotype -> evaluate fitness
-> publish telemetry -> save champion -> recover or continue
```

The PC should only provide observation, first-load support, and optional task
updates. It should not choose candidates or compute fitness.

Use already-proven safe phenotype mechanisms first:

- LUT-INIT;
- local-select fields;
- VRC islands;
- HW-SGD adaptation;
- parameter window;
- ICAP writes that are already constrained by existing contracts.

Required safety features:

- golden image or golden phenotype reload;
- watchdog and timeout per candidate;
- FCLK0 preflight before board runs;
- persistent champion and run log;
- replay bundle for any reported champion;
- explicit PASS/HOLD publication words.

### A2: Peripheral Adaptation Demo

This is the most practical first application.

Candidate targets:

- UART/SPI sampling margin adaptation;
- noisy GPIO decoder;
- sensor calibration LUT;
- debounce/filter adaptation;
- packet parser or line-protocol tolerance;
- drift compensation for a simple signal source.

Fitness should be based on directly observable behavior:

- CRC/pass rate;
- packet error rate;
- latency and jitter;
- timeout count;
- residual error against a known signal;
- stability across temperature or clock changes.

Do not let evolution touch IO bank configuration, global clocks, PS/AXI static
logic, or unsafe electrical settings.

### A3: Compute Adaptation Demo

After the autonomous loop is stable, adapt a small compute substrate.

Candidate genome fields:

- operator choice;
- INT2/INT4/INT8 or mixed precision;
- sparsity mask;
- tiling factor;
- activation approximation;
- HW-SGD enable and adaptation budget;
- weights;
- optional route-template ID once route evolution exists.

Fitness should be multi-objective:

```text
fitness = accuracy_score
        - latency_penalty
        - resource_penalty
        - power_or_temperature_penalty
        - instability_penalty
```

Start with a small real task, not an arbitrary large model:

- anomaly detection;
- packet classifier;
- tiny sensor classifier;
- simple keyword or event detector;
- control-loop predictor.

### R1: Whitelisted Route-Template Evolution

Do this after the autonomous safety/runtime layer exists, or in parallel as a
separate experimental branch. Do not start with raw routing bit mutation.

Platform rule: open-routing experiments should target the XC7K70T/Kintex-7
line with sacrificial boards. The single Zynq/EBAZ board should not be the
default routing-experiment platform. On Zynq, route work should remain limited
to Vivado-legal template replay and evidence gathering unless a duplicate
sacrificial board is available.

The safe framing is:

```text
Vivado legal routed variants -> prjxray/bitdiff extraction
-> whitelisted PIP/frame templates -> genome selects templates
```

R1 should be staged:

1. Generate legal routed variants with fixed placement and tiny pblocks.
2. Extract changed PIPs and frame bits with prjxray/bitread/diffs.
3. Build a route-template manifest and frame whitelist.
4. Replay route A -> route B on board with ICAP.
5. Add route-template IDs to a small GA genome.
6. Later add route-template IDs to the autonomous node genome.

Hard rules:

- no raw random routing bit flips;
- no non-whitelisted PIPs;
- no clock/HCLK/CMT/BUFG edits;
- no PS, AXI, IO, BRAM, DSP, or static net edits;
- one driver per routing node;
- readback or attestation after writes;
- golden reload path tested before experiments.

Routing should be treated as one optional phenotype dimension, not as the
foundation of every later demo.

### X1: Vendor-PR Backends Without prjxray

For Intel/Altera devices such as Stratix V, do not assume a prjxray equivalent
exists. The safer backend strategy is:

> Use official PR/CvP to establish a safe reconfigurable region, then let the
> kernel learn a device-local map by black-box experiments.

The first version should rely on legal personas or partial images generated by
Quartus. The runtime genome can select and evaluate those personas without
understanding bit-level routing semantics:

```text
Quartus legal personas -> PR/CvP load path -> black-box measurement
-> device-local map -> map-based evolution
```

This gives a path for FPGA families without open bitstream documentation:

1. Use vendor tools to create the safe island and a few seed personas.
2. Run the same cartographer loop: configure, stimulate, measure, attest, log,
   and blacklist bad candidates.
3. Learn behavioral tokens and compatibility relations from observations.
4. Evolve only over tokens already proven safe on that board.
5. Treat any later bit-level fuzzing as a tightly constrained extension inside
   the vendor PR region, not as global random mutation.

This backend should be considered after the Zynq version proves the common
cartography/runtime model. A Cyclone V SoC or Arria V SoC may be a lower-risk
Intel/Altera bridge than Stratix V because the hard processor system is closer
to the Zynq control-plane model.

### C4: Cyclone IV Autonomous Cartography Continuation

The Cyclone IV work is already far past a first probe. `Cyclone_CRAM_Mapper`
has a substantial symbolic map/toolchain, including CRAM geometry, LUT/route
codecs, FASM/RBF tooling, route sig-cache, safety gates, and validated
zero-Quartus paths. `rot_tpu_handoff` validated a consumer path for trust-gated
dynamic loading and real fabric LUT surgery on AX301 / EP4CE10.

The next line should therefore be an autonomous continuation layered on that
base, not a restart:

```text
candidate .rbf -> volatile CRAM configuration -> black-box measurement
-> device-local map -> optional EPCS checkpoint for selected champions
```

This is not a live-ICAP path. Cyclone IV E parts such as EP4CE6/EP4CE10 do not
provide the same self-reconfiguration model as Zynq 7-series. The useful claim
is therefore not "live XPART on Cyclone IV"; it is:

> A small non-prjxray FPGA can still support constrained fabric cartography
> through safe RBF edits, volatile CRAM configuration, and black-box
> measurement.

The important boundary is more precise than "routes are not mined." Individual
route and IOB paths have been mined and validated in the existing Cyclone work.
The dangerous case is composition: LI-MUX values can be global-routing
dependent, and independent route/canon/template edits may not compose
byte-identically. The autonomous line should treat composition as the hard
problem, not rediscover the single-token map.

For high-frequency candidate evaluation, do not use EPCS as the hot path.
Re-validate a volatile configuration transport first, such as Passive Serial or
a re-proven JTAG flow, so the candidate bitstream can enter configuration SRAM
without consuming flash erase/program cycles. EPCS staging remains useful for
golden images, selected champions, checkpoints, and release evidence.

Recommended Cyclone IV stages:

Run this line before the Zynq route-template line. Recommended stages:

1. `C4-AFC0`: inherit the validated `Cyclone_CRAM_Mapper` artifacts into a new
   repo: sig-cache, CRC repair, safe byte ranges, fresh-gold gates, editable
   allowlists, and known DO_NOT_FLASH caveats. Do not modify the original
   `Cyclone_CRAM_Mapper` or `rot_tpu_handoff` repos.
2. `C4-AFC1`: define the portable map/log/replay schema and wrap existing
   C4 tools with a candidate lifecycle: generate, validate, configure, measure,
   attest, blacklist, and replay.
3. `C4-AFC2`: host-driven black-box cartography over a tiny island, using the
   existing symbolic map where available and behavioral observations only for
   not-yet-symbolized tokens.
4. `C4-AFC3`: local-map-based LUT/token evolution with explicit composition
   gates.
5. `C4-AFC4`: replace the PC controller with a small MCU or SBC for candidate
   generation, configuration, logging, and recovery only after the transport is
   measured and flash budget rules are enforced.

Do not spend EP4CE6/EP4CE10 resources on a large autonomous softcore runtime
unless there is a specific reason. These devices are better used as measured
fabric probes; the controller can live outside the FPGA.

Hard limits for this line:

- no live partial reconfiguration claim;
- no raw full-RBF random mutation;
- no clock, PLL, IO bank, JTAG, or configuration-pin fuzzing;
- no high-frequency EPCS flash writes in the candidate-evaluation loop;
- no route-template composition unless the gold-anchored composition gate
  proves byte identity or the candidate is explicitly marked experimental and
  not flashed;
- abnormal candidates must be blacklisted permanently;
- every experiment must retain a golden RBF/EPCS recovery path.

## Risk Register

These risks should be treated as design constraints, not after-the-fact debug
notes.

### C4 Flash Wear-Out

Cyclone IV E lacks a Zynq-like ICAP path. If every candidate is staged into
EPCS/SPI flash and cold-booted, a GA run can consume flash endurance quickly.
The future line should inherit the existing flash-budget tracker rather than
invent an informal limit.

Mitigation:

- high-frequency candidate evaluation should use a volatile configuration path
  only after that path is re-validated on the target board;
- JTAG volatile configuration is not assumed working by default. Existing
  EP4CE6 notes record a falsified JTAG Phase 2, so any JTAG hot path must be
  treated as pending until re-proven;
- Passive Serial or another external controller path may become the preferred
  hot path, but it must be measured before the plan calls it load-bearing;
- EPCS writes are reserved for golden images, selected champions, checkpoints,
  and release evidence;
- the C4 run log should record whether a candidate was volatile-loaded or
  persisted;
- any MCU/SBC controller should enforce the inherited flash-budget state before
  issuing writes.

Virtual JTAG should not be described as the primary blank-device configuration
path. It is useful after user logic exists, but the candidate bitstream still
needs a real configuration transport.

### Local-Map State Explosion

A naive `compatible_with` matrix grows as `O(N^2)` and will not fit small
controllers or softcore memories. It also encourages false certainty about
untested combinations.

Mitigation:

- local maps must be sparse graphs, not dense compatibility matrices;
- every token must carry a `spatial_scope` such as tile, switchbox window,
  pblock, CRAM byte range, or backend-defined island region;
- record only observed dependencies, observed conflicts, and measured metrics;
- unknown combinations remain unknown and must not be treated as compatible;
- candidate blacklists should use compact hashes or diff fingerprints;
- Zynq, Cyclone IV, and future PR/CvP backends may define different spatial
  scopes, but all must avoid global unbounded compatibility tables.

### Route-Template Composition Hazard

Vivado-generated route templates are legal individually. They are not
automatically legal when composed by a GA. Two templates can share mux groups,
internal routing nodes, or selection fields in ways that are symbolically
plausible but physically unsafe.

The Cyclone IV evidence strengthens this rule: individual routes can be mined
and byte-identical, while composition can fail because LI-MUX values are
global-routing-dependent. Treat "single template is valid" and "template set is
composable" as separate claims.

Mitigation:

- the first defense is a pre-write validity checker, not readback alone;
- every route mux group must have at most one selected input;
- every routing node must have at most one driver;
- template bit overlaps must be either identical and declared compatible, or
  rejected;
- unknown overlaps are `HOLD`, not best-effort writes;
- all candidate diffs must be contained in the allowed bit whitelist;
- readback/attestation remains mandatory, but it only proves the expected bits
  were written. It does not prove the route composition is electrically safe.

Recovery should reload a known-good configuration if a candidate misbehaves.
A logic reset may clear a stuck FSM, but it cannot remove a bad routing
configuration from configuration SRAM. The fallback order should be:

```text
logic reset -> known-good partial/golden reload -> full bitstream reload
```

On Cyclone IV, the corresponding recovery is volatile load of the golden RBF,
with EPCS golden recovery kept as the last resort.

## Directions Discussed

The following directions were discussed. They are grouped by where they belong.

### Good Fit For The New Zynq Project

- Autonomous node runtime.
- Route-template replay on Zynq, not open-routing mutation on the only board.
- Fitness holdout and anti-cheat tests.
- Fault injection and self-healing.
- Eval/sec and ICAP bandwidth measurement.
- Evolution log and replay format.
- Small benchmark tasks that directly exercise the autonomous loop.
- Basic power/thermal-aware fitness if telemetry is available.
- Autonomous fabric cartography inside a constrained island.

### Better As A Nearby Cyclone IV Continuation Project

- Cyclone IV autonomous cartography continuation on top of
  `Cyclone_CRAM_Mapper`.

This should not be folded into `zynq-autoehw`. It should share map/log/replay
schemas with the Zynq project, but keep its RBF/transport/recovery mechanics in
a separate repository.

### Better As A Nearby Kintex-7 Route Project

- XC7K70T/Kintex-7 open-routing evolution using sacrificial boards.

This is where route-template evolution should become board-facing. The Zynq
board can share contracts and replay tools, but should not be the default
platform for open-routing experiments.

### Better As Separate Tooling Repositories Later

- Bitstream safety verifier.
- prjxray route-template extractor.
- EHW DSL or contract generator.
- Hardware-in-the-loop CI infrastructure.
- Cross-device portability framework.
- Vendor-PR backend adapters once the common cartography interface stabilizes.

These should first be prototyped inside the new project if needed. Once the
interfaces stabilize, split them out.

### Better As Separate Application Repositories

- Specific peripheral products or sensor demos.
- Security or moving-target-defense experiments.
- Multi-FPGA island evolution.
- Cross-board distributed genome migration.

These introduce new task domains, threat models, or network protocols. Keeping
them separate will preserve the clarity of both `zynq-ehw` and the new
autonomous-node project.

## Suggested Repository Layout For Future Work

Possible eventual split:

```text
zynq-ehw
  completed board-verified reference ladder, v1.0.0..v1.2.0

zynq-autoehw
  single-board autonomous evolution, route templates, real demos

ehw-tools
  safety verifier, route-template extractor, frame whitelist checker,
  replay/log parser, contract generator

ehw-bench
  benchmark definitions, datasets, holdout/adversarial cases, golden CSVs

ehw-islands
  future multi-FPGA genome exchange and island-model evolution

ehw-apps
  concrete peripheral and compute adaptation applications

cyclone-fabric-cartographer
  optional Cyclone IV line: autonomous continuation of Cyclone_CRAM_Mapper,
  volatile configuration only after re-validation, optional EPCS checkpoints,
  and local-map evolution on EP4CE6/EP4CE10

k7-openroute-ehw
  optional Kintex-7 line: route-template evolution on sacrificial XC7K70T
  boards using prjxray-db coverage and zynq-ehw-derived safety contracts
```

Do not create all of these immediately. Start with `cyclone-fabric-cartographer`
if the goal is to validate autonomous fabric cartography first. Start
`zynq-autoehw` after the map/log/replay contracts have survived the Cyclone IV
continuation. Split tools only after the contracts are stable.

## Inter-Project Contracts

Projects should interact through artifacts, not ad hoc source-tree references.

Recommended artifacts:

- genome contract: field layout, decode rules, valid ranges;
- phenotype manifest: bitstream hash, pblock, allowed FAR/word/bit set,
  route-template IDs, LUT/select contract;
- frame whitelist: candidate diffs must be fully contained in allowed bits;
- run log: board ID, FCLK, temperature, generation, genome hash, fitness,
  phenotype hash, mailbox words, frame-diff hash;
- replay bundle: genome, manifest, expected mailbox words, optional framebank,
  and load/run script.

This keeps `zynq-ehw` as a reference source and prevents future projects from
silently depending on mutable internal paths.

## Immediate Next Step

Create a new repository, preferably `cyclone-fabric-cartographer`, with:

1. a short `README.md` explaining that it is a post-`zynq-ehw v1.2.0`
   fabric-cartography continuation, not an extension of the finished EHW
   ladder;
2. `docs/design.md` describing the constrained-island cartography model;
3. `docs/schema.md` defining the portable artifacts:
   - local-map entries;
   - run logs;
   - replay bundles;
   - safety whitelist records;
   - candidate blacklist records;
4. inherited Cyclone IV artifacts copied into the new repo, with provenance
   recorded and without modifying `Cyclone_CRAM_Mapper` or `rot_tpu_handoff`:
   - route sig-cache and relevant symbolic-map artifacts;
   - CRC repair wrapper;
   - safe byte ranges;
   - fresh-gold gates;
   - editable-LE allowlist loader;
   - flash-budget state and recovery notes;
   - volatile configuration notes marked pending until re-validated;
5. a first milestone:

```text
C4-AFC0: inherit validated Cyclone_CRAM_Mapper contracts and define the
map/log/replay wrapper.
```

After `C4-AFC0` and `C4-AFC1` prove the map/log/replay vocabulary, create
`zynq-autoehw` and start:

```text
A1.0: single-board autonomous loop using an already-safe phenotype.
```

Only after A1 is stable should Zynq route-template replay or K7 open-routing
evolution become a board-facing milestone.
