# EHW-2 ICAPE2 build: PS7 + neorv32_soc_icap (NEORV32 -> xbus_icap ->
# ICAPE2 + AXI-Lite framebank) + AXI-GPIO observers.
#
# This is the zynq_ehw-local version of the proven zynq_xpart T2.3 build. It
# routes one static design, then writes four bitstreams from that routed design by
# changing only the editable LUT's INIT value. Those bitstreams are diffed/extracted
# into frame sequences and packed into the EHW-2 framebank.
#
#   vivado -mode batch -source vivado/icap_ehw2/build_ehw2_icap.tcl

set proj   ehw2_icap
set part   xc7z010clg400-1
set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/../..]
set nhome  $root/rtl_src/neorv32_tpu/neorv32
set bdir   $origin/build

create_project $proj $bdir -part $part -force

set fl [read [open $nhome/rtl/file_list_soc.f r]]
set fl [string map [list NEORV32_RTL_PATH_PLACEHOLDER $nhome/rtl] $fl]
add_files $fl
set_property library neorv32 [get_files $fl]
add_files [glob $root/rtl/xbus_icap.v $root/rtl/ehw2_lut_target.v]
add_files [list $root/rtl/axil_framebuf.vhd $root/rtl/neorv32_soc_icap.vhd]
update_compile_order -fileset sources_1

create_bd_design "system"
set ps7 [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7 ps7_0]
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 {1} CONFIG.PCW_EN_CLK0_PORT {1} CONFIG.PCW_FCLK_CLK0_BUF {TRUE}] $ps7
create_bd_cell -type module -reference neorv32_soc_icap soc_0

set gpio [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio axi_gpio_0]
set_property -dict [list CONFIG.C_GPIO_WIDTH {32} CONFIG.C_ALL_INPUTS {1} \
  CONFIG.C_IS_DUAL {1} CONFIG.C_GPIO2_WIDTH {32} CONFIG.C_ALL_INPUTS_2 {1}] $gpio

set ic [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect axi_ic_0]
set_property CONFIG.NUM_MI {2} $ic
create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset rst_0
set c1 [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant const1]
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {1}] $c1

set clk [get_bd_pins ps7_0/FCLK_CLK0]
foreach p {ps7_0/M_AXI_GP0_ACLK axi_ic_0/ACLK axi_ic_0/S00_ACLK axi_ic_0/M00_ACLK \
           axi_ic_0/M01_ACLK axi_gpio_0/s_axi_aclk rst_0/slowest_sync_clk \
           soc_0/clk_i soc_0/s_axi_aclk} {
  connect_bd_net $clk [get_bd_pins $p]
}
connect_bd_net [get_bd_pins ps7_0/FCLK_RESET0_N] [get_bd_pins rst_0/ext_reset_in]
foreach p {axi_ic_0/ARESETN axi_ic_0/S00_ARESETN axi_ic_0/M00_ARESETN axi_ic_0/M01_ARESETN} {
  connect_bd_net [get_bd_pins rst_0/interconnect_aresetn] [get_bd_pins $p]
}
connect_bd_net [get_bd_pins rst_0/peripheral_aresetn] [get_bd_pins axi_gpio_0/s_axi_aresetn]
connect_bd_net [get_bd_pins rst_0/peripheral_aresetn] [get_bd_pins soc_0/rstn_i]
connect_bd_net [get_bd_pins rst_0/peripheral_aresetn] [get_bd_pins soc_0/s_axi_aresetn]

connect_bd_intf_net [get_bd_intf_pins ps7_0/M_AXI_GP0]  [get_bd_intf_pins axi_ic_0/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ic_0/M00_AXI] [get_bd_intf_pins axi_gpio_0/S_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_ic_0/M01_AXI] [get_bd_intf_pins soc_0/S_AXI]

connect_bd_net [get_bd_pins soc_0/lut_o]  [get_bd_pins axi_gpio_0/gpio_io_i]
connect_bd_net [get_bd_pins soc_0/mbox_o] [get_bd_pins axi_gpio_0/gpio2_io_i]
connect_bd_net [get_bd_pins const1/dout]  [get_bd_pins soc_0/uart0_rxd_i]

assign_bd_address
catch {assign_bd_address -force -offset 0x40000000 -range 4K [get_bd_addr_segs soc_0/S_AXI/Reg]}
validate_bd_design
save_bd_design
puts "=== ADDRESS MAP ==="
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps7_0/Data]] {
  puts "  $seg -> [get_property OFFSET $seg]  range [get_property RANGE $seg]"
}

make_wrapper -files [get_files system.bd] -top -import
set_property top system_wrapper [current_fileset]
update_compile_order -fileset sources_1

launch_runs impl_1 -to_step route_design -jobs 8
wait_on_run impl_1
puts "=== IMPL: [get_property STATUS [get_runs impl_1]] ==="
if {[string first "Complete" [get_property STATUS [get_runs impl_1]]] < 0} {
  error "implementation did not complete"
}

open_run impl_1
set_property BITSTREAM.GENERAL.CRC Disable [current_design]
set lut [get_cells -hier -filter {REF_NAME == LUT6 && NAME =~ *l_ehw2*}]
if {[llength $lut] != 1} {
  error "expected exactly one EHW-2 LUT cell matching *l_ehw2*, got: $lut"
}
puts "=== EHW2 LUT cell: $lut  LOC=[get_property LOC $lut] ==="

foreach init {00 80 a8 e8} {
  set init64 [format "64'h00000000000000%s" $init]
  set_property INIT $init64 $lut
  write_bitstream -force $bdir/ehw2_init_$init.bit
  puts "=== wrote $bdir/ehw2_init_$init.bit INIT=$init64 ==="
}

exit
