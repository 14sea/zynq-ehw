`timescale 1ns/1ps

// EHW-3.3 repaired baked spare-route island.
// POP=128 repaired champion under hard DISABLE_NODE(A1): mask e8, fitness 8/8.

module tpu_rp (
    input         clk,
    input         rst_n,
    input  [31:0] xbus_adr,
    input  [31:0] xbus_dat_w,
    input  [3:0]  xbus_sel,
    input         xbus_we,
    input         xbus_stb,
    input         xbus_cyc,
    output [31:0] xbus_dat_r,
    output        xbus_ack,
    output        xbus_err,
    output [3:0]  dbg_leds
);
    wb_spare_route_baked #(
        .MARKER(32'h5352_4231), // "SRB1"
        .G0(8'h0b), .G1(8'h09), .G2(8'h09), .G3(8'h03),
        .G4(8'hb1), .G5(8'h00), .G6(8'h04), .G7(8'h04),
        .G8(8'h01), .G9(8'h02), .G10(8'h00), .G11(8'h00),
        .G12(8'h01), .G13(8'h02), .G14(8'h03), .G15(8'h00)
    ) u_sr_baked (
        .clk        (clk),
        .rst_n      (rst_n),
        .xbus_adr   (xbus_adr),
        .xbus_dat_w (xbus_dat_w),
        .xbus_sel   (xbus_sel),
        .xbus_we    (xbus_we),
        .xbus_stb   (xbus_stb),
        .xbus_cyc   (xbus_cyc),
        .xbus_dat_r (xbus_dat_r),
        .xbus_ack   (xbus_ack),
        .xbus_err   (xbus_err),
        .dbg_leds   (dbg_leds)
    );
endmodule

