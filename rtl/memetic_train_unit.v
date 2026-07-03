// memetic_train_unit.v -- EHW-4.2 train-unit prep for the 4->4->2 memetic net.
//
// This is the EHW-local adaptation of zynq_xpart's M7.2 train_unit idea.  The
// arithmetic contract is the EHW-4.1 memetic kernel, not the original 2->4->1 XOR
// contract:
//   qmul(a,b) = (a*b + 128) >>> 8, FRAC=8
//   leaky'(z) = 256 when z>=0, otherwise 64 (K=2)
//   err clamp = [-2^20, 2^20-1], delta clamp = [-2^14, 2^14-1]
//   master update = sat16(master - (dw >>> LR_SHIFT))
//
// The unit owns the 24 Q8.8 master weights of the EHW-0 24-byte genome:
// W1[4][4] at word addresses 32..47 and W2[2][4] at 48..55.  Biases remain fixed
// constants in firmware/oracle and are deliberately not part of the genome.
//
// Word-address map:
//    0.. 3 INA0-3   y[0..1] for loss/d2, or w2td2[0..3] for d1
//    4.. 7 Z0-3     z2[0..1] for loss/d2, or z1[0..3] for d1
//    8..11 T0-3     target lanes; only T0/T1 used by the 2-output net
//   12..27 DW0-15   gradient bank; DW0..7 for W2, DW0..15 for W1
//   28      CMD      [0]=loss_d2 [1]=d1 [2]=upd_l2 [3]=upd_l1 [4]=clr_loss
//   32..47 W1M0-15  Q8.8 master W1, row-major
//   48..55 W2M0-7   Q8.8 master W2, row-major
//   64..65 D2_0-1   output-layer delta
//   68..71 D1_0-3   hidden-layer delta
//   76      LOSS     accumulated SSE for the current epoch

`timescale 1ns/1ps

module memetic_train_unit (
    input                    clk,
    input                    rst_n,
    input        [5:0]       lr,
    input        [5:0]       k,
    input                    we,
    input        [6:0]       addr,
    input signed [31:0]      wdata,
    output reg signed [31:0] rdata
);
    localparam [6:0] A_CMD  = 7'd28;
    localparam [6:0] A_LOSS = 7'd76;

    reg signed [31:0] ina  [0:3];
    reg signed [31:0] zin  [0:3];
    reg signed [31:0] tin  [0:3];
    reg signed [31:0] dw   [0:15];
    reg signed [31:0] w1m  [0:15];
    reg signed [31:0] w2m  [0:7];
    reg signed [31:0] d2r  [0:1];
    reg signed [31:0] d1r  [0:3];
    reg signed [31:0] loss;

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

    // leaky'(z) is either 1.0 or 2^-k.  Do not implement this as a generic qmul:
    // Vivado infers many DSP48s when the combinational d1/d2 lanes are unrolled.
    // For the power-of-two negative slope, qmul(x, 256>>k) is exactly a rounding
    // arithmetic shift: (x + 2^(k-1)) >>> k.
    function signed [31:0] leaky_apply(input signed [31:0] x, input signed [31:0] z);
        begin
            leaky_apply = (z >= 0) ? x : (($signed(x) + (32'sd1 <<< (k - 6'd1))) >>> k);
        end
    endfunction

    // Loss is the only intentional multiplier in this unit.  The input is clamped
    // to 22 signed bits; Vivado should map the two output-lane squares to the small
    // DSP budget left by the 16-DSP array.
    function signed [31:0] qmul_sq(input signed [21:0] a);
        reg signed [43:0] p;
        begin
            p = a * a;
            qmul_sq = (p + 44'sd128) >>> 8;
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
        end else if (we) begin
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
                if (wdata[2]) begin
                    for (i = 0; i < 8; i = i + 1)
                        w2m[i] <= sat16($signed(w2m[i]) - ($signed(dw[i]) >>> lr));
                end
                if (wdata[3]) begin
                    for (i = 0; i < 16; i = i + 1)
                        w1m[i] <= sat16($signed(w1m[i]) - ($signed(dw[i]) >>> lr));
                end
                if (wdata[4]) loss <= 0;
            end
        end
    end

    always @(*) begin
        if      (addr >= 7'd32 && addr <= 7'd47) rdata = w1m[addr - 7'd32];
        else if (addr >= 7'd48 && addr <= 7'd55) rdata = w2m[addr - 7'd48];
        else if (addr >= 7'd64 && addr <= 7'd65) rdata = d2r[addr - 7'd64];
        else if (addr >= 7'd68 && addr <= 7'd71) rdata = d1r[addr - 7'd68];
        else if (addr == A_LOSS)                 rdata = loss;
        else                                     rdata = 32'd0;
    end
endmodule
