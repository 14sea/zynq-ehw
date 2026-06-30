# Reference Map for zynq_ehw

This project is standalone. Reference projects under `/home/test` are read-only
sources to copy from or compare against; do not edit them in place. Selected files
have been copied into `external/` as local snapshots, so EHW work can proceed
without depending on the other working trees.

## `/home/test/zynq_xpart`

Primary source for the hardware-verified EBAZ4205 stack.

- `sim/oracle_m753.py` — M7.5.3-lite 4-4-2 fixed-point classifier oracle. EHW-0
  reuses its task shape, quantization, biases, test vectors, and tile semantics.
- `sw/m7_train/m753_vectors.h` — generated C constants for the 40-sample 2x2
  MNIST 0/1 evaluation set, labels, trained biases, and trained LUT-KCM weight
  tiles.
- `sw/m7_train/m753_infer.c` — board-side folded two-pass LUT-KCM inference and
  mailbox protocol. This is the closest firmware template for EHW-0 board eval.
- `scripts/m753-demo.py` — host orchestration pattern for probe-wait handshakes,
  mailbox polling, and two-stage ICAP baking.
- `scripts/m75-build-frameseqs.py` and `scripts/hwicap-uart.py` — ICAP reveal
  path for baking champion genomes into LUT-KCM tile INIT bits.
- `rtl/systolic_array_4x4.v` — VRC backend for fast register-loaded evaluation.
- `rtl/dfx/lutkcm_array.v` / `rtl/dfx/lutkcm_pe.v` — ICAP-baked LUT backend for
  the final reveal.
- `vivado/dfx/build_dfx.tcl` and `vivado/dfx/pblock_rp.xdc` — DFX build flow to
  copy when the board flow is brought into this repo.

## `/home/test/zynq_agentctl`

Useful host-control reference, especially for long-lived UART/HWICAP processes.

- `host/agentctl.py` — command-surface structure for perceive/act/verify loops.
- `host/ro_adapt.py` — adaptive search loop with a stable serial connection and
  repeated measurements.
- `host/frameguard.py` and `host/measured-load.py` — host-side safety checks for
  frame writes and bitstream loading.
- `host/hwicap-make-framewrite.py` — smaller HWICAP frame sequence generator
  variant.
- `firmware/icaphw.c` — Linux `/dev/mem` HWICAP executor reference. EHW should
  prefer the already-proven zynq_xpart NEORV32/ICAPE2 path for self-reconfig, but
  this is useful for host-driven bring-up.

## Current EHW-0 Implementation

- `sim/oracle_evolve.py` is the first executable rung, EHW-0.0. It is self-contained
  and embeds the M7.5.3-lite constants from `m753_vectors.h`.
- The genome is exactly 24 signed bytes: `W1[4][4]` plus `W2[2][4]`.
- The same genome maps to the VRC register-loaded array and to two ICAP-baked
  LUT-KCM tiles, matching the VRC-first/ICAP-reveal plan.
- `sw/ehw/ehw_kernel.h` and `sw/ehw/ga_eval.c` are the EHW-0.1 C twin. They use
  the same constants and a small xorshift32 RNG so the host Python oracle and C
  path can be compared byte-for-byte before any board work.
- `tests/compare_ehw0_twin.py` also checks that `M753_TRAINED_GENOME` reproduces
  the M7.5.3 golden classification bitmap. This catches fixed-point drift without
  requiring the trained reference to score 40/40 against labels; the upstream
  M7.5.3 golden bitmap is 37/40 label-accurate.
- `sw/ehw/ehw_eval_mbox.c` is the EHW-0.2 bridge firmware: it runs the same
  champion genome through the register-loaded VRC array and publishes mailbox
  tags. It deliberately avoids assuming NEORV32 UART input, because the copied M7
  references document that `uart0` is not pinned out in the current DFX top.

## Local Snapshot Policy

- Reference snapshots live under `external/`.
- Generated artifacts are intentionally not copied: ELF files, build directories,
  bitstreams, frame sequences, Vivado logs/journals, `.git`, and `__pycache__`.
- If a copied file needs to become project-owned code, move or copy it out of
  `external/` into the normal project tree and document the provenance in the file
  header or commit message.

## Research Code Snapshots

- `external/research/cobea/` is a shallow-cloned snapshot of
  `https://github.com/nmi-leipzig/cobea`. It is useful for architecture study:
  `domain/interfaces.py` separates representation, target device, driver, meter,
  fitness, and population init; `domain/use_cases.py` composes decode/configure,
  measure, and fitness into a reusable evaluation pipeline.
- CoBEA had no obvious license file in the snapshot. Use it as a design reference,
  not copied implementation, until licensing is clarified.
- Whitley/evolvablehardware.org is still a web/tutorial reference here; no concrete
  git URL has been added yet.
