`timescale 1ns/1ps

module tb_spare_route_baked;
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
    integer fitness = 0;
    reg [7:0] mask = 8'd0;
    reg [31:0] rd;
    localparam [7:0] TARGET_MASK = 8'he8;

`ifdef SR_BAKED_REPAIR
    localparam [31:0] WANT_MARKER = 32'h5352_4231;
    localparam [7:0] WANT_MASK = 8'he8;
    localparam integer WANT_FITNESS = 8;
    localparam [7:0] G0  = 8'h0b, G1  = 8'h09, G2  = 8'h09, G3  = 8'h03;
    localparam [7:0] G4  = 8'hb1, G5  = 8'h00, G6  = 8'h04, G7  = 8'h04;
    localparam [7:0] G8  = 8'h01, G9  = 8'h02, G10 = 8'h00, G11 = 8'h00;
    localparam [7:0] G12 = 8'h01, G13 = 8'h02, G14 = 8'h03, G15 = 8'h00;
`else
    localparam [31:0] WANT_MARKER = 32'h5352_4230;
    localparam [7:0] WANT_MASK = 8'hc8;
    localparam integer WANT_FITNESS = 7;
    localparam [7:0] G0  = 8'h0a, G1  = 8'h08, G2  = 8'h01, G3  = 8'h0f;
    localparam [7:0] G4  = 8'h32, G5  = 8'h01, G6  = 8'h04, G7  = 8'h00;
    localparam [7:0] G8  = 8'h02, G9  = 8'h02, G10 = 8'h00, G11 = 8'h04;
    localparam [7:0] G12 = 8'h01, G13 = 8'h01, G14 = 8'h02, G15 = 8'h00;
`endif

    always #5 clk = ~clk;

    wb_spare_route_baked #(
        .MARKER(WANT_MARKER),
        .G0(G0), .G1(G1), .G2(G2), .G3(G3), .G4(G4), .G5(G5), .G6(G6), .G7(G7),
        .G8(G8), .G9(G9), .G10(G10), .G11(G11), .G12(G12), .G13(G13), .G14(G14), .G15(G15)
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

    initial begin
        repeat (4) @(posedge clk);
        rst_n = 1'b1;
        repeat (2) @(posedge clk);

        xread(12'h004, rd);
        expect_eq(rd, 32'h0000_0001, "status");
        xread(12'h020, rd);
        expect_eq(rd, WANT_MARKER, "marker");

        for (idx = 0; idx < 8; idx = idx + 1) begin
            xwrite(12'h008, idx[2:0]);
            xread(12'h00c, rd);
            mask[idx] = rd[0];
        end
        for (idx = 0; idx < 8; idx = idx + 1)
            fitness = fitness + (mask[idx] == TARGET_MASK[idx]);

        if (mask != WANT_MASK) begin
            $display("FAIL: mask got=%02x want=%02x", mask, WANT_MASK);
            errors = errors + 1;
        end
        if (fitness != WANT_FITNESS) begin
            $display("FAIL: fitness got=%0d want=%0d", fitness, WANT_FITNESS);
            errors = errors + 1;
        end
        if (xbus_err !== 1'b0) begin
            $display("FAIL: xbus_err asserted");
            errors = errors + 1;
        end

        if (errors == 0) begin
            $display("PASS: spare_route_baked marker=%08x mask=%02x fitness=%0d/8", WANT_MARKER, mask, fitness);
            $finish;
        end
        $display("FAIL: %0d errors", errors);
        $fatal(1);
    end
endmodule
