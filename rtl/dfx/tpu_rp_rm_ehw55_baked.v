`timescale 1ns/1ps

// EHW-5.5 no-fault baked spare-route island, MS_SR_MAJORITY baseline.
// Baseline (this RM as built): truth mask 0xe8, feature mask 0xfbc5dabfc7 (28 ones).
// ICAP editing only g0/g1/g7/g8/g12/g14 INITs turns it into the EHW-5.4a arm1
// structural champion: truth mask 0xa0, feature mask 0xd2c1d02a42 (15 ones).
// Marker "SR55" is NOT part of the edit and must stay constant across the reveal.

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
        .NO_FAULT(1'b1),
        .MARKER(32'h5352_3535), // "SR55"
        .G0(8'h0a), .G1(8'h0a), .G2(8'h0a), .G3(8'h00),
        .G4(8'he8), .G5(8'h00), .G6(8'h00), .G7(8'h01),
        .G8(8'h01), .G9(8'h02), .G10(8'h02), .G11(8'h03),
        .G12(8'h03), .G13(8'h00), .G14(8'h01), .G15(8'h02)
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
