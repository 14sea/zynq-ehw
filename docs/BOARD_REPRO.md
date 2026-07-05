# Board reproduction checklist (EBAZ4205)

Exact, ordered steps to reproduce each hardware-verified milestone on the board.
Full observed mailbox words + diagnoses are in `docs/board_results.md`; board facts
(addresses, gotchas) are in `docs/hw_notes.md`. Board tooling (openocd `ebaz4205.cfg`,
`uboot-intercept.py`, `/dev/ebaz-uart`, `uboot-fpga-load.py`) lives in `/home/test/xilinx`
and `/home/test/zynq_xpart/scripts` — this repo is built/edited; the board host scripts
are reused read-only.

## 0. Common setup (every session)

```sh
# devices: HS3 (FT232H, JTAG, usbipd busid 4-2) + CH340 (UART, busid 4-3, /dev/ebaz-uart)
usbipd.exe attach --wsl --busid 4-3 --auto-attach &     # survive reset brownout
# build firmware -> NEORV32 IMEM (BEFORE the Vivado build that bakes it!)
cd sw_src/<build> && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
    RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" APP_SRC=<fw>.c clean install
# reach a clean U-Boot from any board state (SRST not wired -> SLCR reset)
cd /home/test/xilinx
.env/bin/python scripts/uboot-intercept.py --duration 35 &   # hammers 'd'
openocd -f ebaz4205.cfg -c init -c halt \
    -c "mww phys 0xF8000008 0xDF0D" -c "mww phys 0xF8000200 1" -c shutdown
# after intercept: send a bare \r to flush the residual 'd' before the next command
# miner U-Boot leaves FCLK0 at 125 MHz; all DFX builds sign off 50 MHz
python3 /home/test/zynq_ehw/scripts/board-set-fclk50.py --port /dev/ebaz-uart
```

**Hard rules (learned on silicon — see board_results gotchas):**
- Build the firmware into IMEM **before** the Vivado build (else the bitstream runs stale firmware).
- Set and verify **FCLK0=50 MHz before every `fpga loadb`**. A NAND boot or
  power-cycle restores the miner default 125 MHz, which can produce deterministic
  placement-dependent wrong answers even when Vivado timing reports are clean.
  Historical note: the v1.0.0-era board milestones were produced before this
  mismatch was diagnosed, at the miner default 125 MHz, and remain valid because
  their observed board outputs were bit-exact against their host goldens.
- The target LUT/tile **moves every build** (P&R shifts with firmware size) → always re-extract frames from the freshly built bitstreams; never reuse an old framebank/seqs.
- **Never run a multi-frame ICAP bake in the foreground** — a tool timeout kills it mid-`writeseq` and corrupts the ICAP FSM. Always background it.
- Verify `readreg 12 == 0x13722093` and `PCAP_PR==0` before any PS-HWICAP ICAP write. **But an internal-ICAPE2 build (EHW-2) has NO PS HWICAP — do not poke it, it wedges PL-AXI.**
- DEVCFG wedge → physical Type-C power-cycle is the only recovery (SLCR reset's openocd halt fails when the PL-AXI/DAP is wedged).

## EHW-0.3 — board-resident weight GA (NN classifier 40/40)

```sh
cd vivado/dfx && vivado -mode batch -source build_dfx.tcl    # impl_1 = static + rm1_tpu
# reach U-Boot, then:
python3 /home/test/zynq_xpart/scripts/uboot-fpga-load.py --bit build/dfx.runs/impl_1/dfx_top.bit --op loadb
python3 host/ehw_watch.py --count 45     # read mailbox 0x41200000
```
PASS: `0xe9XX4028` rows=40/40, champion `0xD0..0xD7` == `sim/oracle_evolve.py` champion.

## EHW-0.5 — ICAP-bake evolved weights into LUT-KCM

```sh
cd vivado/dfx && vivado -mode batch -source build_lutkcm.tcl   # impl_7 = static + rm_lutkcm
m753_edit_tile.tcl -tclargs champ <16 weights>                 # edit routed dcp -> champion partial
# bitread baseline vs champion partials -> set/clr bits -> m75-build-frameseqs.py -> N envelopes
# board: loadb impl_7 -> 0x1019391F -> PCAP_PR=0 -> readreg 0x13722093 -> writeseq each frame (BACKGROUND)
```
PASS: mailbox `0x1019391F → 0x80AF7FF2` (VPU-model golden of the champion tile). Restore `PCAP_PR=1`.

## EHW-1.1-sw / -fabric — board CGP GA (2-bit multiplier 16/16)

```sh
# -sw  : reuse rm1_tpu build, IMEM = cgp_ga_mbox.c   (software LUT eval)
# -fab : vivado -source build_cgp_vrc.tcl (impl_10 = static + rm_cgp_vrc), IMEM = cgp_vrc_mbox.c
# loadb -> poll mailbox 0x41200008/0x41200000 (fabric) or 0x41200000 (sw)
```
PASS: `0xdc000010` DONE 16/16, champion `aaaa cccc f0f0 ff00 aaaa cccc f0f0 ff00 a0a0 6ac0 4c00 8000`.

## EHW-1.2 — ICAP-rewrite the evolved multiplier's LUTs (7/16 → 16/16)

```sh
cd vivado/dfx && vivado -source build_cgp_baked.tcl            # impl_11 = static + rm_cgp_baked_base
vivado -source cgp_baked_edit_champ.tcl                        # edit n8..n11 INITs in routed dcp
# bitread base vs champ -> m75-build-frameseqs.py (monotonic-start fix) -> frames
# loadb base -> 0xe3000007 (7/16) -> PCAP_PR=0 -> readreg -> writeseq frames (BACKGROUND)
```
PASS: mailbox `0xe3000007 → 0xe3000010` (rows 7→16). Restore `PCAP_PR=1`.

## EHW-2 — per-eval on-chip ICAPE2 LUT-INIT evolution

```sh
# IMEM = ehw2_icap_micro.c, then:
cd vivado/icap_ehw2 && vivado -mode batch -source build_ehw2_icap.tcl   # + ehw2_init_{00,80,a8,e8}.bit
# bitread the 4 same-route bitstreams, then build the MULTI-FAR framebank:
python3 scripts/ehw2-build-framebank-from-bits.py --out-dir <d> \
    --bit-template 'vivado/icap_ehw2/build/ehw2_init_{init}.bit' \
    --bits-template '<d>/init_{init}.bits'  00 80 a8 e8
# board: loadb ehw2_init_00.bit -> firmware 0xe8000000 (mailbox @ 0x41200008, GPIO ch2!)
#        PCAP_PR=0 (NO readreg — no PS HWICAP here) -> stage framebank (magic word0 last):
python3 scripts/ehw2-framebank-load.py <d>/framebank.bin 0x40000000
```
PASS: mailbox `0x41200008` → steady `0xeb0308e8` (candidate e8, fitness 8/8, mask 0xe8). Restore `PCAP_PR=1`.
The candidate INIT spans **two** config FARs → each candidate needs **two** envelopes (the multi-FAR framebank); a single envelope leaves the 2nd frame as a non-committed FDRI pad (truncated phenotype → wrong result).

## EHW-3.2 — spare-routing fabric VRC island

```sh
cp sw/ehw/Makefile sw/ehw/spare_route_kernel.h sw/ehw/spare_route_vrc_mbox.c sw_src/sr_build/
cd sw_src/sr_build && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
    RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
    APP_SRC=spare_route_vrc_mbox.c clean install
cd ../../vivado/dfx && vivado -mode batch -source build_spare_route_vrc.tcl
# loadb build_sr/dfx.runs/impl_2/dfx_top.bit -> poll mailbox 0x41200000
```

PASS: steady mailbox sequence `0xe32100e8`, `0xe3220008`, `0xe32300c8`,
`0xe3240007`, `0xe32500e8`, `0xe3260008`, `0xe3270002`, `0xe3280001`.
This proves no-fault majority `8/8`, disabled-A1 degradation `7/8`, and repaired
spare-AS recovery `8/8`.

## EHW-3.3 — ICAP-baked spare-route repair

```sh
cp sw/ehw/Makefile sw/ehw/spare_route_kernel.h sw/ehw/spare_route_baked_post.c sw_src/sr_build/
cd sw_src/sr_build && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
    RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
    APP_SRC=spare_route_baked_post.c clean install
cd ../../vivado/dfx
vivado -mode batch -source build_spare_route_baked.tcl          # impl_33 = static + SRB0 baseline
vivado -mode batch -source spare_route_baked_edit_repair.tcl    # same-route repaired bitstream
# bitread baseline impl_33 vs spare_route_icap/dfx_top_spare_route_repair.bit
# -> m75-build-frameseqs.py -> one envelope per changed FAR
# loadb baseline -> expect SRB0/c8/7 -> ICAP frames in background -> expect SRB0/e8/8
```

Expected acceptance: marker remains `SRB0` after ICAP because the edit changes
only target LUT/select INITs; mailbox should move from `E33200c8`/`E3330007` to
`E33200e8`/`E3330008` without PS/NEORV32 reset.

PASS: observed live ICAP repair moved `SRB0` from `c8/7` to `e8/8` with no
PS/NEORV32 reset; exact board evidence is in `docs/board_results.md`.

## EHW-3.4 — per-eval internal-ICAPE2 spare-route evolution

```sh
cp sw/ehw/Makefile sw/ehw/spare_route_kernel.h sw/ehw/ehw34_icap_spare_route.c sw_src/sr_build/
cd sw_src/sr_build && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
    RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
    APP_SRC=ehw34_icap_spare_route.c clean install
cd ../../vivado/icap_ehw34
vivado -mode batch -source build_ehw34_icap.tcl       # writes ehw34_base/logic/route/repair.bit
# bitread the four bitstreams, then:
cd ../..
python3 scripts/ehw34-build-framebank-from-bits.py --out-dir runs/ehw34_seqs \
    --base-label base \
    --bit-template 'vivado/icap_ehw34/build/ehw34_{label}.bit' \
    --bits-template 'runs/ehw34_bits/{label}.bits' \
    --candidate base=0a08010f320104000202000401010200 \
    --candidate logic=0b090903b10104000202000401010200 \
    --candidate route=0a08010f320004040102000001020300 \
    --candidate repair=0b090903b10004040102000001020300
# loadb base design -> PCAP_PR=0 -> stage 64KB framebank word0 last:
python3 scripts/ehw2-framebank-load.py runs/ehw34_seqs/framebank.bin 0x40000000
```

Expected acceptance: candidate loop emits `E900..` rows and converges to
`0xEA0308E8`, then steady `0xEC0308E8`. Board PASS on 2026-07-02 observed steady
`0xEC0308E8` on AXI-GPIO channel 2 (`0x41200008`): best candidate index 3
(`repair`), fitness `8/8`, mask `0xe8`. This internal-ICAPE2 build has **no
PS-HWICAP**; do not run PS-HWICAP readreg/writeseq commands or it can wedge PL-AXI.
The board-pass four-candidate bank uses 5278 words and is padded to the 16384-word
EHW-3.4 framebuf.

## EHW-4 / EHW-5 — memetic and hybrid line (v1.1.0)

The v1.1.0 line reuses the DFX static/RM lineage under `vivado/dfx`. The same
session rules from section 0 apply: build IMEM first, reset to U-Boot, run
`scripts/board-set-fclk50.py`, then `fpga loadb`.

EHW-4 train-unit lineage:

```sh
# example: EHW-4.5 same-boot Baldwinian/Lamarckian
mkdir -p sw_src/ehw_build_m45
cp sw/ehw/Makefile sw/ehw/memetic_kernel.h sw/ehw/memetic_ab_train_mbox.c sw_src/ehw_build_m45/
cd sw_src/ehw_build_m45 && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
    RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
    APP_SRC=memetic_ab_train_mbox.c clean install verify-image
cd ../../vivado/dfx && vivado -mode batch -source m43_add_memetic.tcl
# loadb build/dfx.runs/impl_10/dfx_top.bit after FCLK0 preflight
```

Observed endpoints:

- EHW-4.3 train-unit smoke: `0xF4F00000`.
- EHW-4.4 Lamarckian GA + HW-SGD: `0xF4F00028`.
- EHW-4.5 same-boot Baldwinian/Lamarckian A/B: `0xF7F02828`.
- EHW-4.6a sweep: 48 result words covering 24 point/mode rows; see
  `docs/board_results.md`.
- EHW-4.6b parameter window: PS `0x40000000` -> NEORV32 `0xF5000000`, probe
  `0xFB123456 / 0xFCABCDEF`, live update observed.

EHW-5 combined spare-route VRC + train-unit lineage:

```sh
# example: EHW-5.4a same-boot hybrid ablation
mkdir -p sw_src/ehw_build_m54
cp sw/ehw/Makefile sw/ehw/ehw_kernel.h sw/ehw/memetic_kernel.h \
   sw/ehw/spare_route_kernel.h sw/ehw/memetic_struct_kernel.h \
   sw/ehw/memetic_struct_ab_mbox.c sw_src/ehw_build_m54/
cd sw_src/ehw_build_m54 && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
    RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
    APP_SRC=memetic_struct_ab_mbox.c clean install verify-image
cd ../../vivado/dfx && vivado -mode batch -source m52_add_struct.tcl
# loadb build/dfx.runs/impl_12/dfx_top.bit after FCLK0 preflight
```

Observed endpoints:

- EHW-5.2 combined RM smoke: `0xF5F00000` at FCLK0=50 MHz.
- EHW-5.3 full hybrid arm: `0xF5F30000`, with carousel
  `0xf5302028 / 0xf53111a1 / 0xf5320f00 / 0xf53f0002 / 0xf5f30000`.
- EHW-5.4a same-boot four-arm ablation: final `0xf5f40000`, arm rows
  `f5400028/f55017e4/f5600003/f5700000`,
  `f5400128/f55111a1/f5600102/f5710f00`,
  `f5400228/f5521207/f560020b/f5722700`,
  `f5400328/f55316cd/f5600305/f5730000`, plus `f54f0004`.
- EHW-5.4b parameter-window host prep: generate staged blocks with
  `scripts/ehw54-param-pack.py` and stage them through the board-verified 4.6b
  AXI window:

```sh
python3 scripts/ehw54-param-pack.py --preset pressure-short --generations 4 \
  --out runs/ehw54/param_pressure_short.bin
python3 scripts/ehw2-framebank-load.py runs/ehw54/param_pressure_short.bin 0x40000000
```

  The board acceptance target is source word `0xf54e0101` (staged+valid) plus
  arm rows matching the host golden for the staged block, without rebuilding or
  reloading the bitstream. No 5.4b board claim is recorded until that run is
  captured in `docs/board_results.md`.

Exact sampled words, build manifests, and interpretation are in
`docs/board_results.md`.
