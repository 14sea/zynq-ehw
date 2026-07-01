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
```

**Hard rules (learned on silicon — see board_results gotchas):**
- Build the firmware into IMEM **before** the Vivado build (else the bitstream runs stale firmware).
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

## EHW-3.3 — ICAP-baked spare-route repair (pending board run)

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
