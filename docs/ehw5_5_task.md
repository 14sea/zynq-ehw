# EHW-5.5 Task - Optional ICAP Reveal

Status: **BOARD-VERIFIED on EBAZ4205 (2026-07-05).** Baseline truth 0xe8 /
feature 0xfbc5dabfc7 (28 ones) -> live ICAP edit of exactly g0/g1/g7/g8/g12/g14
-> champion truth 0xa0 / feature 0xd2c1d02a42 (15 ones), marker "SR55"
unchanged, no PS/NEORV32 reset. Exact words in `docs/board_results.md`.

EHW-5.5 is presentation polish after the main EHW-5 line closed at EHW-5.4a. It
should not change the claim: EHW-5 already demonstrated structure+weight
co-evolution, HW-SGD adaptation, pressure selection, and same-boot ablation on
the board. EHW-5.5 is only a reveal step: bake the chosen structural feature
phenotype into fabric INITs and show that an ICAP edit can make the static island
carry the EHW-5 champion feature.

## Frozen Champion

Use the EHW-5.4a arm1 champion:

```text
mode      hybrid_lamarckian_pressure
coupling  bias_x3
sr        08 00 0a 00 e8 00 00 03 03 02 02 03 01 00 03 02
feature   mask d2c1d02a42, ones 15, penalty 0
score     40/40, SSE 4513, first_40 2
```

The 8-row spare-route truth mask of this feature island is `0xa0`. That is not
the EHW-3 majority target; it is a dataset-useful feature selected by the
EHW-5 pressure arm. Do not replace it with the EHW-3 repaired majority genome.

## Host Contract

Run:

```bash
python3 tests/compare_ehw55_reveal_contract.py
```

Expected:

```text
PASS: truth_mask=0xa0 feature_mask=0xd2c1d02a42 ones=15
PASS: EHW-5.5 baseline->champion INIT diff cells: g0 g1 g7 g8 g12 g14
```

The host contract freezes the edit from `MS_SR_MAJORITY` to the EHW-5 champion:

```text
g0   4'ha -> 4'h8
g1   4'ha -> 4'h0
g7   64'hcccccccccccccccc -> 64'h0000000000000000
g8   64'hcccccccccccccccc -> 64'h0000000000000000
g12  64'h0000000000000000 -> 64'hcccccccccccccccc
g14  16'hcccc -> 16'hff00
```

No other primitive INIT should change for a structural-only reveal.

## Board Design Requirements

EHW-3.3's baked spare-route island models `DISABLE_NODE(A1)` in hardware. That is
correct for EHW-3.3 repair, but it is the wrong substrate for EHW-5.5. EHW-5.5
needs a no-fault baked target matching the EHW-5 feature path:

- same frozen 16-byte spare-route genome contract;
- no forced A1 disable;
- feature output reachable at the EHW-5 structural path, or a dedicated POST
  firmware that reports the 8-row mask and 40-sample feature mask;
- fresh routed baseline and same-route champion bitstreams;
- frame extraction from those exact bitstreams; never reuse old framebanks.

## Board Acceptance

Claude-side acceptance should record:

- `scripts/board-set-fclk50.py` readback `0x00200a00` before load;
- baseline no-fault baked target reports the baseline feature;
- ICAP edit applies only the g0/g1/g7/g8/g12/g14 INIT frames;
- live target reports champion truth mask `0xa0`;
- the EHW-5 40-sample feature mask is `0xd2c1d02a42` with 15 ones;
- no PS/NEORV32 reset between baseline observation and post-ICAP observation.

Until those board words are captured in `docs/board_results.md`, EHW-5.5 remains
host-prep only.
