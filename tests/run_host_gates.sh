#!/usr/bin/env bash
# One-command host-side gate runner for zynq_ehw.
#
# Runs every host self-proof (numpy oracle <-> portable-C twin bit-exact, golden
# cross-checks, RTL host gates) WITHOUT a board or Vivado. This is the gate that
# must be green before any board run (see docs/workflow.md).
#
#   tests/run_host_gates.sh           # run all host gates
#   PY=python3 tests/run_host_gates.sh
#
# Vivado OOC synth_design checks (compare_cgp_vrc / compare_cgp_baked) are run with
# --skip-ooc here because they need Vivado on PATH; a board/Vivado environment can
# drop --skip-ooc to also gate RTL against Vivado synthesis strictness.
set -u
cd "$(dirname "$0")/.."
PY="${PY:-python3}"
command -v "$PY" >/dev/null 2>&1 || PY=/home/test/xilinx/.env/bin/python

gates=(
  "compare_ehw0_twin.py"                 # EHW-0.0/0.1: weight GA, Py<->C bit-exact + m753 golden
  "compare_cgp_twin.py"                  # EHW-1.0: CGP multiplier GA, Py<->C bit-exact
  "compare_cgp_vrc.py --skip-ooc"        # EHW-1.1-fabric: cgp_vrc RTL sim + firmware stub
  "compare_cgp_baked.py --skip-ooc"      # EHW-1.2: baked-CGP RTL/firmware host gate
  "compare_ehw2_micro.py"                # EHW-2: ICAPE2 micro oracle + framebank pack contract
)

fail=0
for g in "${gates[@]}"; do
  name="${g%% *}"
  if "$PY" tests/$g >/dev/null 2>&1; then
    printf '  PASS  %s\n' "$name"
  else
    printf '  FAIL  %s\n' "$name"; fail=1
  fi
done

# EHW-0.4 deployment-metric reproducibility (deterministic, no pass/fail — just runs)
"$PY" sim/ehw0_4_compare.py --seed 3 --population 32 --generations 64 >/dev/null 2>&1 \
  && printf '  OK    ehw0_4_compare (deployment metric regenerated)\n'

if [ "$fail" -eq 0 ]; then echo "ALL HOST GATES PASS"; else echo "SOME HOST GATES FAILED"; fi
exit $fail
