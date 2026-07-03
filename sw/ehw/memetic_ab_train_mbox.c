/* EHW-4.5 firmware prep: same-boot Baldwinian vs Lamarckian train-unit GA.
 *
 * One firmware image runs two arms back-to-back on the same bitstream and same
 * boot: Baldwinian first, then Lamarckian.  Every candidate evaluation:
 *   genome -> train-unit master load -> 1 fixed-point SGD epoch via MMIO
 *          -> adapted genome -> label/SSE fitness
 *
 * Baldwinian selection uses post-adapt fitness but keeps the original genome for
 * reproduction; Lamarckian selection writes the adapted genome back.
 *
 * The board path uses the EHW-4.3-verified train-unit window at 0xF0000800.
 * Host mode (-DMEMETIC_AB_HOST_STUB) models the same MMIO protocol and emits
 * both curves for byte-for-byte comparison against memetic_eval.c.
 */

#include "memetic_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef MEMETIC_AB_HOST_STUB
#include <stdio.h>
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

#define EHW45_SEED          3
#define EHW45_POP           16
#define EHW45_GENS          32
#define EHW45_ADAPT_EPOCHS  1
#define EHW45_ELITES        2
#define EHW45_TOURNAMENT    3
#define EHW45_CROSS_PPM     700000
#define EHW45_MUT_PPM       30000
#define EHW45_MUT_STEP      8
#define EHW45_INIT_SPAN     32

typedef enum {
    EHW45_BALDWINIAN = 0,
    EHW45_LAMARCKIAN = 1,
} ehw45_mode_t;

#ifdef MEMETIC_AB_HOST_STUB
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

#ifndef MEMETIC_AB_HOST_STUB
static void publish(uint32_t word) {
    MBOX = word;
    for (volatile uint32_t d = 0; d < 4000u; d++) { }
}
#endif

static void tu_master_load(const int32_t w1[EHW_NH][EHW_NIN], const int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++)
        for (int j = 0; j < EHW_NIN; j++)
            tu_write(TU_W1(i * EHW_NIN + j), w1[i][j]);
    for (int i = 0; i < EHW_NOUT; i++)
        for (int j = 0; j < EHW_NH; j++)
            tu_write(TU_W2(i * EHW_NH + j), w2[i][j]);
}

static void tu_master_read(int32_t w1[EHW_NH][EHW_NIN], int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++)
        for (int j = 0; j < EHW_NIN; j++)
            w1[i][j] = tu_read(TU_W1(i * EHW_NIN + j));
    for (int i = 0; i < EHW_NOUT; i++)
        for (int j = 0; j < EHW_NH; j++)
            w2[i][j] = tu_read(TU_W2(i * EHW_NH + j));
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

        for (int i = 0; i < EHW_NOUT; i++)
            for (int j = 0; j < EHW_NH; j++)
                tu_write(TU_DW(i * EHW_NH + j), mem_qmul(d2[i], h[j]));
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

static void mem_eval_candidate_tu(const int8_t genome[EHW_GENOME_LEN],
                                  int seed, ehw45_mode_t mode, mem_eval_t *out) {
    out->pre_score = ehw_evaluate(genome);
    mem_copy_genome(out->pre_genome, genome);
    mem_adapt_tu(genome, EHW45_ADAPT_EPOCHS, seed, out->post_genome);
    out->post_score = ehw_evaluate(out->post_genome);
    out->select_score = out->post_score;
    if (mode == EHW45_BALDWINIAN) mem_copy_genome(out->genome_for_next, genome);
    else mem_copy_genome(out->genome_for_next, out->post_genome);
}

static int better_score_idx(const ehw_score_t *as, int ai, const ehw_score_t *bs, int bi) {
    if (as->fitness != bs->fitness) return as->fitness > bs->fitness;
    return ai < bi;
}

static void sort_ranked(const mem_eval_t evaluated[EHW45_POP], int ranked_idx[EHW45_POP]) {
    for (int i = 0; i < EHW45_POP; i++) ranked_idx[i] = i;
    for (int i = 1; i < EHW45_POP; i++) {
        int item = ranked_idx[i];
        int j = i - 1;
        while (j >= 0 && better_score_idx(&evaluated[item].select_score, item,
                                          &evaluated[ranked_idx[j]].select_score, ranked_idx[j])) {
            ranked_idx[j + 1] = ranked_idx[j];
            j--;
        }
        ranked_idx[j + 1] = item;
    }
}

static int tournament_pick(const mem_eval_t evaluated[EHW45_POP],
                           const int ranked_idx[EHW45_POP], mem_rng_t *rng) {
    int best_pos = mem_rng_range(rng, EHW45_POP);
    int best_idx = ranked_idx[best_pos];
    for (int i = 1; i < EHW45_TOURNAMENT; i++) {
        int cur_pos = mem_rng_range(rng, EHW45_POP);
        int cur_idx = ranked_idx[cur_pos];
        if (better_score_idx(&evaluated[cur_idx].select_score, cur_idx,
                             &evaluated[best_idx].select_score, best_idx)) {
            best_idx = cur_idx;
        }
    }
    return best_idx;
}

#ifdef MEMETIC_AB_HOST_STUB
static void print_genome(FILE *f, const int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%d", genome[i]);
    }
}

static const char *mode_name(ehw45_mode_t mode) {
    return mode == EHW45_BALDWINIAN ? "baldwinian" : "lamarckian";
}

static void write_curve_row(FILE *f, ehw45_mode_t mode, int gen, const mem_eval_t *best) {
    fprintf(f, "%s,%d,%d,%.4f,%d,%d,%d,%d,",
            mode_name(mode), gen, best->post_score.correct, best->post_score.correct / 40.0,
            best->post_score.sse, best->post_score.fitness,
            best->pre_score.correct, best->post_score.correct);
    print_genome(f, best->genome_for_next);
    fputc(',', f);
    print_genome(f, best->pre_genome);
    fputc(',', f);
    print_genome(f, best->post_genome);
    fputc('\n', f);
}
#endif

static int8_t pop_a[EHW45_POP][EHW_GENOME_LEN];
static int8_t pop_b[EHW45_POP][EHW_GENOME_LEN];
static mem_eval_t evaluated[EHW45_POP];
static int ranked_idx[EHW45_POP];

static int run_mode_ga(ehw45_mode_t mode,
#ifdef MEMETIC_AB_HOST_STUB
    FILE *curve
#else
    uint32_t prefix, int *best_correct_out
#endif
) {
    uint32_t seed_offset = mode == EHW45_BALDWINIAN ? 101u : 202u;
    mem_rng_t rng = { (uint32_t)(EHW45_SEED + seed_offset) };
    mem_seed_population(&rng, EHW45_POP, EHW45_INIT_SPAN, pop_a);
    int8_t (*pop)[EHW_GENOME_LEN] = pop_a;
    int8_t (*next)[EHW_GENOME_LEN] = pop_b;

    mem_eval_t best;
    mem_eval_candidate_tu(pop[0], EHW45_SEED, mode, &best);
    int first_40 = -1;

    for (int gen = 0; gen <= EHW45_GENS; gen++) {
        for (int i = 0; i < EHW45_POP; i++)
            mem_eval_candidate_tu(pop[i], EHW45_SEED + gen * 1009 + i, mode, &evaluated[i]);
        sort_ranked(evaluated, ranked_idx);
        int top = ranked_idx[0];
        if (evaluated[top].select_score.fitness > best.select_score.fitness)
            best = evaluated[top];
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;

#ifdef MEMETIC_AB_HOST_STUB
        write_curve_row(curve, mode, gen, &best);
#else
        publish(prefix | 0x00100000u | ((uint32_t)(gen & 0xFF) << 8) |
                (uint32_t)(best.post_score.correct & 0xFF));
        publish(prefix | 0x00200000u | ((uint32_t)best.post_score.sse & 0xFFFFu));
        publish(prefix | 0x00300000u | ((uint32_t)(ranked_idx[0] & 0xFF) << 16) |
                (uint32_t)(evaluated[top].post_score.correct & 0xFF));
#endif
        if (gen == EHW45_GENS) break;

        for (int i = 0; i < EHW45_ELITES; i++)
            mem_copy_genome(next[i], evaluated[ranked_idx[i]].genome_for_next);
        int nnext = EHW45_ELITES;
        while (nnext < EHW45_POP) {
            int p1_idx = tournament_pick(evaluated, ranked_idx, &rng);
            int8_t child[EHW_GENOME_LEN];
            if (mem_rng_chance(&rng, EHW45_CROSS_PPM)) {
                int p2_idx = tournament_pick(evaluated, ranked_idx, &rng);
                mem_crossover(evaluated[p1_idx].genome_for_next,
                              evaluated[p2_idx].genome_for_next, child, &rng);
            } else {
                mem_copy_genome(child, evaluated[p1_idx].genome_for_next);
            }
            mem_mutate(child, next[nnext], &rng, EHW45_MUT_PPM, EHW45_MUT_STEP);
            nnext++;
        }
        int8_t (*tmp)[EHW_GENOME_LEN] = pop;
        pop = next;
        next = tmp;
    }

#ifndef MEMETIC_AB_HOST_STUB
    publish(prefix | 0x00400000u | (uint32_t)(EHW45_ADAPT_EPOCHS & 0xFF));
    publish(prefix | 0x00F00000u | (uint32_t)(best.post_score.correct & 0xFF));
    *best_correct_out = best.post_score.correct;
#endif
    return first_40;
}

int main(
#ifdef MEMETIC_AB_HOST_STUB
    int argc, char **argv
#else
    void
#endif
) {
#ifdef MEMETIC_AB_HOST_STUB
    const char *curve_csv = "runs/ehw4_5_memetic_ab_curve.csv";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--curve-csv") && i + 1 < argc) curve_csv = argv[++i];
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 2;
        }
    }
    FILE *curve = fopen(curve_csv, "w");
    if (!curve) {
        perror(curve_csv);
        return 1;
    }
    fprintf(curve, "mode,gen,best_correct,best_acc,best_sse,best_fitness,pre_correct,post_correct,genome,pre_genome,post_genome\n");
    int b_first_40 = run_mode_ga(EHW45_BALDWINIAN, curve);
    int l_first_40 = run_mode_ga(EHW45_LAMARCKIAN, curve);
    fclose(curve);
    printf("PASS: EHW-4.5 train-unit A/B curves wrote %s baldwinian_first_40=", curve_csv);
    if (b_first_40 >= 0) printf("%d", b_first_40);
    else printf("none");
    printf(" lamarckian_first_40=");
    if (l_first_40 >= 0) printf("%d\n", l_first_40);
    else printf("none\n");
    return 0;
#else
    publish(0xF4000045u);
    publish(0xF5400000u | (uint32_t)(EHW45_ADAPT_EPOCHS & 0xFF));
    int b_correct = 0, l_correct = 0;
    (void)run_mode_ga(EHW45_BALDWINIAN, 0xF5000000u, &b_correct);
    (void)run_mode_ga(EHW45_LAMARCKIAN, 0xF6000000u, &l_correct);
    uint32_t final_word = 0xF7F00000u |
        ((uint32_t)(b_correct & 0xFF) << 8) |
        (uint32_t)(l_correct & 0xFF);
    publish(final_word);
    while (1) {
        MBOX = final_word;
        for (volatile uint32_t d = 0; d < 400000u; d++) { }
    }
    return 0;
#endif
}
