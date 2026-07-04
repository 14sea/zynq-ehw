`timescale 1ns/1ps

// DFX Reconfigurable Module for EHW-5.2 host prep.
//
// Combines the EHW-3 spare-route VRC feature island with the EHW-4 memetic
// train unit in one RP wrapper.  The existing 4x4 INT8 array is still forwarded
// outside the claimed local windows.
//
// Byte-offset map inside the 0xF0000000 XBUS peripheral window:
//   0x000..0x3ff  existing 4x4 array / TPU accelerator
//   0x400..0x47f  spare-route VRC feature island
//   0x800..0x934  memetic_train_unit_lite, same register map plus BUSY word

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
    wire [11:0] off       = xbus_adr[11:0];
    wire        sr_claim  = (off >= 12'h400) && (off <= 12'h47f);
    wire        tu_claim  = (off >= 12'h800) && (off <= 12'h934);
    wire        any_claim = sr_claim || tu_claim;
    wire        wr_cyc    = xbus_cyc && xbus_stb;

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
        .xbus_stb   (xbus_stb && !any_claim),
        .xbus_cyc   (xbus_cyc && !any_claim),
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

    reg sr_pending;
    wire [31:0] sr_rdata;
    wire        sr_ready;
    wire        sr_start = wr_cyc && sr_claim && !sr_pending;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            sr_pending <= 1'b0;
        else if (sr_start)
            sr_pending <= 1'b1;
        else if (sr_pending && sr_ready)
            sr_pending <= 1'b0;
    end

    spare_route_vrc u_sr (
        .clk       (clk),
        .rst_n     (rst_n),
        .sel       (sr_pending),
        .addr      (off - 12'h400),
        .wdata     (xbus_dat_w),
        .wstrb     ((xbus_we && sr_pending) ? xbus_sel : 4'b0000),
        .rdata     (sr_rdata),
        .ready     (sr_ready),
        .debug_led (dbg_leds)
    );

    reg tu_pending;
    reg signed [31:0] tu_rdata_q;
    wire tu_start = wr_cyc && tu_claim && !tu_pending;

    wire        tu_we    = tu_start && xbus_we && (xbus_sel != 4'b0000);
    wire [6:0]  tu_addr  = off[8:2];
    wire signed [31:0] tu_rdata;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tu_pending <= 1'b0;
            tu_rdata_q <= 32'sd0;
        end else begin
            tu_pending <= tu_start;
            if (tu_start)
                tu_rdata_q <= tu_rdata;
        end
    end

    memetic_train_unit_lite u_tu (
        .clk   (clk),
        .rst_n (rst_n),
        .we    (tu_we),
        .addr  (tu_addr),
        .wdata (xbus_dat_w),
        .rdata (tu_rdata)
    );

    assign xbus_ack   = sr_claim ? (sr_pending && sr_ready) :
                        tu_claim ? tu_pending : acc_ack;
    assign xbus_dat_r = sr_claim ? sr_rdata :
                        tu_claim ? tu_rdata_q : acc_dat_r;
    assign xbus_err   = 1'b0;
endmodule
