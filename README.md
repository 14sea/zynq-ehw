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

## License

Apache-2.0 (see `LICENSE` / `NOTICE`). NEORV32 (BSD-3) is fetched, not vendored; prjxray/Vivado are external tools; the `ref/` papers are gitignored.

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
- **EHW-0.3 HW-VERIFIED on EBAZ4205** — `sw/ehw/ehw_ga_mbox.c` runs the GA resident
  on NEORV32 over the 4×4 register-loaded VRC array; on silicon it converged 40/40
  with the champion genome **bit-identical to the host oracle** (`docs/board_results.md`).
- **EHW-0.4 host comparison done** — `sim/ehw0_4_compare.py`: on the same 40-sample
  evaluation set, the GA champion scores 40/40 labels vs the M7.5.3 gradient-trained
  tile's 37/40 after INT8 quantization (`docs/ehw0_4_results.md`). This is a
  deployment-set metric, not a holdout generalization claim.
- **EHW-0.5 HW-VERIFIED on EBAZ4205** — the EHW-0.3 evolved W1 tile was **ICAP-baked
  into the LUT-KCM fabric, live** (PS/NEORV32 never reset); mailbox
  `0x1019391F → 0x80AF7FF2`, bit-exact to the VPU-model golden, attested
  (`sw/ehw/lutkcm_post.c`, `vivado/dfx/build_lutkcm.tcl`).
- **EHW-1.0 host oracle done** — `sim/oracle_cgp.py` + `tests/compare_cgp_twin.py`:
  CGP GA evolves the four output LUTs of a fixed pass-through scaffold in a
  fixed-routing 3×4 LUT4 grid into a 2-bit multiplier, 16/16 rows, Python/C
  bit-exact.
- **EHW-1.1 HW-VERIFIED on EBAZ4205 (software-eval)** — `sw/ehw/cgp_ga_mbox.c` runs
  the CGP GA resident on NEORV32 → 2-bit multiplier 16/16, champion bit-identical to
  host. ⚠️ **This evaluates the LUT grid in NEORV32 *software*, not in a fabric VRC
  substrate.** The fabric-CGP version is EHW-1.1-fabric below.
- **EHW-1.1-fabric HW-VERIFIED on EBAZ4205** — `rtl/cgp_vrc.v` implements the CGP grid
  as real config-loaded fabric LUTs behind an XBUS register map; the board-resident GA
  evaluated fitness **on the fabric VRC** (MMIO drive) and evolved the 2-bit multiplier
  to 16/16 rows, champion bit-identical to host (`docs/board_results.md`). This is the
  true fabric substrate — the evolved circuit *is* hardware, vs EHW-1.1-sw's software eval.
- **EHW-1.2 HW-VERIFIED on EBAZ4205** — `rtl/cgp_baked.v` bakes the evolved CGP
  multiplier as LUT4 INITs; on silicon, ICAP rewrote only the 4 logic LUTs (n8..n11)
  to transform a **broken 7/16 multiplier into a perfect 16/16**, live (PS/NEORV32
  never reset). mailbox `0xe3000007→0xe3000010` (rows 7→16). The CGP analogue of
  EHW-0.5 (`docs/board_results.md`). Also fixed a real `m75-build-frameseqs.py`
  anchoring bug (duplicate frame start for identical-diff frames).
- **EHW-2 stretch HW-VERIFIED on EBAZ4205** — authentic Thompson **per-eval on-chip
  ICAPE2** evolution: `sw/ehw/ehw2_icap_micro.c` runs on NEORV32 and, every fitness
  evaluation, streams a candidate LUT-INIT frame sequence through `rtl/xbus_icap.v`
  (fabric ICAPE2) to **rewrite a live LUT in the running bitstream**, then scores the
  edited LUT. Converged to the target (mailbox `0xeb0308e8` = candidate e8, fitness
  8/8, mask `0xe8`). Key fix: the LUT INIT spans two config FARs, so each candidate
  needs **two** frame envelopes (the multi-FAR 8KB framebank) — a single envelope left
  the 2nd frame as a non-committed pad (`docs/board_results.md`).
- **EHW-3.0 host oracle done** — `sim/oracle_spare_routing.py` starts the optional
  spare-routing-island line: fixed outer routing, evolved LUT INITs plus local
  mux-select fields. It demonstrates host-side recovery from injected
  `FAULT_DISABLE_NODE(A1)`: no-fault majority `8/8`, degraded `6/8`, repaired
  `8/8` using spare `AS` / local rerouting (`docs/ehw3_0_results.md`). No board
  claim is made for EHW-3 yet.
- **EHW-3.1 host twin done** — `sw/ehw/spare_route_kernel.h` and
  `sw/ehw/spare_route_eval.c` mirror the EHW-3.0 Python oracle. The host gate
  compares Python and C no-fault/recovery GA curves byte-for-byte, including the
  repaired genome and direct fault-model masks (`tests/compare_spare_route_twin.py`,
  `docs/ehw3_1_results.md`).
- **EHW-3.2 board-verified** — `rtl/spare_route_vrc.v` implements the spare-routing
  island as a register-configured fabric VRC with evolved local path-select fields.
  RTL sim + firmware host stub + Py/C oracle + Vivado OOC synth (0 errors) all pass,
  and the full fault→recovery narrative was captured on the EBAZ4205 (XC7Z010): no-fault
  majority `8/8` mask `0xe8`, injected `FAULT_DISABLE_NODE(A1)` degrades to `7/8` mask
  `0xc8`, on-board-evolved repair recovers `8/8` mask `0xe8` routing the spare node AS
  (`docs/board_results.md`, `docs/ehw3_2_results.md`).
- **EHW-3.3 board-verified** — `rtl/spare_route_baked.v` bakes the EHW-3 spare-route
  island into explicit LUT/select INITs with a hard disabled-A1 fault. On the EBAZ4205,
  a live ICAP LUT-INIT edit of 8 frames (only the intended g0-g5/g7/g8/g11/g13/g14 INITs)
  rewrote the island from broken (`mask=0xc8`, `7/8`) to repaired (`mask=0xe8`, `8/8`)
  with the marker staying `SRB0` and no PS/NEORV32 reset — the CGP-analogue of EHW-1.2
  for the spare-routing island (`docs/board_results.md`, `docs/ehw3_3_results.md`).
- **EHW-3.4 board-verified** — stretch flow combining EHW-2's internal-ICAPE2
  per-eval loop with the EHW-3 spare-routing genome. `sw/ehw/ehw34_icap_spare_route.c`
  streams a staged candidate framebank (64KB framebuf, board-pass bank 5278 words) through
  `rtl/xbus_icap.v`, then scores the live `rtl/ehw34_spare_route_target.v` island. On
  the EBAZ4205 the per-eval ICAPE2 loop converged to the repair candidate — mailbox
  (AXI-GPIO ch2 `0x41200008`) steady `0xEC0308E8` (best idx 3, fitness 8/8, mask 0xe8);
  build timing met, DRC 0-err, BRAM 37/60 (`docs/board_results.md`,
  `docs/ehw3_4_results.md`). This build intentionally has **no PS-HWICAP**.

## Dependencies & reproduction environment

Developed and hardware-verified against these versions (others may work; this is the tested baseline). Host gates need only Python + a C compiler + iverilog; the board/ICAP flows additionally need Vivado, the RISC-V toolchain, prjxray, and the EBAZ4205.

| Tool | Version | Used for |
|---|---|---|
| Python | **3.12** + numpy | host oracles, framebank/ICAP tooling, mailbox decoders |
| C compiler (host) | any C99 (gcc/clang) | portable-C twins (`-DEHW*_HOST_STUB`) |
| Icarus Verilog | **12.0** | RTL host gates (`cgp_vrc`, `cgp_baked`, `ehw2`) |
| Vivado | **2025.2** | bitstream builds (`vivado/dfx/*.tcl`, `vivado/icap_ehw2/*.tcl`) + optional OOC `synth_design` gate |
| RISC-V GCC | `riscv64-unknown-elf-` 13.x | NEORV32 firmware (`sw/ehw/*.c` → IMEM) |
| picolibc | system pkg | firmware libc (`-specs=picolibc.specs`); `setup-deps.sh` applies the errno-guard patch |
| NEORV32 | **v1.12.9** | soft-core RTL + libs; fetched by `scripts/setup-deps.sh` (gitignored, not vendored) — BSD-3 licensed upstream |
| prjxray + DB | f4pga | LUT-INIT frame location for ICAP edits (`/home/test/prjxray` + `/home/test/prjxray-db`, part `xc7z010clg400-1`) |
| Board tooling | — | openocd `ebaz4205.cfg`, `uboot-intercept.py`, `/dev/ebaz-uart` — live in `/home/test/xilinx`, NOT this repo |
| Board | EBAZ4205 (XC7Z010) | JTAG = Digilent HS3 (FT232H); UART = CH340 @115200; original miner U-Boot, break key `d`; SRST not wired (reset via SLCR) |

Two large dependencies are kept **out of the repo** (gitignored, regenerable): the NEORV32 source (`rtl_src/`, via `scripts/setup-deps.sh`) and Vivado build outputs (`vivado/**/build/`). `external/` (read-only snapshots of the sibling projects) and `ref/` (copyrighted papers) are also gitignored.

## Host tests (no board, no Vivado)

```sh
tests/run_host_gates.sh      # runs all 9 host gates: oracle<->C-twin bit-exact + RTL sims
```
Every board-bound deliverable ships with a host self-proof; this is the gate that must be green before any board run (see `docs/workflow.md`). Board reproduction (build → ICAP/load → mailbox) is in `docs/BOARD_REPRO.md`.

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
- `docs/WRITEUP.md` — paper-style report: problem, approach, results, prior-art diff,
  limitations, reproducibility, failure cases.
- `docs/BOARD_REPRO.md` — ordered per-milestone board reproduction checklist.
- `tests/run_host_gates.sh` — one-command host gate runner (no board/Vivado).
- `docs/ehw0_4_results.md` — evolution-vs-gradient-training table for EHW-0.
- `docs/ehw1_0_results.md` — host-only CGP 2-bit multiplier result.
- `docs/ehw1_1_fabric_results.md` — host + board result for the fabric CGP VRC substrate.
- `docs/ehw1_2_results.md` — host + board result for ICAP-baking the evolved CGP
  multiplier.
- `docs/ehw2_results.md` — host + board result for per-eval ICAPE2 LUT-INIT
  evolution.
- `docs/ehw3_plan.md` — optional next-step plan for a fixed-route spare-routing
  island that evolves LUT truth tables plus local spare-path selection, without
  mutating raw Xilinx routing bits.
- `docs/ehw3_0_results.md` — host-only EHW-3.0 spare-routing recovery result:
  no-fault `8/8`, injected `DISABLE_NODE(A1)` degradation, and repaired `8/8`.
- `docs/ehw3_1_results.md` — host-only EHW-3.1 Python/C bit-exact twin for the
  spare-routing island.
- `docs/ehw3_2_results.md` — EHW-3.2 host-gated fabric VRC result; board run
  result.
- `docs/ehw3_3_results.md` — EHW-3.3 host + board result for ICAP-baked
  spare-route repair.
- `docs/ehw3_4_results.md` — EHW-3.4 host + board result for per-eval
  internal-ICAPE2 spare-route evolution.
- `sim/oracle_evolve.py` — EHW-0.0 host GA oracle; writes per-generation CSV logs
  under `runs/` (gitignored).
- `sim/ehw0_4_compare.py` — reproducible EHW-0.4 comparison generator.
- `sim/oracle_cgp.py` — EHW-1.0 CGP/LUT-INIT oracle for a 2-bit multiplier.
- `rtl/cgp_vrc.v` — EHW-1.1-fabric register-configured CGP VRC core and XBUS wrapper.
- `rtl/dfx/tpu_rp_rm_cgp_vrc.v` — DFX RM wrapper exposing the CGP VRC in the existing
  `0xF0000000` NEORV32 peripheral window.
- `rtl/cgp_baked.v` — EHW-1.2 hardwired LUT4 CGP grid for ICAP INIT edits.
- `rtl/spare_route_baked.v` — EHW-3.3 hardwired spare-route island for ICAP INIT
  repair of LUT logic and safe local path-select LUTs.
- `rtl/ehw34_spare_route_target.v` / `rtl/neorv32_soc_icap_sr.vhd` — EHW-3.4
  internal-ICAPE2 spare-route substrate and no-PS-HWICAP NEORV32 SoC.
- `rtl/ehw2_lut_target.v` / `rtl/neorv32_soc_icap.vhd` / `rtl/xbus_icap.v` —
  EHW-2 stretch substrate for in-fabric ICAPE2 LUT-INIT edits.
- `sw/ehw/` — EHW host/firmware C twin code; currently `ehw_kernel.h` and
  `ga_eval.c` for EHW-0.1, plus `ehw_eval_mbox.c` for the EHW-0.2 VRC/mailbox
  bridge, `ehw_ga_mbox.c` for the EHW-0.3 board-resident GA bridge, and
  `cgp_kernel.h`/`cgp_eval.c` for the EHW-1.0 CGP twin, `cgp_ga_mbox.c` for the
  EHW-1.1-sw board-resident software-eval CGP GA, `cgp_vrc_mbox.c` for the
  EHW-1.1-fabric board-resident fabric-eval CGP GA, `cgp_baked_post.c` for the
  EHW-1.2 baked-CGP POST, `ehw2_icap_micro.c` for the EHW-2 per-eval ICAPE2
  stretch, `lutkcm_post.c` for the EHW-0.5 ICAP-bake POST, and
  `spare_route_baked_post.c` for the EHW-3.3 baked spare-route POST, and
  `ehw34_icap_spare_route.c` for the EHW-3.4 per-eval ICAPE2 spare-route loop.
- `host/ehw_watch.py` — U-Boot serial mailbox watcher for EHW `0xE*` status tags.
- `tests/compare_ehw0_twin.py` — builds the C twin and verifies Python/C
  bit-exact CSV curves plus the M7.5.3 golden bitmap guard.
- `tests/compare_cgp_twin.py` — builds the CGP C twin and verifies Python/C
  bit-exact CGP curves.
- `tests/compare_cgp_vrc.py` — builds/runs the CGP VRC RTL host gate and firmware
  host stub, plus optional Vivado OOC synth when Vivado is available.
- `tests/compare_cgp_baked.py` — builds/runs the baked-CGP RTL/firmware host gate
  and optional Vivado OOC synth check.
- `tests/compare_ehw2_micro.py` — verifies the EHW-2 Python oracle, C host stub,
  and framebank packer contract.
- `tests/compare_spare_route_twin.py` — verifies the EHW-3 spare-routing island
  Python oracle and portable-C twin are bit-exact for no-fault and post-fault
  recovery curves.
- `tests/compare_spare_route_vrc.py` — verifies the EHW-3.2 spare-routing fabric
  VRC RTL sim, firmware host stub, wrapper compile, Py/C oracle gate, and optional
  Vivado OOC synth.
- `tests/compare_spare_route_baked.py` — verifies the EHW-3.3 baked spare-route
  RTL sim, firmware host stub, wrapper compile, target INIT diff, and optional
  Vivado OOC synth.
- `tests/compare_ehw34_icap.py` — verifies the EHW-3.4 Python oracle, C firmware
  stub, RTL target, generalized framebank packer, and optional Vivado OOC synth.
- `scripts/ehw2-build-framebank-from-bits.py` — builds the EHW-2 multi-FAR
  candidate framebank from same-route `.bit` files and prjxray `.bits` outputs.
- `scripts/ehw34-framebank-pack.py` / `scripts/ehw34-build-framebank-from-bits.py`
  — generalized EHW-3.4 16-byte-genome framebank tooling.
- `vivado/icap_ehw2/build_ehw2_icap.tcl` — EHW-2 T2.3-style static build and
  same-route INIT bitstreams for frame extraction.
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
