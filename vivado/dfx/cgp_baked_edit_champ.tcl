# EHW-1.2: edit the routed baseline baked-CGP checkpoint into the champion.
#
# Run after build_cgp_baked.tcl:
#   vivado -mode batch -source vivado/dfx/cgp_baked_edit_champ.tcl
#
# Then bitread/diff the baseline impl_11 bitstream against cgp_icap/dfx_top_cgp_champ.bit
# and feed the set/clear bits to scripts/m75-build-frameseqs.py.

set origin [file normalize [file dirname [info script]]]
set outdir $origin/cgp_icap
file mkdir $outdir

open_checkpoint $origin/build/dfx.runs/impl_11/dfx_top_routed.dcp

array set want {
  n8  16'hA0A0
  n9  16'h6AC0
  n10 16'h4C00
  n11 16'h8000
}

foreach name {n8 n9 n10 n11} {
  set pat "*u_baked/$name"
  set cells [get_cells -hier -filter "NAME =~ $pat"]
  if {[llength $cells] != 1} {
    error "expected exactly one cell for $pat, got [llength $cells]: $cells"
  }
  set cell [lindex $cells 0]
  set_property INIT $want($name) $cell
  puts "set $cell INIT -> [get_property INIT $cell]"
}

write_bitstream -force -file $outdir/dfx_top_cgp_champ.bit
puts "=== wrote $outdir/dfx_top_cgp_champ.bit ==="
exit
