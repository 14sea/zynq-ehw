`timescale 1ns/1ps

// DFX Reconfigurable Module for EHW-4.2 board-bound prep.
//
// Keeps the existing tpu_rp XBUS port contract used by neorv32_soc_dfx.  The
// 4x4 INT8 array is forwarded unchanged for future EHW-4.3 forward/transpose
// matmuls; the local window at byte offsets 0x800..0x930 exposes the EHW-4
// memetic_train_unit.

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
    localparam [5:0] TU_LR = 6'd7; // EHW-4.1 MEM_LR_SHIFT
    localparam [5:0] TU_K  = 6'd2; // EHW-0/EHW-4 leaky negative-slope shift

    wire [11:0] off    = xbus_adr[11:0];
    wire        claim  = (off >= 12'h800) && (off <= 12'h930);
    wire        wr_cyc = xbus_cyc && xbus_stb;

    wire [31:0] acc_dat_r;
    wire        acc_ack;
    wire [3:0]  acc_leds;

    wb_tpu_accel u_accel (
        .clk        (clk),
        .rst_n      (rst_n),
        .xbus_adr   (xbus_adr),
        .xbus_dat_w (xbus_dat_w),
        .xbus_sel   (xbus_sel),
        .xbus_we    (xbus_we),
        .xbus_stb   (xbus_stb && !claim),
        .xbus_cyc   (xbus_cyc && !claim),
        .xbus_dat_r (acc_dat_r),
        .xbus_ack   (acc_ack),
        .xbus_err   (),
        .dbg_leds   (acc_leds),
        .res0_o     (),
        .res1_o     (),
        .res2_o     (),
        .res3_o     (),
        .mm_done_o  ()
    );

    reg cpend;
    wire start = wr_cyc && claim && !cpend;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) cpend <= 1'b0;
        else        cpend <= start;
    end

    wire        tu_we    = start && xbus_we && (xbus_sel != 4'b0000);
    wire [6:0]  tu_addr  = off[8:2];
    wire signed [31:0] tu_rdata;

    memetic_train_unit u_tu (
        .clk   (clk),
        .rst_n (rst_n),
        .lr    (TU_LR),
        .k     (TU_K),
        .we    (tu_we),
        .addr  (tu_addr),
        .wdata (xbus_dat_w),
        .rdata (tu_rdata)
    );

    assign xbus_ack   = claim ? cpend    : acc_ack;
    assign xbus_dat_r = claim ? tu_rdata : acc_dat_r;
    assign xbus_err   = 1'b0;
    assign dbg_leds   = acc_leds;
endmodule
