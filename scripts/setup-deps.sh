#!/usr/bin/env bash
# Recreate the two gitignored upstream dependencies so the repo builds from a fresh
# clone (see README "Build / reproduce"). Idempotent — safe to re-run.
#   1. NEORV32 source  -> rtl_src/neorv32_tpu/neorv32   (+ picolibc-errno patch)
#   2. image_gen LMA fix applied to the cloned tree (sw/patches/image_gen_lma_fix/)
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)

# NEORV32 version this project was built against (rtl hw_version_c = 0x01120900).
# Override with NEORV32_REF=... if you need a different tag.
NEORV32_REF=${NEORV32_REF:-v1.12.9}
NEORV32_DIR=$root/rtl_src/neorv32_tpu/neorv32

if [ ! -d "$NEORV32_DIR/rtl/core" ]; then
  echo "[setup] cloning NEORV32 $NEORV32_REF -> $NEORV32_DIR"
  echo "[setup] (if it hangs on this box, IPv6 is broken — see reference_wsl_ipv6 notes)"
  mkdir -p "$(dirname "$NEORV32_DIR")"
  git clone --depth 1 --branch "$NEORV32_REF" https://github.com/stnolting/neorv32.git "$NEORV32_DIR"
else
  echo "[setup] NEORV32 already present: $NEORV32_DIR"
fi

# picolibc-errno guard (newlib-style errno clashes with picolibc's thread-local one)
nl=$NEORV32_DIR/sw/lib/source/neorv32_newlib.c
if [ -f "$nl" ] && ! grep -q '__PICOLIBC__' "$nl"; then
  echo "[setup] patching neorv32_newlib.c (picolibc-errno guard)"
  perl -0pi -e 's{#undef errno\nextern int errno;}{// zynq_xpart: picolibc declares errno thread-local; only redeclare for newlib.\n#ifndef __PICOLIBC__\n#undef errno\nextern int errno;\n#endif}' "$nl"
else
  echo "[setup] picolibc patch already applied (or file absent)"
fi

# image_gen LMA fix — stock v1.12.9 image_gen drops LMA alignment gaps (whole
# .rodata shifts -4 B in IMEM whenever .text % 8 == 4 under the picolibc ld
# script; silent constant corruption). See sw/patches/image_gen_lma_fix/README.md
# + https://github.com/stnolting/neorv32/issues/1593. Fixed upstream 2026-04-28
# by removing the ELF parser; for the pinned v1.12.9 we overwrite with the
# PT_LOAD-based drop-in. The stale image_gen BINARY is removed so common.mk
# rebuilds from the fixed source.
ig=$NEORV32_DIR/sw/image_gen/image_gen.c
if ! grep -q 'PT_LOAD' "$ig"; then
  echo "[setup] applying image_gen LMA fix (PT_LOAD load-image construction)"
  cp "$root/sw/patches/image_gen_lma_fix/image_gen.c" "$ig"
  rm -f "$NEORV32_DIR/sw/image_gen/image_gen"
else
  echo "[setup] image_gen LMA fix already applied"
fi

cat <<EOF

[setup] done. Next (EHW firmware builds in an ISOLATED dir — see sw/ehw/Makefile header):
  mkdir -p $root/sw_src/ehw_build
  cp $root/sw/ehw/{ehw_ga_mbox.c,*.h,Makefile} $root/sw_src/ehw_build/
  cd $root/sw_src/ehw_build && make NEORV32_HOME=$NEORV32_DIR \\
      RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" clean install verify-image
  # build the DFX bitstreams
  cd $root/vivado/dfx && vivado -mode batch -source build_dfx.tcl
EOF
