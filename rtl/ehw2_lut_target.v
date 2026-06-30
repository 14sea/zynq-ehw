// EHW-2 editable LUT target.
//
// A single DONT_TOUCH LUT6 is the evolvable substrate. Firmware writes the low
// three input bits, reads q[0], and scores the observed 8-row truth table after
// each ICAPE2 frame write. ICAP edits only this LUT's INIT bits; routing stays
// fixed by construction.
module ehw2_lut_target(
    input  [5:0]  i,
    output [31:0] q
);
    wire o;
    (* DONT_TOUCH = "TRUE" *)
    LUT6 #(.INIT(64'h0000000000000000)) l_ehw2 (
        .O(o),
        .I0(i[0]), .I1(i[1]), .I2(i[2]),
        .I3(i[3]), .I4(i[4]), .I5(i[5]));
    assign q = {31'b0, o};
endmodule
