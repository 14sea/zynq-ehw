/* EHW-5.2 firmware prep: spare-route VRC feature + memetic train-unit smoke.
 *
 * This is the board-bound protocol proof for the first EHW-5 combined RM.  It is
 * not the full GA loop yet.  Firmware loads a 16-byte spare-route genome into the
 * VRC window, uses the live feature output to transform each sample, and runs one
 * fixed-point SGD adaptation epoch through the EHW-4 train-unit window.
 *
 * Host mode (-DMEMETIC_STRUCT_TRAIN_HOST_STUB) models the same MMIO protocol and
 * compares against a CPU golden using the same frozen EHW-5.1 contract.
 */

#include "memetic_struct_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef MEMETIC_STRUCT_TRAIN_HOST_STUB
#include <stdio.h>
static uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#else
#include <neorv32.h>
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define SR_BASE 0xF0000400U
#define TU_BASE 0xF0000800U

#define SR_INPUT       2u
#define SR_OUTPUT      3u
#define SR_MASK        4u
#define SR_MARKER      8u
#define SR_GEN(n)      (16u + (uint32_t)(n))

#define TU_INA(n)      (0u + (uint32_t)(n))
#define TU_Z(n)        (4u + (uint32_t)(n))
#define TU_T(n)        (8u + (uint32_t)(n))
#define TU_DW(n)       (12u + (uint32_t)(n))
#define TU_CMD         28u
#define TU_W1(n)       (32u + (uint32_t)(n))
#define TU_W2(n)       (48u + (uint32_t)(n))
#define TU_D2(n)       (64u + (uint32_t)(n))
#define TU_D1(n)       (68u + (uint32_t)(n))
#define TU_LOSS        76u
#define TU_BUSY        77u

#define TU_CMD_LOSS_D2 0x01u
#define TU_CMD_D1      0x02u
#define TU_CMD_UPD_L2  0x04u
#define TU_CMD_UPD_L1  0x08u
#define TU_CMD_CLR     0x10u

static const uint8_t SR_FEATURE_GENOME[SR_GENOME_LEN] = {
    0x08, 0x00, 0x0a, 0x00, 0xe8, 0, 0, 3, 3, 2, 2, 3, 1, 0, 3, 2
};
static const uint32_t SR_FEATURE_TRUTH_MASK = 0xa0u;

#ifdef MEMETIC_STRUCT_TRAIN_HOST_STUB
static uint8_t sr_genome_model[SR_GENOME_LEN];
static int32_t tu_ina[4], tu_z[4], tu_t[4], tu_dw[16], tu_w1[16], tu_w2[8], tu_d2_reg[2], tu_d1_reg[4], tu_loss;

static int32_t tu_qmul(int32_t a, int32_t b) {
    return (int32_t)((((int64_t)a * (int64_t)b) + (1 << (MEM_FRAC - 1))) >> MEM_FRAC);
}

static int32_t tu_leaky_apply(int32_t x, int32_t z) {
    return tu_qmul(x, z >= 0 ? MEM_ONE : (MEM_ONE >> EHW_K));
}

static void tu_model_cmd(uint32_t cmd) {
    if (cmd & TU_CMD_LOSS_D2) {
        for (int i = 0; i < EHW_NOUT; i++) {
            int32_t e = mem_clamp32(tu_ina[i] - tu_t[i], -MEM_ERR_CLAMP, MEM_ERR_CLAMP - 1);
            tu_loss += tu_qmul(e, e);
            tu_d2_reg[i] = mem_clamp32(tu_leaky_apply(e, tu_z[i]), -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        }
    }
    if (cmd & TU_CMD_D1) {
        for (int i = 0; i < EHW_NH; i++) {
            tu_d1_reg[i] = mem_clamp32(tu_leaky_apply(tu_ina[i], tu_z[i]),
                                       -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        }
    }
    if (cmd & TU_CMD_UPD_L2) {
        for (int i = 0; i < EHW_NOUT * EHW_NH; i++)
            tu_w2[i] = mem_sat16(tu_w2[i] - (tu_dw[i] >> MEM_LR_SHIFT));
    }
    if (cmd & TU_CMD_UPD_L1) {
        for (int i = 0; i < EHW_NH * EHW_NIN; i++)
            tu_w1[i] = mem_sat16(tu_w1[i] - (tu_dw[i] >> MEM_LR_SHIFT));
    }
    if (cmd & TU_CMD_CLR) tu_loss = 0;
}

static void sr_write(uint32_t word, uint32_t value) {
    if (word >= SR_GEN(0) && word <= SR_GEN(15)) sr_genome_model[word - SR_GEN(0)] = (uint8_t)value;
}

static uint32_t sr_read(uint32_t word) {
    if (word == SR_MARKER) return 0x53525630u;
    if (word == SR_MASK) return sr_truth_mask(sr_genome_model, &SR_FAULT_NONE_OBJ);
    if (word == SR_OUTPUT) return 0; /* board path reads output only through sr_phi_hw */
    return 0;
}

static void tu_write(uint32_t word, int32_t value) {
    if (word <= 3u) tu_ina[word] = value;
    else if (word >= 4u && word <= 7u) tu_z[word - 4u] = value;
    else if (word >= 8u && word <= 11u) tu_t[word - 8u] = value;
    else if (word >= 12u && word <= 27u) tu_dw[word - 12u] = value;
    else if (word == TU_CMD) tu_model_cmd((uint32_t)value);
    else if (word >= 32u && word <= 47u) tu_w1[word - 32u] = value;
    else if (word >= 48u && word <= 55u) tu_w2[word - 48u] = value;
}

static int32_t tu_read(uint32_t word) {
    if (word >= 32u && word <= 47u) return tu_w1[word - 32u];
    if (word >= 48u && word <= 55u) return tu_w2[word - 48u];
    if (word >= 64u && word <= 65u) return tu_d2_reg[word - 64u];
    if (word >= 68u && word <= 71u) return tu_d1_reg[word - 68u];
    if (word == TU_LOSS) return tu_loss;
    if (word == TU_BUSY) return 0;
    return 0;
}
#else
static void sr_write(uint32_t word, uint32_t value) {
    *(volatile uint32_t *)(SR_BASE + word * 4u) = value;
}

static uint32_t sr_read(uint32_t word) {
    return *(volatile uint32_t *)(SR_BASE + word * 4u);
}

static void tu_write(uint32_t word, int32_t value) {
    *(volatile uint32_t *)(TU_BASE + word * 4u) = (uint32_t)value;
}

static int32_t tu_read(uint32_t word) {
    return (int32_t)*(volatile uint32_t *)(TU_BASE + word * 4u);
}
#endif

static void tu_wait_idle(void) {
#ifdef MEMETIC_STRUCT_TRAIN_HOST_STUB
    return;
#else
    while (tu_read(TU_BUSY) & 1) { }
#endif
}

static void publish(uint32_t word) {
    MBOX = word;
#ifndef MEMETIC_STRUCT_TRAIN_HOST_STUB
    for (volatile uint32_t d = 0; d < 4000u; d++) { }
#endif
}

static void sr_load(const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) sr_write(SR_GEN(i), genome[i]);
}

static uint8_t sr_phi_hw(const int8_t x[EHW_NIN]) {
    uint32_t row = (uint32_t)(x[0] >= 8) | ((uint32_t)(x[1] >= 8) << 1) | ((uint32_t)(x[2] >= 8) << 2);
#ifdef MEMETIC_STRUCT_TRAIN_HOST_STUB
    return sr_eval_row(sr_genome_model, (int)row, &SR_FAULT_NONE_OBJ);
#else
    sr_write(SR_INPUT, row);
    return (uint8_t)(sr_read(SR_OUTPUT) & 1u);
#endif
}

static void transformed_x_hw(const int8_t x[EHW_NIN], int8_t out[EHW_NIN]) {
    uint8_t phi = sr_phi_hw(x);
    out[0] = x[0];
    out[1] = x[1];
    out[2] = x[2];
    out[3] = (int8_t)ms_clamp_i8((int)x[3] + (phi ? 8 : -8)); /* bias_x3 */
}

static void transformed_x_cpu(const uint8_t sr[SR_GENOME_LEN], const int8_t x[EHW_NIN], int8_t out[EHW_NIN]) {
    uint32_t row = (uint32_t)(x[0] >= 8) | ((uint32_t)(x[1] >= 8) << 1) | ((uint32_t)(x[2] >= 8) << 2);
    uint8_t phi = sr_eval_row(sr, (int)row, &SR_FAULT_NONE_OBJ);
    out[0] = x[0];
    out[1] = x[1];
    out[2] = x[2];
    out[3] = (int8_t)ms_clamp_i8((int)x[3] + (phi ? 8 : -8));
}

static void tu_master_load(const int32_t w1[EHW_NH][EHW_NIN], const int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++)
        for (int j = 0; j < EHW_NIN; j++) tu_write(TU_W1(i * EHW_NIN + j), w1[i][j]);
    for (int i = 0; i < EHW_NOUT; i++)
        for (int j = 0; j < EHW_NH; j++) tu_write(TU_W2(i * EHW_NH + j), w2[i][j]);
}

static void tu_master_read(int32_t w1[EHW_NH][EHW_NIN], int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++)
        for (int j = 0; j < EHW_NIN; j++) w1[i][j] = tu_read(TU_W1(i * EHW_NIN + j));
    for (int i = 0; i < EHW_NOUT; i++)
        for (int j = 0; j < EHW_NH; j++) w2[i][j] = tu_read(TU_W2(i * EHW_NH + j));
}

static void tu_loss_d2(const int32_t y[EHW_NOUT], const int32_t z2[EHW_NOUT],
                       uint8_t label, int32_t d2[EHW_NOUT]) {
    for (int i = 0; i < EHW_NOUT; i++) {
        tu_write(TU_INA(i), y[i]);
        tu_write(TU_Z(i), z2[i]);
        tu_write(TU_T(i), i == (int)label ? MEM_ONE : 0);
    }
    tu_write(TU_CMD, TU_CMD_LOSS_D2);
    for (int i = 0; i < EHW_NOUT; i++) d2[i] = tu_read(TU_D2(i));
}

static void tu_d1(const int32_t w2td2[EHW_NH], const int32_t z1[EHW_NH], int32_t d1[EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++) {
        tu_write(TU_INA(i), w2td2[i]);
        tu_write(TU_Z(i), z1[i]);
    }
    tu_write(TU_CMD, TU_CMD_D1);
    for (int i = 0; i < EHW_NH; i++) d1[i] = tu_read(TU_D1(i));
}

static int32_t sgd_epoch_tu(int32_t w1[EHW_NH][EHW_NIN],
                            int32_t w2[EHW_NOUT][EHW_NH],
                            const int order[EHW_NTEST]) {
    tu_write(TU_CMD, TU_CMD_CLR);
    for (int oi = 0; oi < EHW_NTEST; oi++) {
        int idx = order[oi];
        int8_t x[EHW_NIN];
        transformed_x_hw(EHW_TEST_X[idx], x);
        uint8_t label = EHW_TEST_Y[idx];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        mem_forward_master(w1, w2, x, z1, h, z2, y);

        int32_t d2[EHW_NOUT];
        tu_loss_d2(y, z2, label, d2);

        int8_t d2_i8[EHW_NOUT];
        int8_t w2_i8[EHW_NOUT][EHW_NH];
        for (int i = 0; i < EHW_NOUT; i++) {
            d2_i8[i] = ehw_q8(d2[i], MEM_DSHIFT);
            for (int j = 0; j < EHW_NH; j++) w2_i8[i][j] = ehw_q8(w2[i][j], MEM_WSHIFT);
        }
        int32_t w2td2[EHW_NH];
        for (int j = 0; j < EHW_NH; j++) {
            int32_t acc = 0;
            for (int i = 0; i < EHW_NOUT; i++) acc += (int32_t)w2_i8[i][j] * d2_i8[i];
            w2td2[j] = mem_sat16((acc + (1 << 3)) >> 4);
        }

        int32_t d1[EHW_NH];
        tu_d1(w2td2, z1, d1);

        for (int i = 0; i < EHW_NOUT; i++)
            for (int j = 0; j < EHW_NH; j++) tu_write(TU_DW(i * EHW_NH + j), mem_qmul(d2[i], h[j]));
        tu_write(TU_CMD, TU_CMD_UPD_L2);
        tu_wait_idle();

        for (int i = 0; i < EHW_NH; i++) {
            for (int j = 0; j < EHW_NIN; j++) {
                int32_t x_q88 = (int32_t)x[j] << MEM_XSHIFT;
                tu_write(TU_DW(i * EHW_NIN + j), mem_qmul(d1[i], x_q88));
            }
        }
        tu_write(TU_CMD, TU_CMD_UPD_L1);
        tu_wait_idle();
        tu_master_read(w1, w2);
    }
    return tu_read(TU_LOSS);
}

static int32_t adapt_tu(const int8_t genome[EHW_GENOME_LEN],
                        int epochs, int seed, int8_t out_genome[EHW_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    mem_master_from_genome(genome, w1, w2);
    tu_master_load(w1, w2);
    int order[EHW_NTEST];
    mem_rng_t rng = { (uint32_t)seed };
    int32_t last_sse = 0;
    for (int i = 0; i < EHW_NTEST; i++) order[i] = i;
    for (int ep = 0; ep < epochs; ep++) {
        for (int i = EHW_NTEST - 1; i > 0; i--) {
            int j = mem_rng_range(&rng, i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        last_sse = sgd_epoch_tu(w1, w2, order);
    }
    mem_genome_from_master(w1, w2, out_genome);
    return last_sse;
}

static int32_t sgd_epoch_cpu(int32_t w1[EHW_NH][EHW_NIN],
                             int32_t w2[EHW_NOUT][EHW_NH],
                             const uint8_t sr[SR_GENOME_LEN],
                             const int order[EHW_NTEST]) {
    int32_t sse = 0;
    for (int oi = 0; oi < EHW_NTEST; oi++) {
        int idx = order[oi];
        int8_t x[EHW_NIN];
        transformed_x_cpu(sr, EHW_TEST_X[idx], x);
        uint8_t label = EHW_TEST_Y[idx];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        mem_forward_master(w1, w2, x, z1, h, z2, y);
        int32_t err[EHW_NOUT], d2[EHW_NOUT];
        for (int k = 0; k < EHW_NOUT; k++) {
            int32_t target = (k == label) ? MEM_ONE : 0;
            err[k] = mem_clamp32(y[k] - target, -MEM_ERR_CLAMP, MEM_ERR_CLAMP - 1);
            sse += mem_qmul(err[k], err[k]);
            d2[k] = mem_clamp32(mem_qmul(err[k], mem_leaky_d(z2[k])), -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        }
        int32_t dw2[EHW_NOUT][EHW_NH];
        for (int i = 0; i < EHW_NOUT; i++)
            for (int j = 0; j < EHW_NH; j++) dw2[i][j] = mem_qmul(d2[i], h[j]);
        int8_t d2_i8[EHW_NOUT], w2_i8[EHW_NOUT][EHW_NH];
        for (int i = 0; i < EHW_NOUT; i++) {
            d2_i8[i] = ehw_q8(d2[i], MEM_DSHIFT);
            for (int j = 0; j < EHW_NH; j++) w2_i8[i][j] = ehw_q8(w2[i][j], MEM_WSHIFT);
        }
        int32_t w2td2[EHW_NH];
        for (int j = 0; j < EHW_NH; j++) {
            int32_t acc = 0;
            for (int i = 0; i < EHW_NOUT; i++) acc += (int32_t)w2_i8[i][j] * d2_i8[i];
            w2td2[j] = mem_sat16((acc + (1 << 3)) >> 4);
        }
        int32_t d1[EHW_NH];
        for (int i = 0; i < EHW_NH; i++)
            d1[i] = mem_clamp32(mem_qmul(w2td2[i], mem_leaky_d(z1[i])), -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        int32_t dw1[EHW_NH][EHW_NIN];
        for (int i = 0; i < EHW_NH; i++)
            for (int j = 0; j < EHW_NIN; j++) dw1[i][j] = mem_qmul(d1[i], (int32_t)x[j] << MEM_XSHIFT);
        for (int i = 0; i < EHW_NOUT; i++)
            for (int j = 0; j < EHW_NH; j++) w2[i][j] = mem_sat16(w2[i][j] - (dw2[i][j] >> MEM_LR_SHIFT));
        for (int i = 0; i < EHW_NH; i++)
            for (int j = 0; j < EHW_NIN; j++) w1[i][j] = mem_sat16(w1[i][j] - (dw1[i][j] >> MEM_LR_SHIFT));
    }
    return sse;
}

static int32_t adapt_cpu(const int8_t genome[EHW_GENOME_LEN],
                         const uint8_t sr[SR_GENOME_LEN],
                         int epochs, int seed, int8_t out_genome[EHW_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    mem_master_from_genome(genome, w1, w2);
    int order[EHW_NTEST];
    mem_rng_t rng = { (uint32_t)seed };
    int32_t last_sse = 0;
    for (int i = 0; i < EHW_NTEST; i++) order[i] = i;
    for (int ep = 0; ep < epochs; ep++) {
        for (int i = EHW_NTEST - 1; i > 0; i--) {
            int j = mem_rng_range(&rng, i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        last_sse = sgd_epoch_cpu(w1, w2, sr, order);
    }
    mem_genome_from_master(w1, w2, out_genome);
    return last_sse;
}

static ehw_score_t evaluate_transformed(const int8_t genome[EHW_GENOME_LEN], const uint8_t sr[SR_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    mem_master_from_genome(genome, w1, w2);
    ehw_score_t score = {0, 0, 0};
    for (int i = 0; i < EHW_NTEST; i++) {
        int8_t x[EHW_NIN];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        transformed_x_cpu(sr, EHW_TEST_X[i], x);
        uint8_t pred = mem_forward_master(w1, w2, x, z1, h, z2, y);
        score.correct += pred == EHW_TEST_Y[i];
        for (int k = 0; k < EHW_NOUT; k++) {
            int32_t target = (k == EHW_TEST_Y[i]) ? MEM_ONE : 0;
            int32_t err = y[k] - target;
            score.sse += mem_qmul(err, err);
        }
    }
    score.fitness = score.correct * 1000000 - score.sse;
    return score;
}

int main(void) {
    publish(0xF5000005u);
    sr_load(SR_FEATURE_GENOME);
    uint32_t marker = sr_read(SR_MARKER);
    uint32_t mask = sr_read(SR_MASK) & 0xffu;
    publish(0xF5400000u | mask);

    int8_t gold[EHW_GENOME_LEN], got[EHW_GENOME_LEN];
    int32_t gold_sse = adapt_cpu(EHW_TRAINED_GENOME, SR_FEATURE_GENOME, 1, 3, gold);
    int32_t got_sse = adapt_tu(EHW_TRAINED_GENOME, 1, 3, got);
    int mism = 0;
    for (int i = 0; i < EHW_GENOME_LEN; i++) mism += gold[i] != got[i];
    ehw_score_t score = evaluate_transformed(got, SR_FEATURE_GENOME);
    publish(0xF5200000u | ((uint32_t)(score.correct & 0xff) << 8) | (uint32_t)(mism & 0xff));
    publish(0xF5300000u | ((uint32_t)score.sse & 0xffffu));
    uint32_t pass = (marker == 0x53525630u) && (mask == SR_FEATURE_TRUTH_MASK) &&
                    (mism == 0) && (gold_sse == got_sse);
    publish(pass ? 0xF5F00000u : 0xF5F00001u);
#ifdef MEMETIC_STRUCT_TRAIN_HOST_STUB
    if (!pass) {
        fprintf(stderr, "FAIL: memetic-struct train stub marker=%08x mask=%02x mism=%d gold_sse=%d got_sse=%d correct=%d sse=%d\n",
                marker, (unsigned)mask, mism, (int)gold_sse, (int)got_sse, score.correct, score.sse);
        return 1;
    }
    printf("PASS: memetic-struct VRC+train-unit stub matches CPU golden (mask=%02x sse=%d correct=%d)\n",
           (unsigned)mask, (int)got_sse, score.correct);
    return 0;
#else
    /* Board-only evidence carousel (EHW-3.2 lesson: every pass-condition input
       must live in the steady republish loop, one-shot publishes are unreadable
       over UART-paced md sampling). Does not touch GA/decode/host-stub path. */
    while (1) {
        publish(0xF5100000u | (marker >> 16));
        publish(0xF5110000u | (marker & 0xffffu));
        publish(0xF5400000u | mask);
        publish(0xF5200000u | ((uint32_t)(score.correct & 0xff) << 8) | (uint32_t)(mism & 0xff));
        publish(0xF5300000u | ((uint32_t)score.sse & 0xffffu));
        publish(0xF5600000u | ((uint32_t)gold_sse & 0xffffu));
        publish(0xF5610000u | ((uint32_t)got_sse & 0xffffu));
        publish(0xF5620000u | ((uint32_t)(gold_sse - got_sse) & 0xffffu));
        publish(pass ? 0xF5F00000u : 0xF5F00001u);
    }
#endif
}
