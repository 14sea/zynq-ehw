# EHW-5.5: edit the routed no-fault baseline checkpoint into the structural champion.
#
# Run after build_ehw55_baked.tcl:
#   vivado -mode batch -source vivado/dfx/ehw55_edit_champion.tcl
#
# Edits ONLY the six cells frozen by tests/compare_ehw55_reveal_contract.py
# (g0 g1 g7 g8 g12 g14). Marker stays "SR55" — intentionally not edited; the
# live ICAP proof must show only the phenotype (truth/feature mask) change.
# Then bitread/diff the baseline impl_55 bitstream against
# ehw55_icap/dfx_top_ehw55_champion.bit and feed set/clear bits to
# scripts/m75-build-frameseqs.py. Never reuse frames from an older build.

set origin [file normalize [file dirname [info script]]]
set outdir $origin/ehw55_icap
file mkdir $outdir

open_checkpoint $origin/build_e55/dfx.runs/impl_55/dfx_top_routed.dcp

array set want {
  g0   4'h8
  g1   4'h0
  g7   64'h0000000000000000
  g8   64'h0000000000000000
  g12  64'hCCCCCCCCCCCCCCCC
  g14  16'hFF00
}

foreach name {g0 g1 g7 g8 g12 g14} {
  set pat "*u_sr_baked/u_baked/$name"
  set cells [get_cells -hier -filter "NAME =~ $pat"]
  if {[llength $cells] != 1} {
    error "expected exactly one cell for $pat, got [llength $cells]: $cells"
  }
  set cell [lindex $cells 0]
  set_property INIT $want($name) $cell
  puts "set $cell INIT -> [get_property INIT $cell]"
}

write_checkpoint -force $outdir/dfx_top_ehw55_champion.dcp
write_bitstream -force -file $outdir/dfx_top_ehw55_champion.bit
puts "=== wrote $outdir/dfx_top_ehw55_champion.bit ==="
exit
