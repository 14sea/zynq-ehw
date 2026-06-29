# EHW firmware -> NEORV32 IMEM image. Build in an ISOLATED dir (only the chosen
# APP_SRC + ehw_kernel.h) so NEORV32 common.mk's *.c glob does not pull a second
# main() from the other ehw_*.c files. Example:
#   mkdir -p ../../sw_src/ehw_build && cp ehw_ga_mbox.c ehw_kernel.h Makefile ../../sw_src/ehw_build/
#   cd ../../sw_src/ehw_build && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
#       RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" clean install
APP_SRC ?= ehw_ga_mbox.c
MARCH = rv32imc_zicsr_zifencei
RISCV_PREFIX ?= riscv64-unknown-elf-
EFFORT = -Os
# IMEM/DMEM must match neorv32_soc_dfx generics (32k / 16k)
USER_FLAGS += -Wl,--defsym,__neorv32_rom_size=32k
USER_FLAGS += -Wl,--defsym,__neorv32_ram_size=16k
NEORV32_HOME ?= ../../rtl_src/neorv32_tpu/neorv32
include $(NEORV32_HOME)/sw/common/common.mk
