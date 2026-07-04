/* EHW-5.3 firmware: hybrid structure+weight Lamarckian GA on the board.
 *
 * This ports exactly one EHW-5.1 arm to the board:
 *   hybrid_lamarckian_pressure / bias_x3, seed=3, POP=16, GENS=32, ADAPT=1.
 *
 * Candidate evaluation uses the EHW-5.2 hardware paths:
 *   - spare-route VRC feature island at 0xF0000400
 *   - lite train-unit adaptation window at 0xF0000800
 *
 * Host mode (-DMEMETIC_STRUCT_GA_HOST_STUB) models the same MMIO protocol and
 * emits a per-generation curve CSV that must byte-match memetic_struct_eval.c.
 */

#include "memetic_struct_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
#include <stdio.h>
static uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#else
#include <neorv32.h>
typedef void FILE;
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define SR_BASE 0xF0000400U
#define TU_BASE 0xF0000800U

#define SR_INPUT       2u
#define SR_OUTPUT      3u
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

#define EHW53_SEED        3
#define EHW53_POP         16
#define EHW53_GENS        32
#define EHW53_ADAPT       1
#define EHW53_INIT_SPAN   32
#define EHW53_ELITES      2
#define EHW53_TOURN       3
#define EHW53_CROSS_PPM   700000
#define EHW53_MUT_PPM     30000
#define EHW53_MUT_STEP    8
#define EHW53_STRUCT_PPM  250000
#define EHW53_INIT_PPM    50000
#define EHW53_SEL_PPM     30000

#define EHW53_EXPECT_CORRECT 40
#define EHW53_EXPECT_SSE     4513
#define EHW53_EXPECT_FIRST40 2
#define EHW53_EXPECT_ONES    15
#define EHW53_EXPECT_PENALTY 0

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
static uint8_t sr_genome_model[SR_GENOME_LEN];
static uint8_t sr_input_model;
static int32_t tu_ina[4], tu_z[4], tu_t[4], tu_dw[16], tu_w1[16], tu_w2[8];
static int32_t tu_d2_reg[2], tu_d1_reg[4], tu_loss;

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
    else if (word == SR_INPUT) sr_input_model = (uint8_t)(value & 7u);
}

static uint32_t sr_read(uint32_t word) {
    if (word == SR_OUTPUT) return sr_eval_row(sr_genome_model, sr_input_model, &SR_FAULT_NONE_OBJ);
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

static ms_candidate_t pop[EHW53_POP];
static ms_candidate_t next_pop[EHW53_POP];
static ms_eval_t evaluated[EHW53_POP];
static int ranked_idx[EHW53_POP];

static void publish(uint32_t word) {
    MBOX = word;
#ifndef MEMETIC_STRUCT_GA_HOST_STUB
    for (volatile uint32_t d = 0; d < 4000u; d++) { }
#endif
}

static void tu_wait_idle(void) {
#ifndef MEMETIC_STRUCT_GA_HOST_STUB
    while (tu_read(TU_BUSY) & 1) { }
#endif
}

static void copy_sr(uint8_t dst[SR_GENOME_LEN], const uint8_t src[SR_GENOME_LEN]) {
    memcpy(dst, src, SR_GENOME_LEN);
}

static void sr_load(const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) sr_write(SR_GEN(i), genome[i]);
}

static uint8_t sr_phi_hw(const int8_t x[EHW_NIN]) {
    uint32_t row = (uint32_t)(x[0] >= 8) | ((uint32_t)(x[1] >= 8) << 1) | ((uint32_t)(x[2] >= 8) << 2);
    sr_write(SR_INPUT, row);
    return (uint8_t)(sr_read(SR_OUTPUT) & 1u);
}

static void transformed_x_hw(const int8_t x[EHW_NIN], int8_t out[EHW_NIN]) {
    uint8_t phi = sr_phi_hw(x);
    out[0] = x[0];
    out[1] = x[1];
    out[2] = x[2];
    out[3] = (int8_t)ms_clamp_i8((int)x[3] + (phi ? 8 : -8));
}

static uint64_t feature_mask_hw(const uint8_t sr[SR_GENOME_LEN]) {
    uint64_t mask = 0;
    sr_load(sr);
    for (int i = 0; i < EHW_NTEST; i++) {
        if (sr_phi_hw(EHW_TEST_X[i])) mask |= 1ull << i;
    }
    return mask;
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
    int32_t sse = 0;
    tu_write(TU_CMD, TU_CMD_CLR);
    for (int oi = 0; oi < EHW_NTEST; oi++) {
        int idx = order[oi];
        int8_t x[EHW_NIN];
        uint8_t label = EHW_TEST_Y[idx];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        transformed_x_hw(EHW_TEST_X[idx], x);
        mem_forward_master(w1, w2, x, z1, h, z2, y);

        int32_t d2[EHW_NOUT];
        tu_loss_d2(y, z2, label, d2);
        sse = tu_read(TU_LOSS);

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
    return sse;
}

static int32_t adapt_tu(const int8_t weight[EHW_GENOME_LEN],
                        const uint8_t sr[SR_GENOME_LEN],
                        int seed,
                        int8_t out_weight[EHW_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    int order[EHW_NTEST];
    int32_t last_sse = 0;
    mem_master_from_genome(weight, w1, w2);
    tu_master_load(w1, w2);
    sr_load(sr);
    for (int i = 0; i < EHW_NTEST; i++) order[i] = i;
    mem_rng_t rng = { (uint32_t)seed };
    for (int ep = 0; ep < EHW53_ADAPT; ep++) {
        for (int i = EHW_NTEST - 1; i > 0; i--) {
            int j = mem_rng_range(&rng, i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        last_sse = sgd_epoch_tu(w1, w2, order);
    }
    mem_genome_from_master(w1, w2, out_weight);
    return last_sse;
}

static ehw_score_t evaluate_hw(const int8_t weight[EHW_GENOME_LEN], const uint8_t sr[SR_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    ehw_score_t score = {0, 0, 0};
    mem_master_from_genome(weight, w1, w2);
    sr_load(sr);
    for (int i = 0; i < EHW_NTEST; i++) {
        int8_t x[EHW_NIN];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        transformed_x_hw(EHW_TEST_X[i], x);
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

static void eval_candidate(const uint8_t sr[SR_GENOME_LEN],
                           const int8_t weight[EHW_GENOME_LEN],
                           int seed,
                           ms_eval_t *out) {
    out->pre_score = evaluate_hw(weight, sr);
    mem_copy_genome(out->pre_weight, weight);
    adapt_tu(weight, sr, seed, out->post_weight);
    out->post_score = evaluate_hw(out->post_weight, sr);
    copy_sr(out->sr, sr);
    out->feature_mask = feature_mask_hw(sr);
    out->feature_ones = ms_feature_ones(out->feature_mask);
    out->feature_penalty = ms_structural_penalty(out->feature_mask, &(ms_options_t){
        .feature_min_balance = MS_DEFAULT_FEATURE_MIN_BALANCE,
        .feature_penalty = MS_DEFAULT_FEATURE_PENALTY,
    });
    out->select_score = out->post_score;
    out->select_score.fitness -= out->feature_penalty;
    mem_copy_genome(out->weight_for_next, out->post_weight);
}

static void sr_scaffold_genome(mem_rng_t *rng, int use_spare, uint8_t genome[SR_GENOME_LEN]) {
    genome[0] = 0x0au;
    genome[1] = 0x0au;
    genome[2] = 0x0au;
    genome[3] = use_spare ? 0x0au : 0x00u;
    genome[4] = (uint8_t)mem_rng_range(rng, 256);
    int srcs[4] = {0, 1, 2, use_spare ? 1 : 3};
    for (int i = 0; i < 4; i++) {
        genome[5 + 2 * i] = (uint8_t)srcs[i];
        genome[6 + 2 * i] = (uint8_t)srcs[i];
    }
    if (use_spare) {
        genome[13] = 0;
        genome[14] = 3;
        genome[15] = 2;
    } else {
        genome[13] = 0;
        genome[14] = 1;
        genome[15] = 2;
    }
}

static void sr_mutate_genome(const uint8_t in[SR_GENOME_LEN],
                             uint8_t out[SR_GENOME_LEN],
                             mem_rng_t *rng) {
    copy_sr(out, in);
    int changed = 0;
    for (int idx = 0; idx <= 4; idx++) {
        int bits = idx == 4 ? 8 : 4;
        uint8_t value = out[idx];
        for (int bit = 0; bit < bits; bit++) {
            if (mem_rng_chance(rng, EHW53_INIT_PPM)) {
                value ^= (uint8_t)(1u << bit);
                changed = 1;
            }
        }
        out[idx] = (uint8_t)(value & ((1u << bits) - 1u));
    }
    for (int idx = 5; idx < SR_GENOME_LEN; idx++) {
        int limit = idx < 13 ? 5 : 4;
        if (mem_rng_chance(rng, EHW53_SEL_PPM)) {
            out[idx] = (uint8_t)mem_rng_range(rng, limit);
            changed = 1;
        }
    }
    if (!changed) {
        int idx = mem_rng_range(rng, SR_GENOME_LEN);
        if (idx < 4) out[idx] ^= (uint8_t)(1u << mem_rng_range(rng, 4));
        else if (idx == 4) out[idx] ^= (uint8_t)(1u << mem_rng_range(rng, 8));
        else if (idx < 13) out[idx] = (uint8_t)mem_rng_range(rng, 5);
        else out[idx] = (uint8_t)mem_rng_range(rng, 4);
    }
}

static void sr_crossover_genome(const uint8_t a[SR_GENOME_LEN],
                                const uint8_t b[SR_GENOME_LEN],
                                uint8_t out[SR_GENOME_LEN],
                                mem_rng_t *rng) {
    for (int i = 0; i < SR_GENOME_LEN; i++) {
        out[i] = mem_rng_chance(rng, 500000) ? a[i] : b[i];
    }
}

static void copy_candidate(ms_candidate_t *dst, const ms_candidate_t *src) {
    copy_sr(dst->sr, src->sr);
    mem_copy_genome(dst->weight, src->weight);
}

static void mutate_hybrid(const ms_candidate_t *in, ms_candidate_t *out, mem_rng_t *rng) {
    copy_candidate(out, in);
    if (mem_rng_chance(rng, EHW53_STRUCT_PPM))
        sr_mutate_genome(in->sr, out->sr, rng);
    mem_mutate(in->weight, out->weight, rng, EHW53_MUT_PPM, EHW53_MUT_STEP);
}

static void crossover_hybrid(const ms_candidate_t *a,
                             const ms_candidate_t *b,
                             ms_candidate_t *out,
                             mem_rng_t *rng) {
    sr_crossover_genome(a->sr, b->sr, out->sr, rng);
    mem_crossover(a->weight, b->weight, out->weight, rng);
}

static void seed_hybrid_population(mem_rng_t *rng) {
    int8_t weight_seeds[8][EHW_GENOME_LEN];
    uint8_t sr_seeds[4][SR_GENOME_LEN];
    mem_seed_population(rng, 8, EHW53_INIT_SPAN, weight_seeds);
    copy_sr(sr_seeds[0], MS_SR_MAJORITY);
    copy_sr(sr_seeds[1], MS_SR_REPAIR);
    sr_scaffold_genome(rng, 0, sr_seeds[2]);
    sr_scaffold_genome(rng, 1, sr_seeds[3]);

    int n = 0;
    for (int s = 0; s < 4; s++) {
        for (int w = 0; w < 2; w++) {
            copy_sr(pop[n].sr, sr_seeds[s]);
            mem_copy_genome(pop[n].weight, weight_seeds[w]);
            n++;
        }
    }
    while (n < EHW53_POP) {
        int s = mem_rng_range(rng, 4);
        sr_mutate_genome(sr_seeds[s], pop[n].sr, rng);
        int w = mem_rng_range(rng, 8);
        int8_t tmp[EHW_GENOME_LEN];
        mem_mutate(weight_seeds[w], tmp, rng, EHW53_MUT_PPM, EHW53_MUT_STEP);
        mem_copy_genome(pop[n].weight, tmp);
        n++;
    }
}

static int better_score_idx(const ehw_score_t *as, int ai, const ehw_score_t *bs, int bi) {
    if (as->fitness != bs->fitness) return as->fitness > bs->fitness;
    return ai < bi;
}

static void sort_ranked(void) {
    for (int i = 0; i < EHW53_POP; i++) ranked_idx[i] = i;
    for (int i = 1; i < EHW53_POP; i++) {
        int item = ranked_idx[i];
        int j = i - 1;
        while (j >= 0 && better_score_idx(&evaluated[item].select_score, item,
                                          &evaluated[ranked_idx[j]].select_score,
                                          ranked_idx[j])) {
            ranked_idx[j + 1] = ranked_idx[j];
            j--;
        }
        ranked_idx[j + 1] = item;
    }
}

static int tournament_pick(mem_rng_t *rng) {
    int best_pos = mem_rng_range(rng, EHW53_POP);
    int best_idx = ranked_idx[best_pos];
    for (int i = 1; i < EHW53_TOURN; i++) {
        int cur_pos = mem_rng_range(rng, EHW53_POP);
        int cur_idx = ranked_idx[cur_pos];
        if (better_score_idx(&evaluated[cur_idx].select_score, cur_idx,
                             &evaluated[best_idx].select_score, best_idx)) {
            best_idx = cur_idx;
        }
    }
    return best_idx;
}

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
static void print_i8_genome(FILE *f, const int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%d", genome[i]);
    }
}

static void print_sr_genome(FILE *f, const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%02x", genome[i] & 0xffu);
    }
}

static void write_curve_header(FILE *f) {
    fprintf(f, "mode,coupling,gen,best_correct,best_sse,best_fitness,select_fitness,feature_mask,feature_ones,feature_penalty,top_index,sr_genome,pre_weight,post_weight\n");
}

static void write_curve_row(FILE *f, int gen, int top_index, const ms_eval_t *best) {
    fprintf(f, "hybrid_lamarckian_pressure,bias_x3,%d,%d,%d,%d,%d,%010llx,%d,%d,%d,",
            gen, best->post_score.correct, best->post_score.sse,
            best->post_score.fitness, best->select_score.fitness,
            (unsigned long long)best->feature_mask, best->feature_ones,
            best->feature_penalty, top_index);
    print_sr_genome(f, best->sr);
    fputc(',', f);
    print_i8_genome(f, best->pre_weight);
    fputc(',', f);
    print_i8_genome(f, best->post_weight);
    fputc('\n', f);
}
#endif

#ifndef MEMETIC_STRUCT_GA_HOST_STUB
static void publish_curve_word(int gen, const ms_eval_t *best) {
    uint32_t penalty_bucket = (uint32_t)(best->feature_penalty / MS_DEFAULT_FEATURE_PENALTY);
    publish(0xF5300000u | ((uint32_t)(gen & 0xff) << 8) | (uint32_t)(best->post_score.correct & 0xff));
    publish(0xF5310000u | ((uint32_t)best->post_score.sse & 0xffffu));
    publish(0xF5320000u | ((uint32_t)(best->feature_ones & 0xff) << 8) | (penalty_bucket & 0xffu));
}
#endif

static void run_ga(FILE *curve, ms_eval_t *best_out, int *first_40_out) {
    mem_rng_t rng = { (uint32_t)(EHW53_SEED + 505) };
    seed_hybrid_population(&rng);
    eval_candidate(pop[0].sr, pop[0].weight, EHW53_SEED, &evaluated[0]);
    ms_eval_t best = evaluated[0];
    int first_40 = -1;

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
    write_curve_header(curve);
#endif

    for (int gen = 0; gen <= EHW53_GENS; gen++) {
        for (int i = 0; i < EHW53_POP; i++) {
            eval_candidate(pop[i].sr, pop[i].weight, EHW53_SEED + gen * 1009 + i, &evaluated[i]);
        }
        sort_ranked();
        int top = ranked_idx[0];
        if (evaluated[top].select_score.fitness > best.select_score.fitness) {
            best = evaluated[top];
        }
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;
#ifdef MEMETIC_STRUCT_GA_HOST_STUB
        write_curve_row(curve, gen, top, &best);
#else
        publish_curve_word(gen, &best);
#endif
        if (gen == EHW53_GENS) break;

        for (int i = 0; i < EHW53_ELITES; i++) {
            int src = ranked_idx[i];
            copy_sr(next_pop[i].sr, evaluated[src].sr);
            mem_copy_genome(next_pop[i].weight, evaluated[src].weight_for_next);
        }
        int nnext = EHW53_ELITES;
        while (nnext < EHW53_POP) {
            int p1 = tournament_pick(&rng);
            ms_candidate_t parent, child;
            copy_sr(parent.sr, evaluated[p1].sr);
            mem_copy_genome(parent.weight, evaluated[p1].weight_for_next);
            if (mem_rng_chance(&rng, EHW53_CROSS_PPM)) {
                int p2 = tournament_pick(&rng);
                ms_candidate_t mate;
                copy_sr(mate.sr, evaluated[p2].sr);
                mem_copy_genome(mate.weight, evaluated[p2].weight_for_next);
                crossover_hybrid(&parent, &mate, &child, &rng);
            } else {
                copy_candidate(&child, &parent);
            }
            mutate_hybrid(&child, &next_pop[nnext], &rng);
            nnext++;
        }
        memcpy(pop, next_pop, sizeof(pop));
    }
    *best_out = best;
    *first_40_out = first_40;
}

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
static const char *arg_value(int argc, char **argv, const char *name, const char *fallback) {
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], name)) return argv[i + 1];
    }
    return fallback;
}
#endif

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
int main(int argc, char **argv) {
#else
int main(void) {
#endif
    ms_eval_t best;
    int first_40;

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
    const char *curve_path = arg_value(argc, argv, "--curve-csv", "runs/ehw5_3_struct_ga_curve.csv");
    FILE *curve = fopen(curve_path, "w");
    if (!curve) {
        perror(curve_path);
        return 1;
    }
    run_ga(curve, &best, &first_40);
    fclose(curve);
#else
    publish(0xF5000003u);
    run_ga(0, &best, &first_40);
#endif

    uint32_t pass = best.post_score.correct == EHW53_EXPECT_CORRECT &&
                    best.post_score.sse == EHW53_EXPECT_SSE &&
                    first_40 == EHW53_EXPECT_FIRST40 &&
                    best.feature_ones == EHW53_EXPECT_ONES &&
                    best.feature_penalty == EHW53_EXPECT_PENALTY;

    publish(0xF53F0000u | (uint32_t)((first_40 >= 0 ? first_40 : 0xffff) & 0xffff));
    publish(pass ? 0xF5F30000u : 0xF5F30001u);

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
    if (!pass) {
        fprintf(stderr, "FAIL: EHW-5.3 host stub best=%d/40 sse=%d first_40=%d ones=%d penalty=%d\n",
                best.post_score.correct, best.post_score.sse, first_40,
                best.feature_ones, best.feature_penalty);
        return 1;
    }
    printf("PASS: EHW-5.3 hybrid GA host stub best=40/40 sse=4513 first_40=2 feature_ones=15 penalty=0\n");
    return 0;
#else
    while (1) {
        publish_curve_word(EHW53_GENS, &best);
        publish(0xF53F0000u | (uint32_t)((first_40 >= 0 ? first_40 : 0xffff) & 0xffff));
        publish(pass ? 0xF5F30000u : 0xF5F30001u);
    }
#endif
}
