// memetic_train_unit_lite.v -- resource-reduced EHW-5 train-unit variant.
//
// Same register map as memetic_train_unit.v for the registers used by firmware,
// but W1/W2 updates are serialized to avoid instantiating 24 parallel saturating
// add/sub lanes in the already tight EHW-5 combined RM.
//
// Fixed EHW constants:
//   LR_SHIFT = 7
//   leaky negative-slope shift K = 2
//
// Additional read-only word:
//   77 BUSY  bit0=serialized weight update in progress

`timescale 1ns/1ps

module memetic_train_unit_lite (
    input                    clk,
    input                    rst_n,
    input                    we,
    input        [6:0]       addr,
    input signed [31:0]      wdata,
    output reg signed [31:0] rdata
);
    localparam [6:0] A_CMD  = 7'd28;
    localparam [6:0] A_LOSS = 7'd76;
    localparam [6:0] A_BUSY = 7'd77;

    reg signed [31:0] ina  [0:3];
    reg signed [31:0] zin  [0:3];
    reg signed [31:0] tin  [0:3];
    reg signed [31:0] dw   [0:15];
    reg signed [31:0] w1m  [0:15];
    reg signed [31:0] w2m  [0:7];
    reg signed [31:0] d2r  [0:1];
    reg signed [31:0] d1r  [0:3];
    reg signed [31:0] loss;

    reg        upd_active;
    reg        upd_l1;
    reg [3:0]  upd_idx;

    integer i;

    function signed [31:0] sat16(input signed [31:0] v);
        begin
            sat16 = (v > 32'sd32767)  ? 32'sd32767  :
                    (v < -32'sd32768) ? -32'sd32768 : v;
        end
    endfunction

    function signed [31:0] clampd(input signed [31:0] v);
        begin
            clampd = (v > 32'sd16383)  ? 32'sd16383  :
                     (v < -32'sd16384) ? -32'sd16384 : v;
        end
    endfunction

    function signed [31:0] clamperr(input signed [31:0] v);
        begin
            clamperr = (v > 32'sd1048575)  ? 32'sd1048575  :
                       (v < -32'sd1048576) ? -32'sd1048576 : v;
        end
    endfunction

    function signed [31:0] leaky_apply(input signed [31:0] x, input signed [31:0] z);
        begin
            leaky_apply = (z >= 0) ? x : (($signed(x) + 32'sd2) >>> 2);
        end
    endfunction

    function signed [31:0] qmul_sq(input signed [21:0] a);
        reg signed [43:0] p;
        begin
            p = a * a;
            qmul_sq = (p + 44'sd128) >>> 8;
        end
    endfunction

    function signed [31:0] update_value(input signed [31:0] w, input signed [31:0] grad);
        begin
            update_value = sat16($signed(w) - ($signed(grad) >>> 7));
        end
    endfunction

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            for (i = 0; i < 4; i = i + 1) begin
                ina[i] <= 0;
                zin[i] <= 0;
                tin[i] <= 0;
                d1r[i] <= 0;
            end
            for (i = 0; i < 16; i = i + 1) begin
                dw[i] <= 0;
                w1m[i] <= 0;
            end
            for (i = 0; i < 8; i = i + 1) w2m[i] <= 0;
            for (i = 0; i < 2; i = i + 1) d2r[i] <= 0;
            loss <= 0;
            upd_active <= 1'b0;
            upd_l1 <= 1'b0;
            upd_idx <= 4'd0;
        end else begin
            if (upd_active) begin
                if (upd_l1) begin
                    w1m[upd_idx] <= update_value(w1m[upd_idx], dw[upd_idx]);
                    if (upd_idx == 4'd15)
                        upd_active <= 1'b0;
                    else
                        upd_idx <= upd_idx + 4'd1;
                end else begin
                    w2m[upd_idx] <= update_value(w2m[upd_idx], dw[upd_idx]);
                    if (upd_idx == 4'd7)
                        upd_active <= 1'b0;
                    else
                        upd_idx <= upd_idx + 4'd1;
                end
            end

            if (we) begin
                if (addr <= 7'd3)                         ina[addr[1:0]]        <= wdata;
                else if (addr >= 7'd4  && addr <= 7'd7)   zin[addr[1:0]]        <= wdata;
                else if (addr >= 7'd8  && addr <= 7'd11)  tin[addr[1:0]]        <= wdata;
                else if (addr >= 7'd12 && addr <= 7'd27)  dw[addr - 7'd12]      <= wdata;
                else if (addr >= 7'd32 && addr <= 7'd47)  w1m[addr - 7'd32]     <= wdata;
                else if (addr >= 7'd48 && addr <= 7'd55)  w2m[addr - 7'd48]     <= wdata;

                if (addr == A_CMD) begin
                    if (wdata[0]) begin
                        loss <= loss
                                + qmul_sq(clamperr($signed(ina[0]) - $signed(tin[0])))
                                + qmul_sq(clamperr($signed(ina[1]) - $signed(tin[1])));
                        for (i = 0; i < 2; i = i + 1)
                            d2r[i] <= clampd(leaky_apply(clamperr($signed(ina[i]) - $signed(tin[i])),
                                                         zin[i]));
                    end
                    if (wdata[1]) begin
                        for (i = 0; i < 4; i = i + 1)
                            d1r[i] <= clampd(leaky_apply($signed(ina[i]), zin[i]));
                    end
                    if (!upd_active && wdata[2]) begin
                        upd_active <= 1'b1;
                        upd_l1 <= 1'b0;
                        upd_idx <= 4'd0;
                    end
                    if (!upd_active && wdata[3]) begin
                        upd_active <= 1'b1;
                        upd_l1 <= 1'b1;
                        upd_idx <= 4'd0;
                    end
                    if (wdata[4]) loss <= 0;
                end
            end
        end
    end

    always @(*) begin
        if      (addr >= 7'd32 && addr <= 7'd47) rdata = w1m[addr - 7'd32];
        else if (addr >= 7'd48 && addr <= 7'd55) rdata = w2m[addr - 7'd48];
        else if (addr >= 7'd64 && addr <= 7'd65) rdata = d2r[addr - 7'd64];
        else if (addr >= 7'd68 && addr <= 7'd71) rdata = d1r[addr - 7'd68];
        else if (addr == A_LOSS)                 rdata = loss;
        else if (addr == A_BUSY)                 rdata = {31'd0, upd_active};
        else                                     rdata = 32'd0;
    end
endmodule
