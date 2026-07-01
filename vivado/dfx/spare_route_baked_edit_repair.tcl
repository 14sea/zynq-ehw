# EHW-3.3: edit the routed baseline spare-route baked checkpoint into the repair.
#
# Run after build_spare_route_baked.tcl:
#   vivado -mode batch -source vivado/dfx/spare_route_baked_edit_repair.tcl
#
# Then bitread/diff the baseline impl_33 bitstream against
# spare_route_icap/dfx_top_spare_route_repair.bit and feed the set/clear bits to
# scripts/m75-build-frameseqs.py. Re-extract from every fresh routed build; target
# LUT physical sites can move between builds.

set origin [file normalize [file dirname [info script]]]
set outdir $origin/spare_route_icap
file mkdir $outdir

open_checkpoint $origin/build_srb/dfx.runs/impl_33/dfx_top_routed.dcp

array set want {
  g0   4'hB
  g1   4'h9
  g2   4'h9
  g3   4'h3
  g4   8'hB1
  g5   64'hAAAAAAAAAAAAAAAA
  g7   64'hFFFF0000FFFF0000
  g8   64'hCCCCCCCCCCCCCCCC
  g11  64'hAAAAAAAAAAAAAAAA
  g13  16'hF0F0
  g14  16'hFF00
}

foreach name {g0 g1 g2 g3 g4 g5 g7 g8 g11 g13 g14} {
  set pat "*u_sr_baked/u_baked/$name"
  set cells [get_cells -hier -filter "NAME =~ $pat"]
  if {[llength $cells] != 1} {
    error "expected exactly one cell for $pat, got [llength $cells]: $cells"
  }
  set cell [lindex $cells 0]
  set_property INIT $want($name) $cell
  puts "set $cell INIT -> [get_property INIT $cell]"
}

# This writes a same-route repaired bitstream. The marker remains SRB0 because the
# marker register is intentionally not edited; the live ICAP proof should show only
# the phenotype mask/fitness changes.
write_checkpoint -force $outdir/dfx_top_spare_route_repair.dcp
write_bitstream -force -file $outdir/dfx_top_spare_route_repair.bit
puts "=== wrote $outdir/dfx_top_spare_route_repair.bit ==="
exit
