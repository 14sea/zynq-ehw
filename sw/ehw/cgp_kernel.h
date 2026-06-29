#ifndef CGP_KERNEL_H
#define CGP_KERNEL_H

#include <stdint.h>

#define CGP_ROWS 4
#define CGP_COLS 3
#define CGP_NODES (CGP_ROWS * CGP_COLS)
#define CGP_GENOME_LEN CGP_NODES
#define CGP_FITNESS_MAX 64

static const uint16_t CGP_PASS[4] = {0xAAAAu, 0xCCCCu, 0xF0F0u, 0xFF00u};

static const uint16_t CGP_GOLDEN_GENOME[CGP_GENOME_LEN] = {
    0xAAAAu, 0xCCCCu, 0xF0F0u, 0xFF00u,
    0xAAAAu, 0xCCCCu, 0xF0F0u, 0xFF00u,
    0xA0A0u, 0x6AC0u, 0x4C00u, 0x8000u,
};

static inline uint8_t cgp_lut_eval(uint16_t init, const uint8_t in[4]) {
    uint8_t idx = in[0] | (uint8_t)(in[1] << 1) | (uint8_t)(in[2] << 2) | (uint8_t)(in[3] << 3);
    return (uint8_t)((init >> idx) & 1u);
}

static inline uint8_t cgp_gold_bit(int bit, int idx) {
    int a = (idx & 1) | (((idx >> 1) & 1) << 1);
    int b = ((idx >> 2) & 1) | (((idx >> 3) & 1) << 1);
    return (uint8_t)(((a * b) >> bit) & 1);
}

static inline void cgp_eval_grid(const uint16_t genome[CGP_GENOME_LEN], int idx, uint8_t out[4]) {
    uint8_t sig[4] = {
        (uint8_t)((idx >> 0) & 1),
        (uint8_t)((idx >> 1) & 1),
        (uint8_t)((idx >> 2) & 1),
        (uint8_t)((idx >> 3) & 1),
    };
    for (int col = 0; col < CGP_COLS; col++) {
        uint8_t next[4];
        int base = col * CGP_ROWS;
        for (int row = 0; row < CGP_ROWS; row++) next[row] = cgp_lut_eval(genome[base + row], sig);
        for (int row = 0; row < CGP_ROWS; row++) sig[row] = next[row];
    }
    for (int row = 0; row < CGP_ROWS; row++) out[row] = sig[row];
}

static inline int cgp_evaluate(const uint16_t genome[CGP_GENOME_LEN]) {
    int correct = 0;
    for (int idx = 0; idx < 16; idx++) {
        uint8_t out[4];
        cgp_eval_grid(genome, idx, out);
        for (int bit = 0; bit < 4; bit++) correct += (out[bit] == cgp_gold_bit(bit, idx));
    }
    return correct;
}

static inline int cgp_rows_correct(const uint16_t genome[CGP_GENOME_LEN]) {
    int rows = 0;
    for (int idx = 0; idx < 16; idx++) {
        uint8_t out[4];
        cgp_eval_grid(genome, idx, out);
        int ok = 1;
        for (int bit = 0; bit < 4; bit++) ok &= (out[bit] == cgp_gold_bit(bit, idx));
        rows += ok;
    }
    return rows;
}

static inline int cgp_active_nodes(const uint16_t genome[CGP_GENOME_LEN]) {
    int n = 0;
    for (int i = 0; i < CGP_GENOME_LEN; i++) n += (genome[i] != 0u && genome[i] != 0xFFFFu);
    return n;
}

#endif
