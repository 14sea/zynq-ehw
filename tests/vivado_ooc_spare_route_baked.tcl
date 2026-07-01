set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/..]
set outdir $root/runs/tests/vivado_ooc_spare_route_baked
file mkdir $outdir

create_project spare_route_baked_ooc $outdir -part xc7z010clg400-1 -force
read_verilog $root/rtl/spare_route_baked.v
read_verilog $root/rtl/dfx/tpu_rp_rm_spare_route_baked_repair.v
synth_design -top tpu_rp -part xc7z010clg400-1 -mode out_of_context
report_utilization -file $outdir/util.rpt
write_checkpoint -force $outdir/tpu_rp_spare_route_baked_repair_ooc.dcp
puts "PASS: spare_route_baked repair OOC synth"
exit
