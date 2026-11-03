# Prior art — what in this project was already done by others

Added 2026-11-03 after a full-text literature review (≈75 papers, 8 external code
bases, AMD UG470/UG585/DS187/PG134/UG480/XAPP538/UG908). The review notes live outside
this repo (`ehw_research/analysis/`); this file is the summary that applies here.

**Bottom line: none of the mechanisms in this project are new.** On-chip processors
rewriting real LUTs through ICAP for evolution, on Zynq-7000 included, were published
between 2006 and 2020. What this repo adds is engineering and evidence practice
(an open prjxray bit-location layer, a NEORV32 soft-core driving fabric ICAPE2, and
host-oracle ↔ C-twin ↔ board bit-exact gates), not a new mechanism. Earlier wording in
`README.md`, `docs/WRITEUP.md` and `docs/ehw_design.md` §11 that positioned the project
only against CoBEA/Whitley (iCE40) overstated the gap and has been corrected.

"Not found" below means *not found in the 2026-11 search*; three such verdicts were
overturned during that same search, so treat them as provisional.

## 1. Mechanism-by-mechanism

| What we do | Closest prior work | Hardware | Similarity | Our actual difference |
|---|---|---|---|---|
| On-chip processor rewrites a live LUT through ICAP during evolution (EHW-2, EHW-3.4) | Upegui & Sanchez, IPDPS 2006 RAW / AHS 2006 (MicroBlaze soft-core + OPB HWICAP); Cancare et al., ISCAS 2010 / TRETS 2012 (PowerPC 405) | Virtex-II, Virtex-4 | Same mechanism; Upegui even used a soft-core | RISC-V (NEORV32) instead of MicroBlaze/PPC; 7-series |
| Same, **on Zynq-7000 / 7-series** | Dobai & Sekanina, "ZyEHW" (ICES 2013, AHS 2013, ICES 2014, FPL 2014, TRETS 2015): ARM PS + PCAP, difference-based LUT DPR, 8,700–30,000 candidates/s; García et al., Electronics 2020: ARM + IMPRESS (open source) rewriting LUT constants via ICAP, 41.7 µs per configuration; Norman-Tenazas et al. 2024 (CFGLUT5) | Zynq-7020 | Same chip family, same per-evaluation LUT rewriting | Driver sits in the PL (soft-core + fabric ICAPE2); the PS only stages candidates and hands over ICAP once; prjxray for bit location instead of a vendor-diff or IMPRESS flow |
| VRC (register-configured virtual circuit) + LUT/ICAP hybrid (EHW-0/1) | Sekanina 2003 (VRC concept); Dobai & Sekanina AHS 2013 / TRETS 2015 (hybrid VRC + DPR) | Zynq-7020 (hybrid) | Same hybrid idea | — |
| LUT used as a routing mux; fixed outer routing, evolved local select (EHW-3 spare-route island) | Hübner, Becker, Becker, SBCCI 2004 (LUT-based network topologies); Upegui AHS 2006 (LUT-as-mux, deliberately no PIP edits); Cancare 2010 (predefined MUXA1–4 LUT configurations) | Virtex-II, Virtex-4 | Same "route through LUT contents, never raw PIPs" safety choice | — |
| Fault injection then evolutionary repair with spare resources (EHW-3) | Salvador et al., ReConFig 2011; Garvie & Thompson, IOLTS 2004 (simulation); Lohn, Larchev, DeMara 2003 (mostly simulation) | Virtex-5, simulated | Same experiment shape | Our island is tiny (3-input majority) |
| Evolved NN weights baked into LUT constants (EHW-0.5 LUT-KCM) | García et al. 2020 (BbNN weights/biases as LUT constants, rewritten during neuroevolution) | Zynq-7020 | Same idea, larger network | — |
| Memetic GA × HW-SGD (EHW-4/5) | **Not searched** in the review | — | — | — |
| PCAP_PR=0 for the ICAP path; no GRESTORE; ICAP readback handshake | AMD UG585 v1.15 (PL configuration paths, "set PCAP_PR = 0" for ICAP); UG470 v1.17 (GRESTORE resets CLB/IOB FFs; ABORT rules) | — | Documented vendor behaviour | We reproduced and recorded it on this board; **not a discovery** |
| Host Python oracle ↔ portable-C twin ↔ board, bit-exact gate before every board run | Not found as a published practice | — | — | Evidence practice, not mechanism |

## 2. Where prior work is ahead of this repo

- **On-chip frame synthesis from the genome.** Cancare, Zhang 2014 and ZyEHW build the
  candidate frame on chip. EHW-2 and EHW-3.4 pick from a small set of candidates
  **pre-staged by the PS**.
- **Read-modify-write of a single LUT.** Hübner 2006, Glette 2014 and VR-ZyCAP 2021 keep
  the rest of the frame; we overwrite whole frames from a build-time snapshot.
- **Measured reconfiguration time / throughput.** Every precedent reports it
  (Cancare 0.121 ms per individual; ZyEHW 8.7k–30k evals/s; García 41.7 µs). We never
  measured ours.
- **Statistics over many runs.** ZyEHW reports 30 runs with quartiles; this repo reports
  single runs.
- **Task size.** Precedents evolve image filters, 26-bit parity, 7-bit multipliers; ours
  are a single LUT, a 2-bit multiplier and a 3-input majority.

## 3. Known issues found during the review

- `rtl/xbus_icap.v` (READING state, lines 95–106): switching from WRITING to READING
  keeps `csib` low while `rdwrb` goes 0→1. UG470 v1.17 (p.38, p.43, p.120) says this
  triggers an ABORT, and readback data is only valid three clocks after CSI_B is
  asserted. The read path was never used on the board; fix it before relying on it.
- ICAP once ran on FCLK0 at 125 MHz (the miner FSBL default). DS187 v1.21 gives
  F<sub>ICAPCK</sub> max = 100 MHz, so that was out of spec; the documented flow pins
  FCLK0 to 50 MHz.
- A LUT6 INIT spans **four** minor frames (16 bits each). Only the low INIT bits edited
  in EHW-2 fall into two of them, and which two depends on the site (prjxray zynq7:
  SLICEL_X0 minors 32/33, SLICEL_X1 26/27, SLICEM_X0 34/35).

## 4. References (DOIs checked against the papers where given)

- Upegui, Sanchez. On-chip and on-line self-reconfigurable adaptable platform: the non-uniform cellular automata case. IPDPS 2006 (RAW). — Evolving hardware with self-reconfigurable connectivity in Xilinx FPGAs. AHS 2006.
- Cancare, Santambrogio, Sciuto. A direct bitstream manipulation approach for Virtex4-based evolvable systems. ISCAS 2010. — Cancare et al. On the evolution of hardware circuits via reconfigurable architectures. ACM TRETS 5(4):22, 2012.
- Dobai, Sekanina. Towards evolvable systems based on the Xilinx Zynq platform. ICES 2013, doi:10.1109/ICES.2013.6613287. — Image filter evolution on the Xilinx Zynq platform. AHS 2013, doi:10.1109/AHS.2013.6604241. — Dobai, Glette, Torresen, Sekanina. ICES 2014, doi:10.1109/ICES.2014.7008726. — Dobai. FPL 2014, doi:10.1109/FPL.2014.6927455. — Dobai, Sekanina. Low-level flexible architecture with hybrid reconfiguration for evolvable hardware. ACM TRETS 8(3):20, 2015, doi:10.1145/2700414. Code: github.com/dobairoland/ZyEHW (GPL-3.0).
- García, Zamacola, Otero, de la Torre. A dynamically reconfigurable BbNN architecture for scalable neuroevolution in hardware. Electronics 9(5):803, 2020, doi:10.3390/electronics9050803.
- Norman-Tenazas et al. arXiv:2403.13105, 2024.
- Hübner, Becker, Becker. Real-time LUT-based network topologies for dynamic and partial FPGA self-reconfiguration. SBCCI 2004, doi:10.1145/1016568.1016583.
- Salvador et al. Fault tolerance analysis and self-healing strategy of autonomous, evolvable hardware systems. ReConFig 2011.
- Garvie, Thompson. Scrubbing away transients and jiggling around the permanent. IOLTS 2004.
- Lohn, Larchev, DeMara. Evolutionary fault recovery in a Virtex FPGA using a representation that incorporates routing. IPDPS 2003, doi:10.1109/IPDPS.2003.1213316.
- Glette, Kaufmann. Lookup table partial reconfiguration for an evolvable hardware classifier system. CEC 2014, doi:10.1109/CEC.2014.6900503.
- Zhang et al. A LUT manipulation based intrinsic evolvable system. IEICE Electronics Express 11, 2014, doi:10.1587/elex.11.20131003.
- Sultana et al. VR-ZYCAP: a versatile resource-level ICAP controller for ZYNQ SoC. Electronics 2021.
- AMD UG470 v1.17 (7 Series Configuration), UG585 v1.15 (Zynq 7000 TRM), DS187 v1.21.
