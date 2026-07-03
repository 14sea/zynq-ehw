# EHW-4.3: add rm_memetic_train (cfg10/impl_10) to the EXISTING project and build
# static (impl_1, memetic_train_mbox IMEM) + the full memetic-train bitstream.
#   vivado -mode batch -source m43_add_memetic.tcl
set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/../..]
open_project $origin/build/dfx.xpr
set rp_cell u_soc/wb_tpu_inst
if {[llength [get_reconfig_modules -quiet rm_memetic_train]] == 0} {
  create_reconfig_module -name rm_memetic_train -partition_def [get_partition_defs tpu_pd] -top tpu_rp
  add_files -norecurse -of_objects [get_reconfig_modules rm_memetic_train] \
      [list $root/rtl/dfx/tpu_rp_rm_memetic_train.v $root/rtl/memetic_train_unit.v \
            $root/rtl/wb_tpu_accel.v $root/rtl/tpu_accel.v \
            $root/rtl/systolic_array_4x4.v $root/rtl/pe.v]
  create_pr_configuration -name cfg10 -partitions [list $rp_cell:rm_memetic_train]
  create_run impl_10 -parent_run impl_1 -flow [get_property FLOW [get_runs impl_1]] -pr_config cfg10
}
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
open_run impl_10
report_utilization -pblocks [get_pblocks pblock_rp] -file $origin/build/impl10_rp_util.rpt
puts "=== RP util: $origin/build/impl10_rp_util.rpt ==="
exit
