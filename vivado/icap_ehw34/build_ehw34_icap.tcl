# EHW-3.4 ICAPE2 build: PS7 + neorv32_soc_icap_sr (NEORV32 ->
# xbus_icap -> ICAPE2 + AXI-Lite framebank) + baked spare-route target.
#
# This is the EHW-3 spare-routing analogue of vivado/icap_ehw2/build_ehw2_icap.tcl.
# It has NO PS-HWICAP. The PS only stages the framebank through AXI-Lite and reads
# AXI-GPIO observers; all per-eval reconfiguration goes through fabric ICAPE2.
#
# Build firmware into IMEM first:
#   cp sw/ehw/Makefile sw/ehw/spare_route_kernel.h sw/ehw/ehw34_icap_spare_route.c sw_src/sr_build/
#   cd sw_src/sr_build && make NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
#     RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" \
#     APP_SRC=ehw34_icap_spare_route.c clean install
#
# Then:
#   vivado -mode batch -source vivado/icap_ehw34/build_ehw34_icap.tcl

set proj   ehw34_icap
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
add_files [list $root/rtl/xbus_icap.v $root/rtl/spare_route_baked.v $root/rtl/ehw34_spare_route_target.v]
add_files [list $root/rtl/axil_framebuf.vhd $root/rtl/neorv32_soc_icap_sr.vhd]
update_compile_order -fileset sources_1

create_bd_design "system"
set ps7 [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7 ps7_0]
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 {1} CONFIG.PCW_EN_CLK0_PORT {1} CONFIG.PCW_FCLK_CLK0_BUF {TRUE}] $ps7
create_bd_cell -type module -reference neorv32_soc_icap_sr soc_0

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
catch {assign_bd_address -force -offset 0x40000000 -range 64K [get_bd_addr_segs soc_0/S_AXI/Reg]}
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

proc pool_mux_init {sel} {
  switch -- $sel {
    0 { return "64'hAAAAAAAAAAAAAAAA" }
    1 { return "64'hCCCCCCCCCCCCCCCC" }
    2 { return "64'hF0F0F0F0F0F0F0F0" }
    4 { return "64'hFFFF0000FFFF0000" }
    default { return "64'h0000000000000000" }
  }
}

proc out_mux_init {sel} {
  switch -- $sel {
    0 { return "16'hAAAA" }
    1 { return "16'hCCCC" }
    2 { return "16'hF0F0" }
    3 { return "16'hFF00" }
    default { return "16'hAAAA" }
  }
}

proc find_target_cell {name} {
  set pat "*u_sr_target/u_baked/$name"
  set cells [get_cells -hier -filter "NAME =~ $pat"]
  if {[llength $cells] != 1} {
    error "expected exactly one cell for $pat, got [llength $cells]: $cells"
  }
  return [lindex $cells 0]
}

proc apply_genome {bytes} {
  set names {g0 g1 g2 g3 g4 g5 g6 g7 g8 g9 g10 g11 g12 g13 g14 g15}
  for {set i 0} {$i < 16} {incr i} {
    set b [lindex $bytes $i]
    scan $b %x v
    set name [lindex $names $i]
    set cell [find_target_cell $name]
    if {$i < 4} {
      set init [format "4'h%X" [expr {$v & 15}]]
    } elseif {$i == 4} {
      set init [format "8'h%02X" [expr {$v & 255}]]
    } elseif {$i < 13} {
      set init [pool_mux_init $v]
    } else {
      set init [out_mux_init $v]
    }
    set_property INIT $init $cell
    puts "set $cell INIT -> [get_property INIT $cell]"
  }
}

array set genomes {
  base   {0a 08 01 0f 32 01 04 00 02 02 00 04 01 01 02 00}
  logic  {0b 09 09 03 b1 01 04 00 02 02 00 04 01 01 02 00}
  route  {0a 08 01 0f 32 00 04 04 01 02 00 00 01 02 03 00}
  repair {0b 09 09 03 b1 00 04 04 01 02 00 00 01 02 03 00}
}

foreach label {base logic route repair} {
  apply_genome $genomes($label)
  write_bitstream -force $bdir/ehw34_$label.bit
  puts "=== wrote $bdir/ehw34_$label.bit ==="
}

exit
