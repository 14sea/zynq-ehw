`timescale 1ns/1ps

module tb_spare_route_vrc;
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
    integer row;
    reg [31:0] rd;

    localparam [7:0] NOFAULT0  = 8'h0a;
    localparam [7:0] NOFAULT1  = 8'h08;
    localparam [7:0] NOFAULT2  = 8'h01;
    localparam [7:0] NOFAULT3  = 8'h0f;
    localparam [7:0] NOFAULT4  = 8'h32;
    localparam [7:0] NOFAULT5  = 8'h01;
    localparam [7:0] NOFAULT6  = 8'h04;
    localparam [7:0] NOFAULT7  = 8'h00;
    localparam [7:0] NOFAULT8  = 8'h02;
    localparam [7:0] NOFAULT9  = 8'h02;
    localparam [7:0] NOFAULT10 = 8'h00;
    localparam [7:0] NOFAULT11 = 8'h04;
    localparam [7:0] NOFAULT12 = 8'h01;
    localparam [7:0] NOFAULT13 = 8'h01;
    localparam [7:0] NOFAULT14 = 8'h02;
    localparam [7:0] NOFAULT15 = 8'h00;

    localparam [7:0] REPAIR0  = 8'h0b;
    localparam [7:0] REPAIR1  = 8'h09;
    localparam [7:0] REPAIR2  = 8'h09;
    localparam [7:0] REPAIR3  = 8'h03;
    localparam [7:0] REPAIR4  = 8'hb1;
    localparam [7:0] REPAIR5  = 8'h00;
    localparam [7:0] REPAIR6  = 8'h04;
    localparam [7:0] REPAIR7  = 8'h04;
    localparam [7:0] REPAIR8  = 8'h01;
    localparam [7:0] REPAIR9  = 8'h02;
    localparam [7:0] REPAIR10 = 8'h00;
    localparam [7:0] REPAIR11 = 8'h00;
    localparam [7:0] REPAIR12 = 8'h01;
    localparam [7:0] REPAIR13 = 8'h02;
    localparam [7:0] REPAIR14 = 8'h03;
    localparam [7:0] REPAIR15 = 8'h00;

    always #5 clk = ~clk;

    spare_route_vrc dut (
        .clk        (clk),
        .rst_n      (rst_n),
        .sel        (xbus_cyc && xbus_stb),
        .addr       (xbus_adr[11:0]),
        .wdata      (xbus_dat_w),
        .wstrb      (xbus_we ? xbus_sel : 4'b0000),
        .rdata      (xbus_dat_r),
        .ready      (xbus_ack),
        .debug_led  (dbg_leds)
    );
    assign xbus_err = 1'b0;

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
            @(posedge clk);
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
            @(posedge clk);
            while (!xbus_ack) @(posedge clk);
            #1;
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

    task load_byte;
        input integer idx;
        input [7:0] value;
        begin
            xwrite(12'h040 + (idx * 4), {24'd0, value});
            xread(12'h040 + (idx * 4), rd);
            expect_eq(rd, {24'd0, value}, "genome readback");
        end
    endtask

    task set_fault_none;
        begin
            xwrite(12'h018, 32'd0);
        end
    endtask

    task set_fault_disable_a1;
        begin
            xwrite(12'h018, 32'h0000_0013); // kind=3, node=A1 at bits [6:4]
        end
    endtask

    task set_fault_disable_route_out_in1;
        begin
            xwrite(12'h018, 32'h0000_4104); // kind=4, section=out, route_idx=0, mux=1
        end
    endtask

    task load_nofault_genome;
        begin
            load_byte(0, NOFAULT0);   load_byte(1, NOFAULT1);
            load_byte(2, NOFAULT2);   load_byte(3, NOFAULT3);
            load_byte(4, NOFAULT4);   load_byte(5, NOFAULT5);
            load_byte(6, NOFAULT6);   load_byte(7, NOFAULT7);
            load_byte(8, NOFAULT8);   load_byte(9, NOFAULT9);
            load_byte(10, NOFAULT10); load_byte(11, NOFAULT11);
            load_byte(12, NOFAULT12); load_byte(13, NOFAULT13);
            load_byte(14, NOFAULT14); load_byte(15, NOFAULT15);
        end
    endtask

    task load_repair_genome;
        begin
            load_byte(0, REPAIR0);   load_byte(1, REPAIR1);
            load_byte(2, REPAIR2);   load_byte(3, REPAIR3);
            load_byte(4, REPAIR4);   load_byte(5, REPAIR5);
            load_byte(6, REPAIR6);   load_byte(7, REPAIR7);
            load_byte(8, REPAIR8);   load_byte(9, REPAIR9);
            load_byte(10, REPAIR10); load_byte(11, REPAIR11);
            load_byte(12, REPAIR12); load_byte(13, REPAIR13);
            load_byte(14, REPAIR14); load_byte(15, REPAIR15);
        end
    endtask

    task expect_mask_fit;
        input [7:0] want_mask;
        input [3:0] want_fit;
        input [255:0] what;
        begin
            xread(12'h010, rd);
            expect_eq(rd, {24'd0, want_mask}, what);
            xread(12'h014, rd);
            expect_eq(rd, {28'd0, want_fit}, what);
        end
    endtask

    task sweep_rows;
        input [7:0] want_mask;
        begin
            for (row = 0; row < 8; row = row + 1) begin
                xwrite(12'h008, row[2:0]);
                xread(12'h00c, rd);
                expect_eq(rd, {31'd0, want_mask[row]}, "row output");
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
        expect_eq(rd, 32'h5352_5630, "marker");

        load_nofault_genome();
        set_fault_none();
        expect_mask_fit(8'he8, 4'd8, "no-fault mask/fitness");
        sweep_rows(8'he8);
        xread(12'h01c, rd);
        expect_eq(rd, 32'h0000_0001, "no-fault uses A1 and not AS");

        set_fault_disable_a1();
        xread(12'h018, rd);
        expect_eq(rd, 32'h0000_0013, "fault readback");
        expect_mask_fit(8'hc8, 4'd7, "disable A1 mask/fitness");
        sweep_rows(8'hc8);

        set_fault_disable_route_out_in1();
        expect_mask_fit(8'h20, 4'd5, "disable route O.in1 mask/fitness");

        set_fault_disable_a1();
        load_repair_genome();
        expect_mask_fit(8'he8, 4'd8, "repaired mask/fitness");
        sweep_rows(8'he8);
        xread(12'h01c, rd);
        expect_eq(rd, 32'h0000_0002, "repair uses AS and not A1");

        xwrite(12'h000, 32'h0000_0010);
        xread(12'h010, rd);
        if (rd[7:0] == 8'he8) begin
            $display("FAIL: cleared genome still reports target mask");
            errors = errors + 1;
        end
        xread(12'h018, rd);
        expect_eq(rd, 32'd0, "fault clear");

        if (xbus_err !== 1'b0) begin
            $display("FAIL: xbus_err asserted");
            errors = errors + 1;
        end

        if (errors == 0) begin
            $display("PASS: spare_route_vrc config/fault/sweep recovery checks passed");
            $finish;
        end
        $display("FAIL: %0d errors", errors);
        $fatal(1);
    end
endmodule
