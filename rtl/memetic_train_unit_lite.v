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

    reg signed [31:0] cur_w;
    reg signed [31:0] cur_dw;
    reg signed [31:0] next_w;
    always @(*) begin
        cur_w = 32'sd0;
        cur_dw = 32'sd0;
        if (upd_l1) begin
            case (upd_idx)
                4'd0:  begin cur_w = w1m[0];  cur_dw = dw[0];  end
                4'd1:  begin cur_w = w1m[1];  cur_dw = dw[1];  end
                4'd2:  begin cur_w = w1m[2];  cur_dw = dw[2];  end
                4'd3:  begin cur_w = w1m[3];  cur_dw = dw[3];  end
                4'd4:  begin cur_w = w1m[4];  cur_dw = dw[4];  end
                4'd5:  begin cur_w = w1m[5];  cur_dw = dw[5];  end
                4'd6:  begin cur_w = w1m[6];  cur_dw = dw[6];  end
                4'd7:  begin cur_w = w1m[7];  cur_dw = dw[7];  end
                4'd8:  begin cur_w = w1m[8];  cur_dw = dw[8];  end
                4'd9:  begin cur_w = w1m[9];  cur_dw = dw[9];  end
                4'd10: begin cur_w = w1m[10]; cur_dw = dw[10]; end
                4'd11: begin cur_w = w1m[11]; cur_dw = dw[11]; end
                4'd12: begin cur_w = w1m[12]; cur_dw = dw[12]; end
                4'd13: begin cur_w = w1m[13]; cur_dw = dw[13]; end
                4'd14: begin cur_w = w1m[14]; cur_dw = dw[14]; end
                default: begin cur_w = w1m[15]; cur_dw = dw[15]; end
            endcase
        end else begin
            case (upd_idx[2:0])
                3'd0: begin cur_w = w2m[0]; cur_dw = dw[0]; end
                3'd1: begin cur_w = w2m[1]; cur_dw = dw[1]; end
                3'd2: begin cur_w = w2m[2]; cur_dw = dw[2]; end
                3'd3: begin cur_w = w2m[3]; cur_dw = dw[3]; end
                3'd4: begin cur_w = w2m[4]; cur_dw = dw[4]; end
                3'd5: begin cur_w = w2m[5]; cur_dw = dw[5]; end
                3'd6: begin cur_w = w2m[6]; cur_dw = dw[6]; end
                default: begin cur_w = w2m[7]; cur_dw = dw[7]; end
            endcase
        end
        next_w = update_value(cur_w, cur_dw);
    end

    task write_w2_idx(input [2:0] idx, input signed [31:0] value);
        begin
            case (idx)
                3'd0: w2m[0] <= value;
                3'd1: w2m[1] <= value;
                3'd2: w2m[2] <= value;
                3'd3: w2m[3] <= value;
                3'd4: w2m[4] <= value;
                3'd5: w2m[5] <= value;
                3'd6: w2m[6] <= value;
                default: w2m[7] <= value;
            endcase
        end
    endtask

    task write_w1_idx(input [3:0] idx, input signed [31:0] value);
        begin
            case (idx)
                4'd0:  w1m[0]  <= value;
                4'd1:  w1m[1]  <= value;
                4'd2:  w1m[2]  <= value;
                4'd3:  w1m[3]  <= value;
                4'd4:  w1m[4]  <= value;
                4'd5:  w1m[5]  <= value;
                4'd6:  w1m[6]  <= value;
                4'd7:  w1m[7]  <= value;
                4'd8:  w1m[8]  <= value;
                4'd9:  w1m[9]  <= value;
                4'd10: w1m[10] <= value;
                4'd11: w1m[11] <= value;
                4'd12: w1m[12] <= value;
                4'd13: w1m[13] <= value;
                4'd14: w1m[14] <= value;
                default: w1m[15] <= value;
            endcase
        end
    endtask

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
                    write_w1_idx(upd_idx, next_w);
                    if (upd_idx == 4'd15)
                        upd_active <= 1'b0;
                    else
                        upd_idx <= upd_idx + 4'd1;
                end else begin
                    write_w2_idx(upd_idx[2:0], next_w);
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
