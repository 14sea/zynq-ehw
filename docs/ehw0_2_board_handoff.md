# EHW-0.2 Board Handoff Notes

EHW-0.2 in the design doc is "host-sent genomes evaluated on the VRC array".
The copied M7 references expose a practical constraint: in the current
`zynq_xpart` DFX top, NEORV32 `uart0` is not pinned out, so a direct UART
host-to-softcore genome stream is not available.

Current bridge step:

- `sw/ehw/ehw_eval_mbox.c` evaluates a compiled 24-byte champion genome on the
  register-loaded 4x4 VRC array.
- It publishes results through the existing PS-visible mailbox at `0x41200000`.
- This validates the critical path before adding a command channel:
  fixed-point C evaluator -> VRC register loads -> array MAC -> mailbox.

Mailbox tags:

- `0xE0000000`: firmware reached main.
- `0xE100xxxx`: array self-check result; low 16 bits should be `14`.
- `0xE2ccssss`: final score summary; `cc` is correct count, `ssss` is low 16 bits
  of SSE.
- `0xE3ffffff`: low 24 bits of fitness.

Expected default champion:

- correct: `40/40`
- SSE: `4799`
- fitness: `39995201`

Next decision for true host-in-loop EHW-0.2:

- add a PS->PL command path to the static design, or
- run the GA board-resident on NEORV32 first and use the mailbox only for progress
  and champion reporting.

The second option is likely simpler and closer to EHW-0.3, because it avoids a
new bidirectional mailbox before the core evaluator has been proven on silicon.
