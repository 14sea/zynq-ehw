# EHW-5.2: add rm_memetic_struct (cfg11/impl_11) to the EXISTING project; rebuild
# static (memetic_struct_train_mbox IMEM) + the combined-RM full bitstream, then
# report place-level RP utilization (the authoritative resource verdict).
set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/../..]
open_project $origin/build/dfx.xpr
set rp_cell u_soc/wb_tpu_inst
if {[llength [get_reconfig_modules -quiet rm_memetic_struct]] == 0} {
  create_reconfig_module -name rm_memetic_struct -partition_def [get_partition_defs tpu_pd] -top tpu_rp
  add_files -norecurse -of_objects [get_reconfig_modules rm_memetic_struct] \
      [list $root/rtl/dfx/tpu_rp_rm_memetic_struct.v $root/rtl/memetic_train_unit_lite.v \
            $root/rtl/spare_route_vrc.v \
            $root/rtl/wb_tpu_accel.v $root/rtl/tpu_accel.v \
            $root/rtl/systolic_array_4x4.v $root/rtl/pe.v]
}
# cfg11/impl_11 belong to the cgp_baked lineage in the LIVE project -> use cfg12/impl_12
if {[llength [get_pr_configurations -quiet cfg12]] == 0} {
  create_pr_configuration -name cfg12 -partitions [list $rp_cell:rm_memetic_struct]
  create_run impl_12 -parent_run impl_1 -flow [get_property FLOW [get_runs impl_1]] -pr_config cfg12
}
reset_run synth_1
reset_run impl_1
reset_run impl_12
launch_runs synth_1 -jobs 8
wait_on_run synth_1
launch_runs impl_1 -to_step write_bitstream -jobs 8
wait_on_run impl_1
puts "=== impl_1: [get_property STATUS [get_runs impl_1]] ==="
launch_runs impl_12 -to_step write_bitstream -jobs 8
wait_on_run impl_12
puts "=== impl_12 (cfg12 rm_memetic_struct): [get_property STATUS [get_runs impl_12]] ==="
open_run impl_12
report_utilization -pblocks [get_pblocks pblock_rp] -file $origin/build/impl12_rp_util.rpt
puts "=== RP util: $origin/build/impl12_rp_util.rpt ==="
exit
