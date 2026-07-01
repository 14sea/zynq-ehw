`timescale 1ns/1ps

module tb_ehw34_spare_route_target;
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
    wire [31:0] q;

    integer errors = 0;
    integer idx;
    integer fitness = 0;
    reg [7:0] mask = 8'd0;
    reg [31:0] rd;
    localparam [7:0] TARGET_MASK = 8'he8;

    always #5 clk = ~clk;

    ehw34_spare_route_target dut (
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
        .q          (q)
    );

    task xwrite;
        input [11:0] off;
        input [31:0] data;
        begin
            @(negedge clk);
            xbus_adr   = {20'hf4000, off};
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
            xbus_adr   = {20'hf4000, off};
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

    initial begin
        repeat (4) @(posedge clk);
        rst_n = 1'b1;
        repeat (2) @(posedge clk);

        xread(12'h004, rd);
        expect_eq(rd, 32'h0000_0001, "status");
        xread(12'h020, rd);
        expect_eq(rd, 32'h5352_3334, "marker");

        for (idx = 0; idx < 8; idx = idx + 1) begin
            xwrite(12'h008, idx[2:0]);
            xread(12'h00c, rd);
            mask[idx] = rd[0];
            expect_eq(q, {31'd0, rd[0]}, "q mirrors output");
        end
        for (idx = 0; idx < 8; idx = idx + 1)
            fitness = fitness + (mask[idx] == TARGET_MASK[idx]);

        if (mask != 8'hc8) begin
            $display("FAIL: mask got=%02x want=c8", mask);
            errors = errors + 1;
        end
        if (fitness != 7) begin
            $display("FAIL: fitness got=%0d want=7", fitness);
            errors = errors + 1;
        end
        if (xbus_err !== 1'b0) begin
            $display("FAIL: xbus_err asserted");
            errors = errors + 1;
        end

        if (errors == 0) begin
            $display("PASS: ehw34_spare_route_target marker=53523334 mask=%02x fitness=%0d/8", mask, fitness);
            $finish;
        end
        $display("FAIL: %0d errors", errors);
        $fatal(1);
    end
endmodule
