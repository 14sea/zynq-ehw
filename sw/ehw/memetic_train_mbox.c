/* EHW-4.2 firmware prep: memetic train-unit protocol smoke test.
 *
 * This is not the full board GA yet.  It proves the board-bound split that EHW-4.3
 * will use: firmware keeps the forward path / outer products, while an EHW-local
 * train unit owns the Q8.8 master weights and performs loss+d2, d1, and saturating
 * SGD updates through MMIO.
 *
 * Host mode (-DMEMETIC_TRAIN_HOST_STUB) models the same register protocol and
 * compares against mem_adapt() from memetic_kernel.h.  Board mode uses the train
 * unit window at 0xF0000800 and publishes compact 0xF4xxxxxx mailbox words.
 */

#include "memetic_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef MEMETIC_TRAIN_HOST_STUB
#include <stdio.h>
static uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#else
#include <neorv32.h>
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define TU_BASE 0xF0000800U

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

#define TU_CMD_LOSS_D2 0x01u
#define TU_CMD_D1      0x02u
#define TU_CMD_UPD_L2  0x04u
#define TU_CMD_UPD_L1  0x08u
#define TU_CMD_CLR     0x10u

#ifdef MEMETIC_TRAIN_HOST_STUB
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
        for (int i = 0; i < EHW_NOUT * EHW_NH; i++) {
            tu_w2[i] = mem_sat16(tu_w2[i] - (tu_dw[i] >> MEM_LR_SHIFT));
        }
    }
    if (cmd & TU_CMD_UPD_L1) {
        for (int i = 0; i < EHW_NH * EHW_NIN; i++) {
            tu_w1[i] = mem_sat16(tu_w1[i] - (tu_dw[i] >> MEM_LR_SHIFT));
        }
    }
    if (cmd & TU_CMD_CLR) tu_loss = 0;
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
    return 0;
}
#else
static void tu_write(uint32_t word, int32_t value) {
    *(volatile uint32_t *)(TU_BASE + word * 4u) = (uint32_t)value;
}

static int32_t tu_read(uint32_t word) {
    return (int32_t)*(volatile uint32_t *)(TU_BASE + word * 4u);
}
#endif

static void publish(uint32_t word) {
    MBOX = word;
#ifndef MEMETIC_TRAIN_HOST_STUB
    for (volatile uint32_t d = 0; d < 4000u; d++) { }
#endif
}

static void tu_master_load(const int32_t w1[EHW_NH][EHW_NIN], const int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++) {
        for (int j = 0; j < EHW_NIN; j++) tu_write(TU_W1(i * EHW_NIN + j), w1[i][j]);
    }
    for (int i = 0; i < EHW_NOUT; i++) {
        for (int j = 0; j < EHW_NH; j++) tu_write(TU_W2(i * EHW_NH + j), w2[i][j]);
    }
}

static void tu_master_read(int32_t w1[EHW_NH][EHW_NIN], int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++) {
        for (int j = 0; j < EHW_NIN; j++) w1[i][j] = tu_read(TU_W1(i * EHW_NIN + j));
    }
    for (int i = 0; i < EHW_NOUT; i++) {
        for (int j = 0; j < EHW_NH; j++) w2[i][j] = tu_read(TU_W2(i * EHW_NH + j));
    }
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

static int32_t mem_sgd_epoch_tu(int32_t w1[EHW_NH][EHW_NIN],
                                int32_t w2[EHW_NOUT][EHW_NH],
                                const int order[EHW_NTEST]) {
    tu_write(TU_CMD, TU_CMD_CLR);
    for (int oi = 0; oi < EHW_NTEST; oi++) {
        int idx = order[oi];
        const int8_t *x = EHW_TEST_X[idx];
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

        for (int i = 0; i < EHW_NOUT; i++) {
            for (int j = 0; j < EHW_NH; j++) tu_write(TU_DW(i * EHW_NH + j), mem_qmul(d2[i], h[j]));
        }
        tu_write(TU_CMD, TU_CMD_UPD_L2);

        for (int i = 0; i < EHW_NH; i++) {
            for (int j = 0; j < EHW_NIN; j++) {
                int32_t x_q88 = (int32_t)x[j] << MEM_XSHIFT;
                tu_write(TU_DW(i * EHW_NIN + j), mem_qmul(d1[i], x_q88));
            }
        }
        tu_write(TU_CMD, TU_CMD_UPD_L1);
        tu_master_read(w1, w2);
    }
    return tu_read(TU_LOSS);
}

static int32_t mem_adapt_tu(const int8_t genome[EHW_GENOME_LEN],
                            int epochs, int seed, int8_t out_genome[EHW_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    mem_master_from_genome(genome, w1, w2);
    tu_master_load(w1, w2);
    mem_rng_t rng = { (uint32_t)seed };
    int order[EHW_NTEST];
    int32_t last_sse = 0;
    for (int i = 0; i < EHW_NTEST; i++) order[i] = i;
    for (int ep = 0; ep < epochs; ep++) {
        for (int i = EHW_NTEST - 1; i > 0; i--) {
            int j = mem_rng_range(&rng, i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        last_sse = mem_sgd_epoch_tu(w1, w2, order);
    }
    mem_genome_from_master(w1, w2, out_genome);
    return last_sse;
}

int main(void) {
    publish(0xF4000042u);
    int8_t gold[EHW_GENOME_LEN], got[EHW_GENOME_LEN];
    int32_t gold_sse = mem_adapt(EHW_TRAINED_GENOME, 2, MEM_LR_SHIFT, 3, gold);
    int32_t got_sse = mem_adapt_tu(EHW_TRAINED_GENOME, 2, 3, got);
    int mism = 0;
    for (int i = 0; i < EHW_GENOME_LEN; i++) mism += (gold[i] != got[i]);
    ehw_score_t score = ehw_evaluate(got);
    publish(0xF4200000u | ((uint32_t)(mism & 0xFF) << 8) | (uint32_t)((gold_sse == got_sse) ? 1 : 0));
    publish(0xF4300000u | ((uint32_t)(score.correct & 0xFF) << 16) | ((uint32_t)score.sse & 0xFFFF));
    publish((mism == 0 && gold_sse == got_sse) ? 0xF4F00000u : 0xF4F00001u);
#ifdef MEMETIC_TRAIN_HOST_STUB
    if (mism || gold_sse != got_sse) {
        fprintf(stderr, "FAIL: memetic train-unit stub mismatch mism=%d gold_sse=%d got_sse=%d\n",
                mism, (int)gold_sse, (int)got_sse);
        return 1;
    }
    printf("PASS: memetic train-unit firmware stub matches mem_adapt (sse=%d correct=%d)\n",
           (int)got_sse, score.correct);
    return 0;
#else
    while (1) { MBOX = (mism == 0 && gold_sse == got_sse) ? 0xF4F00000u : 0xF4F00001u; }
#endif
}
