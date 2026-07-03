set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/..]
set outdir $root/runs/tests/vivado_ooc_memetic_struct
file mkdir $outdir

create_project memetic_struct_ooc $outdir -part xc7z010clg400-1 -force
read_verilog $root/rtl/pe.v
read_verilog $root/rtl/systolic_array_4x4.v
read_verilog $root/rtl/tpu_accel.v
read_verilog $root/rtl/wb_tpu_accel.v
read_verilog $root/rtl/spare_route_vrc.v
read_verilog $root/rtl/memetic_train_unit_lite.v
read_verilog $root/rtl/dfx/tpu_rp_rm_memetic_struct.v
synth_design -top tpu_rp -part xc7z010clg400-1 -mode out_of_context
report_utilization -hierarchical -file $outdir/util_hier.rpt
report_utilization -file $outdir/util.rpt
write_checkpoint -force $outdir/tpu_rp_memetic_struct_ooc.dcp
puts "PASS: memetic_struct OOC synth"
exit
