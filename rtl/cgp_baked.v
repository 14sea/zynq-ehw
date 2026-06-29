`timescale 1ns/1ps

// cgp_baked.v -- hardwired CGP LUT grid for EHW-1.2 ICAP-bake reveal.
//
// Unlike cgp_vrc.v, the genome is not held in writable registers. Each CGP node
// is an explicit LUT4 primitive with a compile-time INIT. ICAP edits change those
// LUT INITs in real fabric, so firmware must evaluate by driving INPUT and reading
// OUTPUT, not by reading a shadow software/register model.
//
// Register map, byte offsets:
//   0x004 STATUS  R  bit0 = ready/done (combinational grid, always 1)
//   0x008 INPUT   RW [3:0] = {b1,b0,a1,a0} as idx bits [3:0]
//   0x00C OUTPUT  R  [3:0] = {p3,p2,p1,p0}
//   0x020 MARKER  R  build marker supplied by wrapper parameter

`ifdef SIM
module LUT4 #(
    parameter [15:0] INIT = 16'h0000
) (
    output O,
    input I0,
    input I1,
    input I2,
    input I3
);
    assign O = INIT[{I3, I2, I1, I0}];
endmodule
`endif

module cgp_baked #(
    parameter [15:0] INIT0  = 16'haaaa,
    parameter [15:0] INIT1  = 16'hcccc,
    parameter [15:0] INIT2  = 16'hf0f0,
    parameter [15:0] INIT3  = 16'hff00,
    parameter [15:0] INIT4  = 16'haaaa,
    parameter [15:0] INIT5  = 16'hcccc,
    parameter [15:0] INIT6  = 16'hf0f0,
    parameter [15:0] INIT7  = 16'hff00,
    parameter [15:0] INIT8  = 16'h0000,
    parameter [15:0] INIT9  = 16'h0000,
    parameter [15:0] INIT10 = 16'h0000,
    parameter [15:0] INIT11 = 16'h0000
) (
    input  wire [3:0] idx,
    output wire [3:0] out
);
    wire [3:0] c0;
    wire [3:0] c1;

    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT0))  n0  (.O(c0[0]),  .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(idx[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT1))  n1  (.O(c0[1]),  .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(idx[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT2))  n2  (.O(c0[2]),  .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(idx[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT3))  n3  (.O(c0[3]),  .I0(idx[0]), .I1(idx[1]), .I2(idx[2]), .I3(idx[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT4))  n4  (.O(c1[0]),  .I0(c0[0]),  .I1(c0[1]),  .I2(c0[2]),  .I3(c0[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT5))  n5  (.O(c1[1]),  .I0(c0[0]),  .I1(c0[1]),  .I2(c0[2]),  .I3(c0[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT6))  n6  (.O(c1[2]),  .I0(c0[0]),  .I1(c0[1]),  .I2(c0[2]),  .I3(c0[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT7))  n7  (.O(c1[3]),  .I0(c0[0]),  .I1(c0[1]),  .I2(c0[2]),  .I3(c0[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT8))  n8  (.O(out[0]), .I0(c1[0]),  .I1(c1[1]),  .I2(c1[2]),  .I3(c1[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT9))  n9  (.O(out[1]), .I0(c1[0]),  .I1(c1[1]),  .I2(c1[2]),  .I3(c1[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT10)) n10 (.O(out[2]), .I0(c1[0]),  .I1(c1[1]),  .I2(c1[2]),  .I3(c1[3]));
    (* DONT_TOUCH = "yes" *) LUT4 #(.INIT(INIT11)) n11 (.O(out[3]), .I0(c1[0]),  .I1(c1[1]),  .I2(c1[2]),  .I3(c1[3]));
endmodule

module wb_cgp_baked #(
    parameter [31:0] MARKER = 32'h4347_5030, // "CGP0"
    parameter [15:0] INIT0  = 16'haaaa,
    parameter [15:0] INIT1  = 16'hcccc,
    parameter [15:0] INIT2  = 16'hf0f0,
    parameter [15:0] INIT3  = 16'hff00,
    parameter [15:0] INIT4  = 16'haaaa,
    parameter [15:0] INIT5  = 16'hcccc,
    parameter [15:0] INIT6  = 16'hf0f0,
    parameter [15:0] INIT7  = 16'hff00,
    parameter [15:0] INIT8  = 16'h0000,
    parameter [15:0] INIT9  = 16'h0000,
    parameter [15:0] INIT10 = 16'h0000,
    parameter [15:0] INIT11 = 16'h0000
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
    reg [3:0] input_idx;
    wire [3:0] out_bits;
    wire [11:0] off = xbus_adr[11:0];
    wire access = xbus_cyc && xbus_stb;

    assign xbus_err = 1'b0;
    assign dbg_leds = out_bits;

    cgp_baked #(
        .INIT0(INIT0), .INIT1(INIT1), .INIT2(INIT2), .INIT3(INIT3),
        .INIT4(INIT4), .INIT5(INIT5), .INIT6(INIT6), .INIT7(INIT7),
        .INIT8(INIT8), .INIT9(INIT9), .INIT10(INIT10), .INIT11(INIT11)
    ) u_baked (
        .idx(input_idx),
        .out(out_bits)
    );

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            input_idx  <= 4'd0;
            xbus_ack   <= 1'b0;
            xbus_dat_r <= 32'd0;
        end else begin
            xbus_ack <= access && !xbus_ack;
            if (access && xbus_we && (xbus_sel != 4'b0000) && off == 12'h008)
                input_idx <= xbus_dat_w[3:0];
            case (off)
                12'h004: xbus_dat_r <= 32'h0000_0001;
                12'h008: xbus_dat_r <= {28'd0, input_idx};
                12'h00c: xbus_dat_r <= {28'd0, out_bits};
                12'h020: xbus_dat_r <= MARKER;
                default: xbus_dat_r <= 32'd0;
            endcase
        end
    end
endmodule
