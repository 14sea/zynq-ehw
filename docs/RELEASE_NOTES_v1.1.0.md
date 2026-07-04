# Release Notes - v1.1.0

`v1.1.0` extends the v1.0.0 EHW-0 -> EHW-3.4 ladder with the EHW-4/EHW-5
memetic-hybrid line and closes EHW-5 after the board-verified EHW-5.4a
same-boot ablation.

## What is new since v1.0.0

- **EHW-4:** GA x HW-SGD memetic evolution over the 24-byte INT8 weight genome.
  The EHW-local train unit was reduced to fit the DFX pblock, smoke-tested on
  board, then used inside NEORV32 GA loops.
- **EHW-4.5:** same-boot Baldwinian vs Lamarckian A/B on board. Final mailbox
  `0xF7F02828`: both arms reached 40/40; Lamarckian converged faster, Baldwinian
  ended with lower SSE.
- **EHW-4.6a:** one-boot 12-point compile-time parameter sweep; all 24 point/mode
  rows matched host golden.
- **EHW-4.6b:** PS-writable parameter window for future sweeps. PS writes AXI
  `0x40000000`; NEORV32 reads XBUS `0xF5000000`; board probe demonstrated live
  update without reboot.
- **EHW-5.0/5.1:** host oracle and Python/C twin for the hybrid genome:
  16-byte safe spare-route structure + 24-byte INT8 seed weights.
- **EHW-5.2:** combined spare-route VRC + lite train-unit RM board-verified at
  FCLK0=50 MHz. Final mailbox `0xF5F00000`: mism=0, got_sse=gold_sse=4560,
  correct=38.
- **EHW-5.3:** first full hybrid structure+weight+HW-SGD Lamarckian-pressure arm
  on board. Final `0xF5F30000`; summary matched host golden: 40/40, SSE 4513,
  first_40 2, feature_ones 15, penalty 0.
- **EHW-5.4a:** same-image/same-boot four-arm ablation on board. Final
  `0xF5F40000`; all arms matched host golden.

## EHW-5.4a board evidence

Exact board words are in `docs/board_results.md`. The steady carousel observed
on 2026-07-05 was:

| Arm | Meaning | Board words |
|---:|---|---|
| 0 | weight-only Lamarckian | `f5400028`, `f55017e4`, `f5600003`, `f5700000` |
| 1 | hybrid-pressure `bias_x3` | `f5400128`, `f55111a1`, `f5600102`, `f5710f00` |
| 2 | hybrid no-adapt `gate_x3` | `f5400228`, `f5521207`, `f560020b`, `f5722700` |
| 3 | unpressured hybrid `bias_x3` | `f5400328`, `f55316cd`, `f5600305`, `f5730000` |

Final words: `f54f0004` (arm count 4), `f5f40000` (PASS).

Same-set deployment/adaptation readout:

- arm 1 vs arm 0: structure+pressure improves convergence and SSE
  (first_40 2 vs 3, SSE 4513 vs 6116);
- arm 1 vs arm 2: HW-SGD adaptation improves convergence speed
  (first_40 2 vs 11);
- arm 1 vs arm 3: pressure prevents the all-zero feature degeneration and gives
  the best SSE (feature_ones 15 vs 0, SSE 4513 vs 5837).

## Final EHW-5 claim

A fixed-route FPGA design can co-evolve safe local structure and INT8 weights,
use a board-verified HW-SGD inner loop for candidate adaptation, and show
same-boot ablation evidence that structure, adaptation, and pressure each affect
the same-set deployment metric.

## Non-claims

- This is not raw Xilinx routing-bit evolution. EHW-5 mutates safe local
  spare-route LUT/select fields and weight bytes, not arbitrary switch-matrix
  routing bits.
- This is not a holdout generalization result. The metrics are same-set
  deployment/adaptation metrics.
- This is not a claim that an ICAP reveal is required for EHW-5. EHW-5.5 remains
  optional presentation polish.
- This is not arbitrary large-scale EHW. The workloads are intentionally small
  and host-gated before every board claim.

## Reproducibility notes

- `tests/run_host_gates.sh` runs 19 host gates.
- From EHW-5.2 onward, board reproduction must run
  `scripts/board-set-fclk50.py` before `fpga loadb`; miner U-Boot defaults
  FCLK0 to 125 MHz while the DFX designs sign off 50 MHz.
- The v1.0.0-era board results were produced before this clock mismatch was
  diagnosed, at the miner default 125 MHz. They remain valid because their
  observed board outputs were bit-exact against their host goldens.
- Board flows still require Vivado, prjxray, the RISC-V toolchain, and the
  EBAZ4205 setup described in `README.md`, `docs/BOARD_REPRO.md`, and
  `docs/hw_notes.md`.

## Stop rule

EHW-5 is closed at EHW-5.4a. EHW-5.4b parameter-window scans and EHW-5.5 ICAP
reveal are optional future demos, not part of the v1.1.0 release scope.
