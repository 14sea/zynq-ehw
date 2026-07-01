`timescale 1ns/1ps

// spare_route_baked.v -- hardwired EHW-3 spare-routing island for ICAP repair.
//
// This is the EHW-3.3 baked analogue of spare_route_vrc.v. The frozen 16-byte
// genome is compiled into explicit LUT primitives:
//   g0..g3   = C1 logic LUT2 INITs
//   g4       = O logic LUT3 INIT
//   g5..g12  = C1 input-select mux LUT6 INITs, source pool [x0,x1,x2,ZERO,ONE]
//   g13..g15 = O input-select mux LUT4 INITs, sources [A0,A1,A2,AS]
//
// The island has a modeled hard fault for EHW-3.3: A1 is disabled as an output
// source. Therefore the POP=128 no-fault champion bakes to mask c8 / fitness 7,
// while the repaired genome bakes to mask e8 / fitness 8. ICAP repair changes
// only the g0..g15 LUT INIT properties, not routing.

`ifdef SIM
module LUT2 #(parameter [3:0] INIT = 4'h0) (output O, input I0, input I1);
    assign O = INIT[{I1, I0}];
endmodule

module LUT3 #(parameter [7:0] INIT = 8'h00) (output O, input I0, input I1, input I2);
    assign O = INIT[{I2, I1, I0}];
endmodule

module LUT4 #(parameter [15:0] INIT = 16'h0000) (output O, input I0, input I1, input I2, input I3);
    assign O = INIT[{I3, I2, I1, I0}];
endmodule

module LUT6 #(parameter [63:0] INIT = 64'h0) (
    output O,
    input I0,
    input I1,
    input I2,
    input I3,
    input I4,
    input I5
);
    assign O = INIT[{I5, I4, I3, I2, I1, I0}];
endmodule
`endif

module spare_route_baked #(
    parameter [7:0] G0  = 8'h0a,
    parameter [7:0] G1  = 8'h08,
    parameter [7:0] G2  = 8'h01,
    parameter [7:0] G3  = 8'h0f,
    parameter [7:0] G4  = 8'h32,
    parameter [7:0] G5  = 8'h01,
    parameter [7:0] G6  = 8'h04,
    parameter [7:0] G7  = 8'h00,
    parameter [7:0] G8  = 8'h02,
    parameter [7:0] G9  = 8'h02,
    parameter [7:0] G10 = 8'h00,
    parameter [7:0] G11 = 8'h04,
    parameter [7:0] G12 = 8'h01,
    parameter [7:0] G13 = 8'h01,
    parameter [7:0] G14 = 8'h02,
    parameter [7:0] G15 = 8'h00
) (
    input  wire [2:0] idx,
    output wire       out
);
    function [63:0] pool_mux_init;
        input [7:0] sel;
        begin
            case (sel)
                8'd0: pool_mux_init = 64'hAAAA_AAAA_AAAA_AAAA; // x0 / I0
                8'd1: pool_mux_init = 64'hCCCC_CCCC_CCCC_CCCC; // x1 / I1
                8'd2: pool_mux_init = 64'hF0F0_F0F0_F0F0_F0F0; // x2 / I2
                8'd4: pool_mux_init = 64'hFFFF_0000_FFFF_0000; // ONE via I4
                default: pool_mux_init = 64'h0000_0000_0000_0000; // ZERO/default
            endcase
        end
    endfunction

    function [15:0] out_mux_init;
        input [7:0] sel;
        begin
            case (sel)
                8'd0: out_mux_init = 16'hAAAA; // A0 / I0
                8'd1: out_mux_init = 16'hCCCC; // A1 / I1
                8'd2: out_mux_init = 16'hF0F0; // A2 / I2
                8'd3: out_mux_init = 16'hFF00; // AS / I3
                default: out_mux_init = 16'hAAAA; // A0/default
            endcase
        end
    endfunction

    wire zero = 1'b0;
    wire one  = 1'b1;

    wire a0_i0, a0_i1;
    wire a1_i0, a1_i1;
    wire a2_i0, a2_i1;
    wire as_i0, as_i1;
    wire a0, a1_raw, a1_disabled, a2, as_node;
    wire o_i0, o_i1, o_i2;

    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G5)))  g5  (.O(a0_i0), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));
    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G6)))  g6  (.O(a0_i1), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));
    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G7)))  g7  (.O(a1_i0), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));
    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G8)))  g8  (.O(a1_i1), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));
    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G9)))  g9  (.O(a2_i0), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));
    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G10))) g10 (.O(a2_i1), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));
    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G11))) g11 (.O(as_i0), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));
    (* DONT_TOUCH = "yes" *) LUT6 #(.INIT(pool_mux_init(G12))) g12 (.O(as_i1), .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(zero), .I4(one), .I5(zero));

    (* DONT_TOUCH = "yes" *) LUT2 #(.INIT(G0[3:0])) g0 (.O(a0),     .I0(a0_i0), .I1(a0_i1));
    (* DONT_TOUCH = "yes" *) LUT2 #(.INIT(G1[3:0])) g1 (.O(a1_raw), .I0(a1_i0), .I1(a1_i1));
    (* DONT_TOUCH = "yes" *) LUT2 #(.INIT(G2[3:0])) g2 (.O(a2),     .I0(a2_i0), .I1(a2_i1));
    (* DONT_TOUCH = "yes" *) LUT2 #(.INIT(G3[3:0])) g3 (.O(as_node),.I0(as_i0), .I1(as_i1));

    assign a1_disabled = 1'b0;

    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(out_mux_init(G13))) g13 (.O(o_i0), .I0(a0), .I1(a1_disabled), .I2(a2), .I3(as_node));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(out_mux_init(G14))) g14 (.O(o_i1), .I0(a0), .I1(a1_disabled), .I2(a2), .I3(as_node));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(out_mux_init(G15))) g15 (.O(o_i2), .I0(a0), .I1(a1_disabled), .I2(a2), .I3(as_node));
    (* DONT_TOUCH = "yes" *) LUT3 #(.INIT(G4)) g4 (.O(out), .I0(o_i0), .I1(o_i1), .I2(o_i2));
endmodule

module wb_spare_route_baked #(
    parameter [31:0] MARKER = 32'h5352_4230, // "SRB0"
    parameter [7:0] G0  = 8'h0a,
    parameter [7:0] G1  = 8'h08,
    parameter [7:0] G2  = 8'h01,
    parameter [7:0] G3  = 8'h0f,
    parameter [7:0] G4  = 8'h32,
    parameter [7:0] G5  = 8'h01,
    parameter [7:0] G6  = 8'h04,
    parameter [7:0] G7  = 8'h00,
    parameter [7:0] G8  = 8'h02,
    parameter [7:0] G9  = 8'h02,
    parameter [7:0] G10 = 8'h00,
    parameter [7:0] G11 = 8'h04,
    parameter [7:0] G12 = 8'h01,
    parameter [7:0] G13 = 8'h01,
    parameter [7:0] G14 = 8'h02,
    parameter [7:0] G15 = 8'h00
) (
    input         clk,
    input         rst_n,
    input  [31:0] xbus_adr,
    input  [31:0] xbus_dat_w,
    input  [3:0]  xbus_sel,
    input         xbus_we,
    input         xbus_stb,
    input         xbus_cyc,
    output reg [31:0] xbus_dat_r,
    output reg        xbus_ack,
    output            xbus_err,
    output [3:0]      dbg_leds
);
    reg [2:0] input_idx;
    wire out_bit;
    wire [11:0] off = xbus_adr[11:0];
    wire access = xbus_cyc && xbus_stb;

    assign xbus_err = 1'b0;
    assign dbg_leds = {3'd0, out_bit};

    spare_route_baked #(
        .G0(G0), .G1(G1), .G2(G2), .G3(G3), .G4(G4), .G5(G5), .G6(G6), .G7(G7),
        .G8(G8), .G9(G9), .G10(G10), .G11(G11), .G12(G12), .G13(G13), .G14(G14), .G15(G15)
    ) u_baked (
        .idx(input_idx),
        .out(out_bit)
    );

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            input_idx  <= 3'd0;
            xbus_ack   <= 1'b0;
            xbus_dat_r <= 32'd0;
        end else begin
            xbus_ack <= access && !xbus_ack;
            if (access && xbus_we && (xbus_sel != 4'b0000) && off == 12'h008)
                input_idx <= xbus_dat_w[2:0];
            case (off)
                12'h004: xbus_dat_r <= 32'h0000_0001;
                12'h008: xbus_dat_r <= {29'd0, input_idx};
                12'h00c: xbus_dat_r <= {31'd0, out_bit};
                12'h020: xbus_dat_r <= MARKER;
                default: xbus_dat_r <= 32'd0;
            endcase
        end
    end
endmodule

