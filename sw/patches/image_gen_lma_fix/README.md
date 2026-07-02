# image_gen LMA fix (applied automatically by scripts/setup-deps.sh)

The stock NEORV32 v1.12.9 `sw/image_gen/image_gen.c` builds the IMEM image by
concatenating `.text`+`.rodata`+`.data`, dropping LMA alignment gaps. With the
picolibc linker script (`.rodata` ALIGN(8)), any firmware whose `.text % 8 == 4`
gets its whole `.rodata` shifted −4 bytes in IMEM — constants read wrong at
runtime while code executes normally. Upstream removed the ELF parser entirely
on 2026-04-28 (objcopy-flat-binary flow); v1.12.9 and earlier remain affected.

This directory holds a fixed drop-in that builds the image from the ELF
**PT_LOAD program headers** (each segment at `p_paddr − base` in a zero-filled
buffer, `p_filesz` bytes only). Regression: `-t bin` output byte-identical to
`riscv64-unknown-elf-objcopy -O binary` across 9+ firmwares, incl. the 4-byte
gap class and `.data ≠ 0`.

Full forensic write-up (this bug masqueraded as a "7-series DFX routing
limitation" for ten days in the sibling project):
https://github.com/14sea/zynq-xpart/blob/main/docs/m7_2_dcpdiff.md
Upstream heads-up: https://github.com/stnolting/neorv32/issues/1593

`setup-deps.sh` copies it over the cloned NEORV32 tree; common.mk rebuilds the
image_gen binary automatically on the next make. Tripwire for any doubt:
`make verify-image` in sw/ehw/ (byte-compares objcopy vs image_gen output).
