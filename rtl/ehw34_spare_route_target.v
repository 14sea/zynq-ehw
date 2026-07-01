`timescale 1ns/1ps

// EHW-3.4 live ICAPE2 target: baked spare-routing island behind XBUS.
//
// The routed design starts from the baseline/broken POP=128 genome. Vivado writes
// same-route candidate bitstreams by editing only u_sr_target/u_baked/g* INITs;
// NEORV32 then applies those frame sequences through rtl/xbus_icap.v per eval.

module ehw34_spare_route_target (
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
    output [31:0] q
);
    wire [3:0] dbg_leds;

    wb_spare_route_baked #(
        .MARKER(32'h5352_3334), // "SR34"
        .G0(8'h0a), .G1(8'h08), .G2(8'h01), .G3(8'h0f),
        .G4(8'h32), .G5(8'h01), .G6(8'h04), .G7(8'h00),
        .G8(8'h02), .G9(8'h02), .G10(8'h00), .G11(8'h04),
        .G12(8'h01), .G13(8'h01), .G14(8'h02), .G15(8'h00)
    ) u_sr_target (
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

    assign q = {31'd0, dbg_leds[0]};
endmodule
