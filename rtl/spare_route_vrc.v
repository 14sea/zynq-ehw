`timescale 1ns/1ps

// spare_route_vrc.v -- EHW-3.2 register-configured spare-routing island.
//
// Frozen 16-byte genome contract, matching sim/oracle_spare_routing.py and
// sw/ehw/spare_route_kernel.h:
//   0..3   logic_init[A0,A1,A2,AS], low nibble valid for C1 LUT4 nodes
//   4      init_out[O], full byte valid for the 3-input LUT8 output node
//   5..12  node_sel[i][m], source select into P=[x0,x1,x2,ZERO,ONE]
//   13..15 out_sel[m], source select into [A0,A1,A2,AS]
//
// LUT decode:
//   C1 node: out = (INIT >> (in1 << 1 | in0)) & 1
//   O node : out = (INIT >> (in2 << 2 | in1 << 1 | in0)) & 1
//
// Validity layer:
//   node_sel >= 5 decodes to ZERO. out_sel >= 4 decodes to A0. Muxes are pure
//   fan-in selectors, so every byte string maps to a legal single-driver circuit.
//
// Register map, byte offsets:
//   0x000 CTRL       W  bit4 clears genome/input/fault registers
//   0x004 STATUS     R  bit0 = ready/done (combinational island, always 1)
//   0x008 INPUT      RW [2:0] = {x2,x1,x0}
//   0x00C OUTPUT     R  bit0 = O(input)
//   0x010 MASK       R  [7:0] truth-table mask under current fault
//   0x014 FITNESS    R  0..8 bit matches against majority target 0xe8
//   0x018 FAULT      RW [2:0] kind, [6:4] node, [9:8] route_section,
//                       [13:10] route_idx, [17:14] route_mux
//   0x01C USES       R  bit0 = O selects A1, bit1 = O selects AS
//   0x020 MARKER     R  "SRV0"
//   0x040+i*4 GENi   RW low8 = genome byte, i=0..15

module spare_route_vrc (
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
    localparam GENOME_LEN   = 16;
    localparam TARGET_MASK  = 8'he8;
    localparam FAULT_NONE   = 3'd0;
    localparam FAULT_STUCK0 = 3'd1;
    localparam FAULT_STUCK1 = 3'd2;
    localparam FAULT_DIS_NODE  = 3'd3;
    localparam FAULT_DIS_ROUTE = 3'd4;

    reg [7:0] genome [0:GENOME_LEN-1];
    reg [2:0] input_idx;
    reg [2:0] fault_kind;
    reg [2:0] fault_node;
    reg [1:0] fault_route_section;
    reg [3:0] fault_route_idx;
    reg [3:0] fault_route_mux;

    wire [9:0] word_addr = addr[11:2];
    wire       wr        = sel && (wstrb != 4'b0000);
    wire       in_genome = (word_addr >= 10'h010) && (word_addr <= 10'h01f);
    wire [3:0] genome_idx = word_addr[3:0];

    integer i;

    function lut2;
        input [7:0] init;
        input       in0;
        input       in1;
        begin
            lut2 = init[{in1, in0}];
        end
    endfunction

    function lut3;
        input [7:0] init;
        input       in0;
        input       in1;
        input       in2;
        begin
            lut3 = init[{in2, in1, in0}];
        end
    endfunction

    function [2:0] decode_node_sel;
        input [7:0] raw;
        begin
            decode_node_sel = (raw < 8'd5) ? raw[2:0] : 3'd3; // ZERO
        end
    endfunction

    function [1:0] decode_out_sel;
        input [7:0] raw;
        begin
            decode_out_sel = (raw < 8'd4) ? raw[1:0] : 2'd0; // A0
        end
    endfunction

    function force_default_route;
        input [1:0] section;
        input [3:0] idx;
        input [3:0] mux;
        begin
            force_default_route = (fault_kind == FAULT_DIS_ROUTE) &&
                                  (fault_route_section == section) &&
                                  (fault_route_idx == idx) &&
                                  (fault_route_mux == mux);
        end
    endfunction

    function pool_bit;
        input [2:0] row;
        input [2:0] sel;
        begin
            case (sel)
                3'd0: pool_bit = row[0];
                3'd1: pool_bit = row[1];
                3'd2: pool_bit = row[2];
                3'd4: pool_bit = 1'b1;
                default: pool_bit = 1'b0;
            endcase
        end
    endfunction

    function c1_node;
        input [1:0] node;
        input [2:0] row;
        reg [2:0] sel0;
        reg [2:0] sel1;
        reg value;
        begin
            sel0 = force_default_route(2'd0, {2'b00, node}, 4'd0) ? 3'd3 : decode_node_sel(genome[5 + 2 * node]);
            sel1 = force_default_route(2'd0, {2'b00, node}, 4'd1) ? 3'd3 : decode_node_sel(genome[6 + 2 * node]);
            value = lut2(genome[node], pool_bit(row, sel0), pool_bit(row, sel1));
            if (fault_node == {1'b0, node}) begin
                if (fault_kind == FAULT_STUCK0 || fault_kind == FAULT_DIS_NODE)
                    value = 1'b0;
                else if (fault_kind == FAULT_STUCK1)
                    value = 1'b1;
            end
            c1_node = value;
        end
    endfunction

    function out_uses_node_fn;
        input [1:0] node;
        begin
            out_uses_node_fn = (decode_out_sel(genome[13]) == node) ||
                               (decode_out_sel(genome[14]) == node) ||
                               (decode_out_sel(genome[15]) == node);
        end
    endfunction

    function eval_row;
        input [2:0] row;
        reg [3:0] nodes;
        reg [1:0] sel0;
        reg [1:0] sel1;
        reg [1:0] sel2;
        reg in0;
        reg in1;
        reg in2;
        begin
            nodes[0] = c1_node(2'd0, row);
            nodes[1] = c1_node(2'd1, row);
            nodes[2] = c1_node(2'd2, row);
            nodes[3] = c1_node(2'd3, row);

            sel0 = force_default_route(2'd1, 4'd0, 4'd0) ? 2'd0 : decode_out_sel(genome[13]);
            sel1 = force_default_route(2'd1, 4'd0, 4'd1) ? 2'd0 : decode_out_sel(genome[14]);
            sel2 = force_default_route(2'd1, 4'd0, 4'd2) ? 2'd0 : decode_out_sel(genome[15]);

            in0 = (fault_kind == FAULT_DIS_NODE && fault_node == {1'b0, sel0}) ? 1'b0 : nodes[sel0];
            in1 = (fault_kind == FAULT_DIS_NODE && fault_node == {1'b0, sel1}) ? 1'b0 : nodes[sel1];
            in2 = (fault_kind == FAULT_DIS_NODE && fault_node == {1'b0, sel2}) ? 1'b0 : nodes[sel2];

            eval_row = lut3(genome[4], in0, in1, in2);
        end
    endfunction

    function [3:0] fitness_count;
        input dummy; // Vivado synth requires >=1 function input
        reg [7:0] diff;
        integer bitn;
        integer count;
        begin
            diff = mask_bits ^ TARGET_MASK;
            count = 0;
            for (bitn = 0; bitn < 8; bitn = bitn + 1)
                if (!diff[bitn])
                    count = count + 1;
            fitness_count = count[3:0];
        end
    endfunction

    function [31:0] pack_fault;
        input dummy; // Vivado synth requires >=1 function input
        begin
            pack_fault = {15'd0, fault_route_mux, fault_route_idx, fault_route_section,
                          fault_node, 1'b0, fault_kind};
        end
    endfunction

    wire out_bit = eval_row(input_idx);
    reg [7:0] mask_bits_r;
    integer mask_row;
    always @(genome[0] or genome[1] or genome[2] or genome[3] or
             genome[4] or genome[5] or genome[6] or genome[7] or
             genome[8] or genome[9] or genome[10] or genome[11] or
             genome[12] or genome[13] or genome[14] or genome[15] or
             fault_kind or fault_node or fault_route_section or
             fault_route_idx or fault_route_mux) begin
        mask_bits_r = 8'd0;
        for (mask_row = 0; mask_row < 8; mask_row = mask_row + 1)
            mask_bits_r[mask_row] = eval_row(mask_row[2:0]);
    end
    wire [7:0] mask_bits = mask_bits_r;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            input_idx <= 3'd0;
            fault_kind <= FAULT_NONE;
            fault_node <= 3'd0;
            fault_route_section <= 2'd0;
            fault_route_idx <= 4'd0;
            fault_route_mux <= 4'd0;
            for (i = 0; i < GENOME_LEN; i = i + 1)
                genome[i] <= 8'd0;
        end else if (wr) begin
            if (word_addr == 10'h000 && wdata[4]) begin
                input_idx <= 3'd0;
                fault_kind <= FAULT_NONE;
                fault_node <= 3'd0;
                fault_route_section <= 2'd0;
                fault_route_idx <= 4'd0;
                fault_route_mux <= 4'd0;
                for (i = 0; i < GENOME_LEN; i = i + 1)
                    genome[i] <= 8'd0;
            end else if (word_addr == 10'h002) begin
                input_idx <= wdata[2:0];
            end else if (word_addr == 10'h006) begin
                fault_kind <= wdata[2:0];
                fault_node <= wdata[6:4];
                fault_route_section <= wdata[9:8];
                fault_route_idx <= wdata[13:10];
                fault_route_mux <= wdata[17:14];
            end else if (in_genome) begin
                genome[genome_idx] <= wdata[7:0];
            end
        end
    end

    always @(*) begin
        if (in_genome) begin
            rdata = {24'd0, genome[genome_idx]};
        end else begin
            case (word_addr)
                10'h001: rdata = 32'h0000_0001;
                10'h002: rdata = {29'd0, input_idx};
                10'h003: rdata = {31'd0, out_bit};
                10'h004: rdata = {24'd0, mask_bits};
                10'h005: rdata = {28'd0, fitness_count(1'b0)};
                10'h006: rdata = pack_fault(1'b0);
                10'h007: rdata = {30'd0, out_uses_node_fn(2'd3), out_uses_node_fn(2'd1)};
                10'h008: rdata = 32'h5352_5630; // "SRV0"
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

    assign debug_led = {mask_bits[2:0], out_bit};
endmodule

module wb_spare_route_vrc (
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

    spare_route_vrc u_vrc (
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
