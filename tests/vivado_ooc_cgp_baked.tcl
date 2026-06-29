set origin [file normalize [file dirname [info script]]]
set root   [file normalize $origin/..]
set outdir $root/runs/tests/vivado_ooc_cgp_baked
file mkdir $outdir

create_project cgp_baked_ooc $outdir -part xc7z010clg400-1 -force
read_verilog $root/rtl/cgp_baked.v
read_verilog $root/rtl/dfx/tpu_rp_rm_cgp_baked_champ.v
synth_design -top tpu_rp -part xc7z010clg400-1 -mode out_of_context
report_utilization -file $outdir/util.rpt
write_checkpoint -force $outdir/tpu_rp_cgp_baked_champ_ooc.dcp
puts "PASS: cgp_baked champion OOC synth"
exit
