set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/..]
set outdir $root/runs/tests/vivado_ooc_cgp_vrc
file mkdir $outdir

create_project cgp_vrc_ooc $outdir -part xc7z010clg400-1 -force
read_verilog $root/rtl/cgp_vrc.v
read_verilog $root/rtl/dfx/tpu_rp_rm_cgp_vrc.v
synth_design -top tpu_rp -part xc7z010clg400-1 -mode out_of_context
report_utilization -file $outdir/util.rpt
write_checkpoint -force $outdir/tpu_rp_cgp_vrc_ooc.dcp
puts "PASS: cgp_vrc OOC synth"
exit
