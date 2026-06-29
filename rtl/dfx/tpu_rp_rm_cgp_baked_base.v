`timescale 1ns/1ps

// EHW-1.2 baseline baked-CGP RM. The first two columns pass primary inputs
// through; the output column is all-zero. ICAP edits only the final four LUT4
// INITs to reveal the evolved multiplier.

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
    wb_cgp_baked #(
        .MARKER(32'h4347_5030), // CGP0
        .INIT0(16'haaaa), .INIT1(16'hcccc), .INIT2(16'hf0f0), .INIT3(16'hff00),
        .INIT4(16'haaaa), .INIT5(16'hcccc), .INIT6(16'hf0f0), .INIT7(16'hff00),
        .INIT8(16'h0000), .INIT9(16'h0000), .INIT10(16'h0000), .INIT11(16'h0000)
    ) u_cgp (
        .clk(clk), .rst_n(rst_n),
        .xbus_adr(xbus_adr), .xbus_dat_w(xbus_dat_w), .xbus_sel(xbus_sel),
        .xbus_we(xbus_we), .xbus_stb(xbus_stb), .xbus_cyc(xbus_cyc),
        .xbus_dat_r(xbus_dat_r), .xbus_ack(xbus_ack), .xbus_err(xbus_err),
        .dbg_leds(dbg_leds)
    );
endmodule
