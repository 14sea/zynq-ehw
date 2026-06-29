`timescale 1ns/1ps

module tb_cgp_baked;
    reg clk = 1'b0;
    reg rst_n = 1'b0;

    reg  [31:0] xbus_adr = 32'd0;
    reg  [31:0] xbus_dat_w = 32'd0;
    reg  [3:0]  xbus_sel = 4'h0;
    reg         xbus_we = 1'b0;
    reg         xbus_stb = 1'b0;
    reg         xbus_cyc = 1'b0;
    wire [31:0] xbus_dat_r;
    wire        xbus_ack;
    wire        xbus_err;
    wire [3:0]  dbg_leds;

    integer errors = 0;
    integer idx;
    integer bitn;
    integer fitness = 0;
    integer rows = 0;
    integer row_ok;
    reg [31:0] rd;
    reg [3:0] out_bits;
    reg [3:0] product;

`ifdef CGP_BAKED_CHAMPION
    localparam [31:0] WANT_MARKER = 32'h4347_5031;
    localparam integer WANT_FITNESS = 64;
    localparam integer WANT_ROWS = 16;
    localparam [15:0] INIT8  = 16'ha0a0;
    localparam [15:0] INIT9  = 16'h6ac0;
    localparam [15:0] INIT10 = 16'h4c00;
    localparam [15:0] INIT11 = 16'h8000;
`else
    localparam [31:0] WANT_MARKER = 32'h4347_5030;
    localparam integer WANT_FITNESS = 50;
    localparam integer WANT_ROWS = 7;
    localparam [15:0] INIT8  = 16'h0000;
    localparam [15:0] INIT9  = 16'h0000;
    localparam [15:0] INIT10 = 16'h0000;
    localparam [15:0] INIT11 = 16'h0000;
`endif

    always #5 clk = ~clk;

    wb_cgp_baked #(
        .MARKER(WANT_MARKER),
        .INIT0(16'haaaa), .INIT1(16'hcccc), .INIT2(16'hf0f0), .INIT3(16'hff00),
        .INIT4(16'haaaa), .INIT5(16'hcccc), .INIT6(16'hf0f0), .INIT7(16'hff00),
        .INIT8(INIT8), .INIT9(INIT9), .INIT10(INIT10), .INIT11(INIT11)
    ) dut (
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

    task xwrite;
        input [11:0] off;
        input [31:0] data;
        begin
            @(negedge clk);
            xbus_adr   = {20'hf0000, off};
            xbus_dat_w = data;
            xbus_sel   = 4'hf;
            xbus_we    = 1'b1;
            xbus_stb   = 1'b1;
            xbus_cyc   = 1'b1;
            while (!xbus_ack) @(posedge clk);
            @(negedge clk);
            xbus_sel   = 4'h0;
            xbus_we    = 1'b0;
            xbus_stb   = 1'b0;
            xbus_cyc   = 1'b0;
        end
    endtask

    task xread;
        input [11:0] off;
        output [31:0] data;
        begin
            @(negedge clk);
            xbus_adr   = {20'hf0000, off};
            xbus_dat_w = 32'd0;
            xbus_sel   = 4'hf;
            xbus_we    = 1'b0;
            xbus_stb   = 1'b1;
            xbus_cyc   = 1'b1;
            while (!xbus_ack) @(posedge clk);
            data = xbus_dat_r;
            @(negedge clk);
            xbus_sel   = 4'h0;
            xbus_stb   = 1'b0;
            xbus_cyc   = 1'b0;
        end
    endtask

    task expect_eq;
        input [31:0] got;
        input [31:0] want;
        input [255:0] what;
        begin
            if (got !== want) begin
                $display("FAIL: %0s got=%08x want=%08x", what, got, want);
                errors = errors + 1;
            end
        end
    endtask

    function [3:0] gold_product;
        input [3:0] truth_idx;
        reg [1:0] a;
        reg [1:0] b;
        begin
            a = {truth_idx[1], truth_idx[0]};
            b = {truth_idx[3], truth_idx[2]};
            gold_product = a * b;
        end
    endfunction

    initial begin
        repeat (4) @(posedge clk);
        rst_n = 1'b1;
        repeat (2) @(posedge clk);

        xread(12'h004, rd);
        expect_eq(rd, 32'h0000_0001, "status");
        xread(12'h020, rd);
        expect_eq(rd, WANT_MARKER, "marker");

        for (idx = 0; idx < 16; idx = idx + 1) begin
            xwrite(12'h008, idx[3:0]);
            xread(12'h00c, rd);
            out_bits = rd[3:0];
            product = gold_product(idx[3:0]);
            row_ok = 1;
            for (bitn = 0; bitn < 4; bitn = bitn + 1) begin
                if (out_bits[bitn] === product[bitn])
                    fitness = fitness + 1;
                else
                    row_ok = 0;
            end
            if (row_ok)
                rows = rows + 1;
        end

        if (fitness != WANT_FITNESS) begin
            $display("FAIL: fitness got=%0d want=%0d", fitness, WANT_FITNESS);
            errors = errors + 1;
        end
        if (rows != WANT_ROWS) begin
            $display("FAIL: rows got=%0d want=%0d", rows, WANT_ROWS);
            errors = errors + 1;
        end
        if (xbus_err !== 1'b0) begin
            $display("FAIL: xbus_err asserted");
            errors = errors + 1;
        end

        if (errors == 0) begin
            $display("PASS: cgp_baked marker=%08x rows=%0d/16 fitness=%0d/64", WANT_MARKER, rows, fitness);
            $finish;
        end
        $display("FAIL: %0d errors", errors);
        $finish;
    end
endmodule
