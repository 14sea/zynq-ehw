# EHW-3.2 Results — Spare-Routing Fabric VRC Host Gate

Generated / verified by:

```bash
python3 tests/compare_spare_route_vrc.py
```

Status: **HOST GATE PASS — board run pending**. No board claim is made in this
document.

## Deliverables

- `rtl/spare_route_vrc.v`: register-configured fabric VRC island implementing
  the frozen EHW-3 16-byte genome contract.
- `rtl/dfx/tpu_rp_rm_spare_route_vrc.v`: optional DFX RM wrapper for the existing
  `tpu_rp` XBUS port contract.
- `sw/ehw/spare_route_vrc_mbox.c`: NEORV32 firmware that measures GA fitness
  through the VRC register interface.
- `tests/tb_spare_route_vrc.v`: RTL host test for config load, fault injection,
  input sweep, output readback, and repaired phenotype.
- `tests/compare_spare_route_vrc.py`: host gate tying RTL sim, firmware host
  stub, Py/C oracle cross-check, wrapper compile, and Vivado OOC synth hook.
- `tests/vivado_ooc_spare_route_vrc.tcl`: Vivado out-of-context synth gate.

## VRC Register Contract

The VRC accepts the same 16-byte genome as `sim/oracle_spare_routing.py` and
`sw/ehw/spare_route_kernel.h`.

Register map:

```text
0x000 CTRL       W  bit4 clears genome/input/fault registers
0x004 STATUS     R  bit0 = ready
0x008 INPUT      RW [2:0] = {x2,x1,x0}
0x00C OUTPUT     R  bit0 = O(input)
0x010 MASK       R  [7:0] truth-table mask under current fault
0x014 FITNESS    R  0..8 matches against majority target 0xe8
0x018 FAULT      RW [2:0] kind, [6:4] node, [9:8] route_section,
                    [13:10] route_idx, [17:14] route_mux
0x01C USES       R  bit0 = O selects A1, bit1 = O selects AS
0x020 MARKER     R  "SRV0"
0x040+i*4 GENi   RW low8 = genome byte, i=0..15
```

## Verified Host Output

```text
PASS: spare_route_vrc config/fault/sweep recovery checks passed
SPARE_ROUTE_VRC nofault gen=16 fit=8/8 mask=e8 degraded_fit=6/8 degraded_mask=88
SPARE_ROUTE_VRC repair gen=19 fit=8/8 mask=e8 uses=2 genome=0e 06 0e 01 68 00 01 04 01 02 00 01 02 00 03 02
PASS: spare-routing Python oracle and C twin are bit-exact
fault masks:
FAULT_NONE,e8,8
FAULT_STUCK0(A1),88,6
FAULT_STUCK1(A1),ee,6
FAULT_DISABLE_NODE(A1),88,6
FAULT_DISABLE_ROUTE(O.in1),cc,6
FAULT_DISABLE_ROUTE(A1.in0),88,6
```

Vivado is not on the PATH used by `tests/compare_spare_route_vrc.py`, so the
scripted gate reported:

```text
SKIP: vivado not found; OOC synth gate is available via tests/vivado_ooc_spare_route_vrc.tcl
```

The OOC gate was then run separately by Claude (Vivado 2025.2,
`tests/vivado_ooc_spare_route_vrc.tcl`, part `xc7z010clg400-1`) and **PASSED**:

```text
synth_design completed successfully
Synthesis finished with 0 errors, 0 critical warnings and 74 warnings.
Report Instance Areas: top/u_spare_route(wb_spare_route_vrc)=579 cells; u_vrc(spare_route_vrc)=549 cells
PASS: spare_route_vrc OOC synth
```

This is the gate iverilog cannot provide (it caught the earlier `cgp_vrc.v`
no-input-function synth error); the `input dummy` arguments on `fitness_count` /
`pack_fault` are what keep this island synthesizable. Board mailbox evidence is
still the remaining gate for an EHW-3.2 board claim.

## Interpretation

The RTL VRC loads the exact host-discovered no-fault and repaired genomes into
fabric registers and computes the same masks as the Python/C oracle:

- no fault: `mask=e8`, `fitness=8/8`
- injected `DISABLE_NODE(A1)`: `mask=88`, `fitness=6/8`
- injected `DISABLE_ROUTE(O.in1)`: `mask=cc`, `fitness=6/8`
- repaired genome under `DISABLE_NODE(A1)`: `mask=e8`, `fitness=8/8`

The firmware host stub runs the same no-fault and recovery GA, but all fitness
measurements go through the VRC register protocol. This is the pre-board proof
for EHW-3.2; the next gate is Vivado OOC synth and then board mailbox evidence.

