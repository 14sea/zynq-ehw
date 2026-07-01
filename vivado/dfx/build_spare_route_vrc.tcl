# EHW-3.2 DFX build: static (rm1_tpu, cfg1) + spare-routing VRC partial
# (rm_spare_route_vrc). Same PS-only BD + dfx_top / neorv32_soc_dfx static as
# build_cgp_vrc.tcl (the shared, HW-verified static SoC); the only new partition
# module is the spare-routing island behind the 0xF0000000 XBUS window.
#
# The NEORV32 IMEM must already hold the EHW-3.2 firmware image
# (rtl_src/.../rtl/core/neorv32_imem_image.vhd) BEFORE this runs — build it first:
#   cd sw_src/sr_build && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
#     RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
#     APP_SRC=spare_route_vrc_mbox.c clean install
#
#   vivado -mode batch -source build_spare_route_vrc.tcl
# Output: build/dfx.runs/impl_2/dfx_top.bit (static + rm_spare_route_vrc full bitstream)

set proj   dfx
set part   xc7z010clg400-1
set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/../..]
set nhome  $root/rtl_src/neorv32_tpu/neorv32
set bdir   $origin/build_sr

create_project $proj $bdir -part $part -force
set_property PR_FLOW 1 [current_project]

# --- static + RM1 RTL sources (shared static SoC) ---
set fl [read [open $nhome/rtl/file_list_soc.f r]]
set fl [string map [list NEORV32_RTL_PATH_PLACEHOLDER $nhome/rtl] $fl]
add_files $fl
set_property library neorv32 [get_files $fl]
add_files $root/rtl/neorv32_soc_dfx.vhd
add_files $root/rtl/dfx_top.v
add_files [list $root/rtl/dfx/tpu_rp_rm1_tpu.v $root/rtl/pe.v \
                $root/rtl/systolic_array_4x4.v $root/rtl/tpu_accel.v $root/rtl/wb_tpu_accel.v]

# --- PS-only block design (PS7 + AXI-GPIO mailbox + AXI HWICAP), verbatim from
#     build_cgp_vrc.tcl so the static matches the shared HW-verified SoC ---
create_bd_design "ps"
set ps7 [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7 ps7_0]
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 {1} CONFIG.PCW_EN_CLK0_PORT {1} CONFIG.PCW_FCLK_CLK0_BUF {TRUE}] $ps7
set gpio [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio axi_gpio_0]
set_property -dict [list CONFIG.C_GPIO_WIDTH {32} CONFIG.C_ALL_INPUTS {1} \
  CONFIG.C_IS_DUAL {1} CONFIG.C_GPIO2_WIDTH {1} CONFIG.C_ALL_INPUTS_2 {1}] $gpio
set hwi [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_hwicap axi_hwicap_0]
set ic [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect axi_ic_0]
set_property CONFIG.NUM_MI {2} $ic
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset rst_0
create_bd_port -dir O fclk_o
create_bd_port -dir O rstn_o
create_bd_port -dir I -from 31 -to 0 mbox_i
create_bd_port -dir I mbox_valid_i
set clk [get_bd_pins ps7_0/FCLK_CLK0]
foreach p {ps7_0/M_AXI_GP0_ACLK axi_ic_0/ACLK axi_ic_0/S00_ACLK axi_ic_0/M00_ACLK \
           axi_ic_0/M01_ACLK axi_gpio_0/s_axi_aclk \
           axi_hwicap_0/s_axi_aclk axi_hwicap_0/icap_clk rst_0/slowest_sync_clk} {
  connect_bd_net $clk [get_bd_pins $p]
}
connect_bd_net $clk [get_bd_ports fclk_o]
connect_bd_net [get_bd_pins ps7_0/FCLK_RESET0_N] [get_bd_pins rst_0/ext_reset_in]
foreach p {axi_ic_0/ARESETN axi_ic_0/S00_ARESETN axi_ic_0/M00_ARESETN axi_ic_0/M01_ARESETN} {
  connect_bd_net [get_bd_pins rst_0/interconnect_aresetn] [get_bd_pins $p]
}
connect_bd_net [get_bd_pins rst_0/peripheral_aresetn] [get_bd_pins axi_gpio_0/s_axi_aresetn]
connect_bd_net [get_bd_pins rst_0/peripheral_aresetn] [get_bd_pins axi_hwicap_0/s_axi_aresetn]
connect_bd_net [get_bd_pins rst_0/peripheral_aresetn] [get_bd_ports rstn_o]
connect_bd_intf_net [get_bd_intf_pins ps7_0/M_AXI_GP0]  [get_bd_intf_pins axi_ic_0/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ic_0/M00_AXI] [get_bd_intf_pins axi_gpio_0/S_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ic_0/M01_AXI] [get_bd_intf_pins axi_hwicap_0/S_AXI_LITE]
connect_bd_net [get_bd_ports mbox_i]       [get_bd_pins axi_gpio_0/gpio_io_i]
connect_bd_net [get_bd_ports mbox_valid_i] [get_bd_pins axi_gpio_0/gpio2_io_i]
assign_bd_address
catch {assign_bd_address -force -offset 0x41200000 -range 64K [get_bd_addr_segs axi_gpio_0/S_AXI/Reg]}
catch {assign_bd_address -force -offset 0x41400000 -range 64K [get_bd_addr_segs axi_hwicap_0/S_AXI_LITE/Reg]}
validate_bd_design
save_bd_design
make_wrapper -files [get_files ps.bd] -top -import

# --- top = RTL dfx_top ---
set_property top dfx_top [current_fileset]
update_compile_order -fileset sources_1

# --- DFX: partition def + reconfig modules + configurations ---
set rp_cell u_soc/wb_tpu_inst
create_partition_def -name tpu_pd -module tpu_rp
create_reconfig_module -name rm1_tpu -partition_def [get_partition_defs tpu_pd] -define_from tpu_rp
# EHW-3.2: spare-routing VRC island reconfigurable module (0xF0000000 XBUS window)
create_reconfig_module -name rm_spare_route_vrc -partition_def [get_partition_defs tpu_pd] -top tpu_rp
add_files -norecurse -of_objects [get_reconfig_modules rm_spare_route_vrc] \
    [list $root/rtl/dfx/tpu_rp_rm_spare_route_vrc.v $root/rtl/spare_route_vrc.v]
create_pr_configuration -name cfg1 -partitions [list $rp_cell:rm1_tpu]
create_pr_configuration -name cfg_sr -partitions [list $rp_cell:rm_spare_route_vrc]
add_files -fileset constrs_1 -norecurse $origin/pblock_rp.xdc

set_property PR_CONFIGURATION cfg1 [get_runs impl_1]
create_run impl_2 -parent_run impl_1 -flow [get_property FLOW [get_runs impl_1]] -pr_config cfg_sr

launch_runs synth_1 -jobs 8
wait_on_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
puts "=== impl_1 (cfg1 static+rm1_tpu): [get_property STATUS [get_runs impl_1]] ==="

launch_runs impl_2 -to_step write_bitstream -jobs 8
wait_on_run impl_2
puts "=== impl_2 (cfg_sr static+rm_spare_route_vrc): [get_property STATUS [get_runs impl_2]] ==="
open_run impl_2
report_utilization -file $bdir/impl2_util.rpt
report_drc        -file $bdir/impl2_drc.rpt
puts "=== impl_2 reports: $bdir/impl2_util.rpt , $bdir/impl2_drc.rpt ==="
foreach b [glob -nocomplain $bdir/$proj.runs/impl_2/*.bit] { puts "  BIT: $b" }
exit
