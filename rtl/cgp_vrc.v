`timescale 1ns/1ps

// cgp_vrc.v -- register-configured CGP VRC for EHW-1.1-fabric.
//
// A small fixed-routing 3x4 LUT4 grid. Firmware writes twelve 16-bit LUT INIT
// words into registers, writes one 4-bit input vector, and reads the 4-bit output.
// Routing is fixed column-to-column, so evolution changes LUT truth tables only.
//
// Register map, byte offsets:
//   0x000 CTRL       W  bit4 clears genome/input registers
//   0x004 STATUS     R  bit0 = ready/done (combinational grid, always 1)
//   0x008 INPUT      RW [3:0] = {b1,b0,a1,a0} as idx bits [3:0]
//   0x00C OUTPUT     R  [3:0] = {p3,p2,p1,p0}
//   0x010 FITNESS    R  0..64 bit matches against 2-bit multiplier truth table
//   0x014 ROWS       R  0..16 fully-correct truth-table rows
//   0x018 ACTIVE     R  non-0/non-FFFF LUT INIT count
//   0x040+i*4 INITi  RW low16 = LUT INIT word, i=0..11

module cgp_vrc (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        sel,
    input  wire [11:0] addr,
    input  wire [31:0] wdata,
    input  wire [ 3:0] wstrb,
    output reg  [31:0] rdata,
    output wire        ready,
    output wire [3:0]  debug_led
);
    localparam ROWS  = 4;
    localparam COLS  = 3;
    localparam NODES = ROWS * COLS;

    reg [15:0] init [0:NODES-1];
    reg [3:0]  input_idx;

    wire [9:0] word_addr = addr[11:2];
    wire       wr        = sel && (wstrb != 4'b0000);
    wire       in_init   = (word_addr >= 10'h010) && (word_addr <= 10'h01B);
    wire [3:0] init_idx  = word_addr[3:0] - 4'h0;

    integer i;

    function lut_bit;
        input [15:0] lut_init;
        input [3:0]  lut_idx;
        begin
            lut_bit = lut_init[lut_idx];
        end
    endfunction

    function [3:0] eval_grid;
        input [3:0] idx;
        reg [3:0] sig;
        reg [3:0] next_sig;
        reg [3:0] lut_idx;
        integer col;
        integer row;
        integer base;
        begin
            sig = idx;
            for (col = 0; col < COLS; col = col + 1) begin
                base = col * ROWS;
                lut_idx = sig;
                for (row = 0; row < ROWS; row = row + 1)
                    next_sig[row] = lut_bit(init[base + row], lut_idx);
                sig = next_sig;
            end
            eval_grid = sig;
        end
    endfunction

    function gold_bit;
        input integer bitn;
        input [3:0] idx;
        reg [1:0] a;
        reg [1:0] b;
        reg [3:0] product;
        begin
            a = {idx[1], idx[0]};
            b = {idx[3], idx[2]};
            product = a * b;
            gold_bit = product[bitn];
        end
    endfunction

    function [6:0] fitness_count;
        reg [3:0] out;
        integer idx;
        integer bitn;
        integer count;
        begin
            count = 0;
            for (idx = 0; idx < 16; idx = idx + 1) begin
                out = eval_grid(idx[3:0]);
                for (bitn = 0; bitn < 4; bitn = bitn + 1)
                    if (out[bitn] == gold_bit(bitn, idx[3:0]))
                        count = count + 1;
            end
            fitness_count = count[6:0];
        end
    endfunction

    function [4:0] rows_count;
        reg [3:0] out;
        integer idx;
        integer bitn;
        integer ok;
        integer rows;
        begin
            rows = 0;
            for (idx = 0; idx < 16; idx = idx + 1) begin
                out = eval_grid(idx[3:0]);
                ok = 1;
                for (bitn = 0; bitn < 4; bitn = bitn + 1)
                    if (out[bitn] != gold_bit(bitn, idx[3:0]))
                        ok = 0;
                if (ok)
                    rows = rows + 1;
            end
            rows_count = rows[4:0];
        end
    endfunction

    function [4:0] active_count;
        integer node;
        integer count;
        begin
            count = 0;
            for (node = 0; node < NODES; node = node + 1)
                if (init[node] != 16'h0000 && init[node] != 16'hffff)
                    count = count + 1;
            active_count = count[4:0];
        end
    endfunction

    wire [3:0] out_bits = eval_grid(input_idx);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            input_idx <= 4'd0;
            for (i = 0; i < NODES; i = i + 1)
                init[i] <= 16'h0000;
        end else if (wr) begin
            if (word_addr == 10'h000 && wdata[4]) begin
                input_idx <= 4'd0;
                for (i = 0; i < NODES; i = i + 1)
                    init[i] <= 16'h0000;
            end else if (word_addr == 10'h002) begin
                input_idx <= wdata[3:0];
            end else if (in_init) begin
                init[init_idx] <= wdata[15:0];
            end
        end
    end

    always @(*) begin
        if (in_init) begin
            rdata = {16'd0, init[init_idx]};
        end else begin
            case (word_addr)
                10'h001: rdata = 32'h0000_0001;
                10'h002: rdata = {28'd0, input_idx};
                10'h003: rdata = {28'd0, out_bits};
                10'h004: rdata = {25'd0, fitness_count()};
                10'h005: rdata = {27'd0, rows_count()};
                10'h006: rdata = {27'd0, active_count()};
                default: rdata = 32'd0;
            endcase
        end
    end

    reg ready_r;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n)
            ready_r <= 1'b0;
        else
            ready_r <= sel && !ready_r;
    end
    assign ready = ready_r;

    assign debug_led = out_bits;
endmodule

module wb_cgp_vrc (
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
    wire [31:0] vrc_rdata;
    wire        vrc_ready;
    reg         pending;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pending    <= 1'b0;
            xbus_ack   <= 1'b0;
            xbus_dat_r <= 32'd0;
        end else begin
            xbus_ack <= 1'b0;
            if (xbus_cyc && xbus_stb && !pending)
                pending <= 1'b1;
            if (pending && vrc_ready) begin
                xbus_dat_r <= vrc_rdata;
                xbus_ack   <= 1'b1;
                pending    <= 1'b0;
            end
        end
    end

    assign xbus_err = 1'b0;

    cgp_vrc u_vrc (
        .clk       (clk),
        .rst_n     (rst_n),
        .sel       (pending),
        .addr      (xbus_adr[11:0]),
        .wdata     (xbus_dat_w),
        .wstrb     (xbus_we ? xbus_sel : 4'b0000),
        .rdata     (vrc_rdata),
        .ready     (vrc_ready),
        .debug_led (dbg_leds)
    );
endmodule
