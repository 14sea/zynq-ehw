# zynq_ehw — division of labour & workflow

This project is built by **two AIs with a human relay**. This doc is the contract
both sides follow. Read it before contributing.

## Roles

| Who | Owns | Cannot do |
|---|---|---|
| **ChatGPT** (codegen) | writes `sim/`, `sw/`, `host/`, `tests/`, `rtl/` (code, oracles, twins, firmware, RTL) | cannot touch the board; has no access to Claude's persistent memory |
| **Claude** (board + gatekeeper) | board bring-up, review, `git` commit/push, `.gitignore` & repo hygiene, `docs/board_results.md` | should not author large new features (reviews/fixes, not the primary author) |
| **Human** | the relay: carries files between the two AIs, makes scope/decision calls | — |

## Hard rules

1. **Every hardware-bound deliverable from ChatGPT ships with a host gate.**
   No board-targeted code is accepted without a host-side self-proof:
   numpy oracle + portable-C twin + bit-exact test + a **golden cross-check**
   against the relevant zynq_xpart HW-verified oracle. Rationale: ChatGPT can't
   see the board, so its code must be verifiable on the host first; Claude's
   board step then only has to confirm **board == host model**.

2. **Claude runs a fixed pre-commit checklist every time** (see below). Nothing
   is committed until it passes.

3. **Neither side's "done" is trusted; each is the other's gate.**
   ChatGPT marks milestones done in README → Claude must reproduce/verify each
   before committing. Claude's review claims → ChatGPT may rebut them from
   source. **No conclusion from memory or assumption — only from a command that
   was actually run.** (This rule exists because a Claude review once shipped a
   wrong blocker by misreading an arithmetic line; ChatGPT correctly rebutted it.)

4. **Hardware truth lives in the repo, not in one AI's head.** ChatGPT cannot
   read Claude's memory, so all board gotchas and numeric conventions go in
   `docs/hw_notes.md` (mailbox addresses, fixed-point conventions e.g.
   `down = 8 - wshift - ashift`, the two leaky variants, post-config settle, the
   M7.2 in-context-routing gremlin, load-via-`fpga loadb` etc.). Code is written
   against that file, not against guesses.

5. **Isolation is absolute.** `external/` holds read-only copies of
   `/home/test/zynq_xpart` and `/home/test/zynq_agentctl` for offline reference.
   Edit only the copies here; **never modify the source projects.** `external/`
   and `runs/` are gitignored — never commit them.

## Handoffs (both directions are files; the human carries them)

- **ChatGPT → Claude:** code + README updates + `docs/*_handoff.md` (what was
  built, what's incomplete, what board step is needed next).
- **Claude → ChatGPT:**
  - `review.vN.txt` — review with **file:line + evidence + a decisive test**, so
    ChatGPT can fix or rebut precisely. Deleted once resolved.
  - `docs/board_results.md` — structured board observations: exact mailbox hex,
    pass/fail per test vector, where/how it diverged, so ChatGPT can iterate
    blind on the next code drop.

## Claude's pre-commit checklist

Run before every commit; all must pass:

1. **Host gate green** — `python3 tests/compare_ehw0_twin.py` (and equivalents)
   PASS: Py↔C bit-exact **and** golden cross-check vs the zynq_xpart oracle.
2. **Board-verified** (only for hardware milestones) — the on-board result
   matches the host model; logged in `docs/board_results.md`.
3. **Isolation** — `git -C /home/test/zynq_xpart status -s` and the same for
   `zynq_agentctl` are both empty (source projects untouched).
4. **gitignore sanity** — `git status` shows no `external/`, no `runs/`, no
   `__pycache__/`, and no staged file > ~500 KB.
5. **Claims source-validated** — every assertion in the review/commit message is
   backed by a command that was actually run this session.

## Git policy

- Branch `main`. Commit locally without asking; **always ask the human before
  push** (human reviews first).
- Commit attribution is honest: code authored by ChatGPT, reviewed / board-tested
  by Claude — state it in the body. `Co-Authored-By: Claude` trailer.
- Remote: none yet. Before adding one, confirm branch is `main` (not `master`).

## Milestone ladder

See `docs/ehw_design.md` (§7) for the EHW-0 → EHW-1 → EHW-2 ladder and `§12` for
the prior-art lessons being applied. `docs/reference_map.md` lists what to mine
from the sibling projects.
