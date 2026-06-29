# Board Results

Record only observations from commands actually run on the EBAZ4205. Keep exact
mailbox words and commands so codegen can reproduce the host model comparison.

## EHW-0.2 VRC Compiled-Champion Eval

Firmware: `sw/ehw/ehw_eval_mbox.c`

Host model:

- array self-check low16: `14`
- correct: `40/40`
- SSE: `4799`
- fitness: `39995201`

Board commands / observations:

```text
# build firmware into NEORV32 image:
# make APP_SRC=ehw_eval_mbox.c ...

# load full bitstream:
# fpga loadb ...

# observe mailbox:
# md 0x41200000 1
```

Expected mailbox sequence:

- `0xE0000000`: reached main.
- `0xE100000E`: array self-check acc0 = 14.
- `0xE22812BF`: correct = 40, SSE low16 = `0x12BF` = 4799.
- `0xE3624741`: fitness low24 for `39995201`.

Result:

- status: pending board run
- exact observed words:
- notes:

## EHW-0.3 Board-Resident GA

Firmware: `sw/ehw/ehw_ga_mbox.c`

Host model:

- seed: `3`
- population: `32`
- generations cap: `64`
- expected curve:

```text
gen,best_correct,best_acc,best_sse,best_fitness,genome
0,38,0.9500,4787,37995213,3 -1 -3 -2 13 19 21 18 -7 -3 -1 0 4 0 2 -35 -2 27 3 0 14 -14 5 13
1,40,1.0000,4799,39995201,3 -1 -3 -2 13 13 21 18 -7 -3 -7 0 4 0 2 -35 -2 27 3 0 14 -14 5 13
```

Expected mailbox tags:

- `0xE8000000`: reached main.
- `0xE9000026`: generation 0, correct 38.
- `0xEA0012B3`: SSE 4787.
- `0xEB43C2CD`: fitness low24 for 37995213.
- `0xE9000128`: generation 1, correct 40.
- `0xEA0012BF`: SSE 4799.
- `0xEB624741`: fitness low24 for 39995201.
- `0xEC000028`: done, correct 40.
- `0xD0..0xD7`: 24-byte champion genome chunks.

Result:

- status: **PASS — HW-VERIFIED on EBAZ4205 (2026-06-29), first DFX roll**
- bitstream: `vivado/dfx/build/dfx.runs/impl_1/dfx_top.bit` (static + `rm1_tpu`),
  timing met (WNS +7.829 / WHS +0.042 / 0 DRC errors)
- exact observed words (`host/ehw_watch.py`):
  - `0xe9004028` — GA_GEN gen=64 correct=40/40
  - `0xea0012bf` — GA_SSE 4799
  - `0xeb624741` — GA_FITNESS low24 = `39995201 & 0xFFFFFF`
  - `0xD0..0xD7` champion chunks
- reconstructed champion genome:
  `3 -1 -3 -2 13 13 21 18 -7 -3 -7 0 4 0 2 -35 -2 27 3 0 14 -14 5 13`
- host cross-check: `sim/oracle_evolve.py --seed 3 --population 32 --generations 64`
  → champion **bit-identical**, correct 40/40, SSE 4799, fitness 39995201. PASS.
- notes:
  - The board reports `gen=64` (ran the full cap) while the host reaches 40/40 at
    `gen 1`; the final champion is identical. Only difference from ChatGPT's
    predicted tags is the gen counter in `0xE9` (predicted `0x0128` gen1, observed
    `0x4028` gen64) — both `correct=40`, expected.
  - The 4×4 VRC array (`tpu_accel`) computes correctly on silicon: SSE + genome
    match the host model exactly → the register-map driver in `ehw_ga_mbox.c` is
    hardware-verified. The 300000-count settle was sufficient for `rm1_tpu`.

---

## EHW-1.1 board-resident CGP GA (2-bit multiplier) — PASS (2026-06-29)

**On-chip evolution of a logic circuit on real silicon.** Board-resident CGP GA on
NEORV32 evolved a 2-bit unsigned multiplier (fixed-routing LUT grid, `cgp_kernel.h`)
to a perfect solution, champion bit-identical to the host oracle.

- Firmware: `sw/ehw/cgp_ga_mbox.c` (Claude-authored for board; GA helpers + loop
  COPIED VERBATIM from ChatGPT's `cgp_eval.c` for bit-exactness). `.text` 1780 B.
  Software LUT-grid eval (does NOT use the fabric array) — so this is on-chip
  *evolution of a logic circuit*, not yet a fabric-LUT substrate (that = `cgp_vrc.v`,
  a later rung).
- Bitstream: same static+`rm1_tpu` build, new IMEM (timing met WNS +7.574).
- **Pre-board host gate (PASS):** `cc -DCGP_HOST_STUB ... cgp_ga_mbox.c` champion ==
  `cgp_eval.c`/`oracle_cgp.py` champion, bit-exact.
- Observed mailbox (`0xCC`/`0xCA`/`0xB0..0xBB`):
  - `0xcc000010` — DONE rows = 16/16
  - `0xca000040` — fitness 64/64
  - champion genome (b0..bb): `aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0
    4c00 8000` — **bit-identical to host** (12/12 words), 12 active nodes.

### Gotcha caught on silicon (worth keeping)
- First board attempt: mailbox stuck at `0x00000000` — firmware wasn't writing. Root
  cause = I wrote the firmware-side `MBOX` to the **PS** address `0x41200000` instead
  of the **PL** mailbox input `0xF1000000` (PS *reads* 0x41200000; firmware *writes*
  0xF1000000 — `docs/hw_notes.md`). The **host gate did NOT catch this** (host stub
  uses `MBOX_STUB`), only the board did → board run is the final gate for
  board-specific addresses. Fixed → rebuilt → PASS.
