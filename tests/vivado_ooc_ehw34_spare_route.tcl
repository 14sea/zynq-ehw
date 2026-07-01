set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/..]
set outdir $root/runs/tests/vivado_ooc_ehw34_spare_route
file mkdir $outdir

create_project ehw34_spare_route_ooc $outdir -part xc7z010clg400-1 -force
read_verilog $root/rtl/spare_route_baked.v
read_verilog $root/rtl/ehw34_spare_route_target.v
synth_design -top ehw34_spare_route_target -part xc7z010clg400-1 -mode out_of_context
report_utilization -file $outdir/util.rpt
write_checkpoint -force $outdir/ehw34_spare_route_target_ooc.dcp
puts "PASS: ehw34_spare_route_target OOC synth"
exit
