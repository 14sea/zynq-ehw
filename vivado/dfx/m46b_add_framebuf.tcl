# EHW-4.6b: add the axil_framebuf param window to the EXISTING dfx project's ps BD
# (PS @0x40000000 <-> NEORV32 @0xF5000xxx), regenerate wrapper, rebuild static +
# rm_memetic_train full bitstream.
#   vivado -mode batch -source m46b_add_framebuf.tcl
set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/../..]
open_project $origin/build/dfx.xpr
if {[llength [get_files -quiet */axil_framebuf.vhd]] == 0} {
  add_files $root/rtl/axil_framebuf.vhd
}
update_compile_order -fileset sources_1
open_bd_design [get_files ps.bd]
if {[llength [get_bd_cells -quiet fb_0]] == 0} {
  set fb [create_bd_cell -type module -reference axil_framebuf fb_0]
  set_property CONFIG.NUM_MI {3} [get_bd_cells axi_ic_0]
  set clk [get_bd_pins ps7_0/FCLK_CLK0]
  connect_bd_net $clk [get_bd_pins axi_ic_0/M02_ACLK]
  connect_bd_net $clk [get_bd_pins fb_0/s_axi_aclk]
  connect_bd_net [get_bd_pins rst_0/interconnect_aresetn] [get_bd_pins axi_ic_0/M02_ARESETN]
  connect_bd_net [get_bd_pins rst_0/peripheral_aresetn]   [get_bd_pins fb_0/s_axi_aresetn]
  connect_bd_intf_net [get_bd_intf_pins axi_ic_0/M02_AXI] [get_bd_intf_pins fb_0/s_axi]
  create_bd_port -dir I -from 10 -to 0 fb_rd_addr
  create_bd_port -dir O -from 31 -to 0 fb_rd_data
  connect_bd_net [get_bd_ports fb_rd_addr] [get_bd_pins fb_0/rd_addr]
  connect_bd_net [get_bd_ports fb_rd_data] [get_bd_pins fb_0/rd_data]
  assign_bd_address
  catch {assign_bd_address -force -offset 0x40000000 -range 8K [get_bd_addr_segs fb_0/s_axi/reg0]}
}
puts "=== ADDRESS MAP ==="
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps7_0/Data]] {
  puts "  $seg -> [get_property OFFSET $seg] range [get_property RANGE $seg]"
}
validate_bd_design
save_bd_design
make_wrapper -files [get_files ps.bd] -top -import -force
update_compile_order -fileset sources_1
reset_run synth_1
reset_run impl_1
reset_run impl_10
launch_runs synth_1 -jobs 8
wait_on_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
puts "=== impl_1: [get_property STATUS [get_runs impl_1]] ==="
launch_runs impl_10 -to_step write_bitstream -jobs 8
wait_on_run impl_10
puts "=== impl_10 (cfg10 rm_memetic_train): [get_property STATUS [get_runs impl_10]] ==="
exit
