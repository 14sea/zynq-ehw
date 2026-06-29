`timescale 1ns/1ps

module tb_cgp_vrc;
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
    reg [31:0] rd;
    reg [3:0] out_bits;
    reg [3:0] product;

    localparam [15:0] G0  = 16'haaaa;
    localparam [15:0] G1  = 16'hcccc;
    localparam [15:0] G2  = 16'hf0f0;
    localparam [15:0] G3  = 16'hff00;
    localparam [15:0] G4  = 16'haaaa;
    localparam [15:0] G5  = 16'hcccc;
    localparam [15:0] G6  = 16'hf0f0;
    localparam [15:0] G7  = 16'hff00;
    localparam [15:0] G8  = 16'ha0a0;
    localparam [15:0] G9  = 16'h6ac0;
    localparam [15:0] G10 = 16'h4c00;
    localparam [15:0] G11 = 16'h8000;

    always #5 clk = ~clk;

    wb_cgp_vrc dut (
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

    task load_node;
        input integer node;
        input [15:0] init;
        begin
            xwrite(12'h040 + (node * 4), {16'd0, init});
            xread(12'h040 + (node * 4), rd);
            expect_eq(rd, {16'd0, init}, "node readback");
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
        xread(12'h018, rd);
        expect_eq(rd, 32'h0000_0000, "active after reset");

        load_node(0,  G0);
        load_node(1,  G1);
        load_node(2,  G2);
        load_node(3,  G3);
        load_node(4,  G4);
        load_node(5,  G5);
        load_node(6,  G6);
        load_node(7,  G7);
        load_node(8,  G8);
        load_node(9,  G9);
        load_node(10, G10);
        load_node(11, G11);

        xread(12'h010, rd);
        expect_eq(rd, 32'd64, "golden fitness");
        xread(12'h014, rd);
        expect_eq(rd, 32'd16, "golden rows");
        xread(12'h018, rd);
        expect_eq(rd, 32'd12, "golden active nodes");

        for (idx = 0; idx < 16; idx = idx + 1) begin
            xwrite(12'h008, idx[3:0]);
            xread(12'h00c, rd);
            out_bits = rd[3:0];
            product = gold_product(idx[3:0]);
            for (bitn = 0; bitn < 4; bitn = bitn + 1) begin
                if (out_bits[bitn] !== product[bitn]) begin
                    $display("FAIL: row idx=%0d out=%x gold=%x", idx, out_bits, product);
                    errors = errors + 1;
                end
            end
        end

        xwrite(12'h000, 32'h0000_0010);
        xread(12'h018, rd);
        expect_eq(rd, 32'd0, "active after clear");
        xread(12'h014, rd);
        if (rd == 32'd16) begin
            $display("FAIL: cleared genome still has 16 correct rows");
            errors = errors + 1;
        end

        if (xbus_err !== 1'b0) begin
            $display("FAIL: xbus_err asserted");
            errors = errors + 1;
        end

        if (errors == 0) begin
            $display("PASS: cgp_vrc golden genome computes 16/16 rows through xbus wrapper");
            $finish;
        end
        $display("FAIL: %0d errors", errors);
        $finish;
    end
endmodule
