/* EHW-4.6a firmware prep: compile-time parameter sweep for memetic GA.
 *
 * This is the no-static-change sweep rung.  One firmware image contains a small
 * table of parameter points and runs each point sequentially through the
 * EHW-4.3/4.4 board-verified train-unit MMIO path at 0xF0000800.
 *
 * The GA code reads an ehw46_param_t; EHW-4.6b can replace the compile-time table
 * with a framebuf/AXI-loaded parameter source without rewriting the GA evaluator.
 *
 * Host mode (-DMEMETIC_SWEEP_HOST_STUB) emits one summary CSV row per
 * point/mode.  The host gate compares it byte-for-byte against memetic_eval.c
 * reference runs for the same table.
 */

#include "memetic_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef MEMETIC_SWEEP_HOST_STUB
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

#define EHW46_MODE_BALD 0x01u
#define EHW46_MODE_LAM  0x02u
#define EHW46_MAX_POP   32
#define EHW46_NPOINTS   12

typedef enum {
    EHW46_BALDWINIAN = 0,
    EHW46_LAMARCKIAN = 1,
} ehw46_mode_t;

typedef struct {
    uint8_t  point;
    uint8_t  mode_mask;
    uint8_t  population;
    uint8_t  generations;
    uint8_t  adapt_epochs;
    uint8_t  elites;
    uint8_t  tournament;
    int8_t   init_span;
    int8_t   mutation_step;
    uint32_t seed;
    int      crossover_ppm;
    int      mutation_ppm;
} ehw46_param_t;

typedef struct {
    ehw_score_t best_score;
    int8_t best_genome[EHW_GENOME_LEN];
    int first_40;
} ehw46_result_t;

static const ehw46_param_t EHW46_TABLE[EHW46_NPOINTS] = {
    {  0, EHW46_MODE_BALD | EHW46_MODE_LAM,  8,  8, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  1, EHW46_MODE_BALD | EHW46_MODE_LAM,  8, 16, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  2, EHW46_MODE_BALD | EHW46_MODE_LAM,  8, 32, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  3, EHW46_MODE_BALD | EHW46_MODE_LAM, 16,  8, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  4, EHW46_MODE_BALD | EHW46_MODE_LAM, 16, 16, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  5, EHW46_MODE_BALD | EHW46_MODE_LAM, 16, 32, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  6, EHW46_MODE_BALD | EHW46_MODE_LAM, 32,  8, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  7, EHW46_MODE_BALD | EHW46_MODE_LAM, 32, 16, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  8, EHW46_MODE_BALD | EHW46_MODE_LAM, 32, 32, 1, 2, 3, 32, 8, 3, 700000, 30000 },
    {  9, EHW46_MODE_BALD | EHW46_MODE_LAM, 16,  8, 2, 2, 3, 32, 8, 3, 700000, 30000 },
    { 10, EHW46_MODE_BALD | EHW46_MODE_LAM, 16, 16, 2, 2, 3, 32, 8, 3, 700000, 30000 },
    { 11, EHW46_MODE_BALD | EHW46_MODE_LAM, 16, 32, 2, 2, 3, 32, 8, 3, 700000, 30000 },
};

#ifdef MEMETIC_SWEEP_HOST_STUB
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

static void publish(uint32_t word) {
    MBOX = word;
    for (volatile uint32_t d = 0; d < 4000u; d++) { }
}
#endif

#ifdef MEMETIC_SWEEP_HOST_STUB
static const char *mode_name(ehw46_mode_t mode) {
    return mode == EHW46_BALDWINIAN ? "baldwinian" : "lamarckian";
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

static void mem_eval_candidate_tu(const ehw46_param_t *p, ehw46_mode_t mode,
                                  const int8_t genome[EHW_GENOME_LEN],
                                  int seed, mem_eval_t *out) {
    out->pre_score = ehw_evaluate(genome);
    mem_copy_genome(out->pre_genome, genome);
    mem_adapt_tu(genome, p->adapt_epochs, seed, out->post_genome);
    out->post_score = ehw_evaluate(out->post_genome);
    out->select_score = out->post_score;
    if (mode == EHW46_BALDWINIAN) mem_copy_genome(out->genome_for_next, genome);
    else mem_copy_genome(out->genome_for_next, out->post_genome);
}

static int better_score_idx(const ehw_score_t *as, int ai, const ehw_score_t *bs, int bi) {
    if (as->fitness != bs->fitness) return as->fitness > bs->fitness;
    return ai < bi;
}

static void sort_ranked(const mem_eval_t evaluated[EHW46_MAX_POP],
                        int ranked_idx[EHW46_MAX_POP], int n) {
    for (int i = 0; i < n; i++) ranked_idx[i] = i;
    for (int i = 1; i < n; i++) {
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

static int tournament_pick(const mem_eval_t evaluated[EHW46_MAX_POP],
                           const int ranked_idx[EHW46_MAX_POP],
                           const ehw46_param_t *p, mem_rng_t *rng) {
    int best_pos = mem_rng_range(rng, p->population);
    int best_idx = ranked_idx[best_pos];
    for (int i = 1; i < p->tournament; i++) {
        int cur_pos = mem_rng_range(rng, p->population);
        int cur_idx = ranked_idx[cur_pos];
        if (better_score_idx(&evaluated[cur_idx].select_score, cur_idx,
                             &evaluated[best_idx].select_score, best_idx)) {
            best_idx = cur_idx;
        }
    }
    return best_idx;
}

static int8_t pop_a[EHW46_MAX_POP][EHW_GENOME_LEN];
static int8_t pop_b[EHW46_MAX_POP][EHW_GENOME_LEN];
static mem_eval_t evaluated[EHW46_MAX_POP];
static int ranked_idx[EHW46_MAX_POP];
static ehw46_result_t results[EHW46_NPOINTS][2];

#ifdef MEMETIC_SWEEP_HOST_STUB
static void write_summary_row(FILE *f, const ehw46_param_t *p, ehw46_mode_t mode,
                              const ehw46_result_t *r) {
    fprintf(f, "%u,%s,%u,%u,%u,%u,%d,%d,%d,",
            (unsigned)p->point, mode_name(mode), (unsigned)p->population,
            (unsigned)p->generations, (unsigned)p->adapt_epochs, (unsigned)p->seed,
            r->best_score.correct, r->best_score.sse, r->best_score.fitness);
    if (r->first_40 >= 0) fprintf(f, "%d", r->first_40);
    fputc(',', f);
    fprintf(f, "%d\n", mem_sat_count(r->best_genome));
}
#else
static uint32_t result_word0(uint8_t point, ehw46_mode_t mode, const ehw46_result_t *r) {
    uint32_t first = (r->first_40 >= 0) ? (uint32_t)(r->first_40 & 0x3F) : 0x3Fu;
    return 0xF8000000u | ((uint32_t)(point & 0x3F) << 18) |
           ((uint32_t)(mode & 1) << 17) | (first << 8) |
           (uint32_t)(r->best_score.correct & 0xFF);
}

static uint32_t result_word1(uint8_t point, ehw46_mode_t mode, const ehw46_result_t *r) {
    return 0xF9000000u | ((uint32_t)(point & 0x3F) << 18) |
           ((uint32_t)(mode & 1) << 17) |
           (uint32_t)(r->best_score.sse & 0xFFFF);
}
#endif

static void run_mode_ga(const ehw46_param_t *p, ehw46_mode_t mode, ehw46_result_t *out) {
    uint32_t seed_offset = mode == EHW46_BALDWINIAN ? 101u : 202u;
    mem_rng_t rng = { p->seed + seed_offset };
    mem_seed_population(&rng, p->population, p->init_span, pop_a);
    int8_t (*pop)[EHW_GENOME_LEN] = pop_a;
    int8_t (*next)[EHW_GENOME_LEN] = pop_b;

    mem_eval_t best;
    mem_eval_candidate_tu(p, mode, pop[0], (int)p->seed, &best);
    int first_40 = -1;

    for (int gen = 0; gen <= p->generations; gen++) {
        for (int i = 0; i < p->population; i++)
            mem_eval_candidate_tu(p, mode, pop[i], (int)(p->seed + gen * 1009u + (uint32_t)i),
                                  &evaluated[i]);
        sort_ranked(evaluated, ranked_idx, p->population);
        int top = ranked_idx[0];
        if (evaluated[top].select_score.fitness > best.select_score.fitness)
            best = evaluated[top];
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;

#ifndef MEMETIC_SWEEP_HOST_STUB
        publish(0xF8100000u | ((uint32_t)(p->point & 0x3F) << 16) |
                ((uint32_t)(mode & 1) << 15) | ((uint32_t)(gen & 0x7F) << 8) |
                (uint32_t)(best.post_score.correct & 0x3F));
#endif
        if (gen == p->generations) break;

        for (int i = 0; i < p->elites; i++)
            mem_copy_genome(next[i], evaluated[ranked_idx[i]].genome_for_next);
        int nnext = p->elites;
        while (nnext < p->population) {
            int p1_idx = tournament_pick(evaluated, ranked_idx, p, &rng);
            int8_t child[EHW_GENOME_LEN];
            if (mem_rng_chance(&rng, p->crossover_ppm)) {
                int p2_idx = tournament_pick(evaluated, ranked_idx, p, &rng);
                mem_crossover(evaluated[p1_idx].genome_for_next,
                              evaluated[p2_idx].genome_for_next, child, &rng);
            } else {
                mem_copy_genome(child, evaluated[p1_idx].genome_for_next);
            }
            mem_mutate(child, next[nnext], &rng, p->mutation_ppm, p->mutation_step);
            nnext++;
        }
        int8_t (*tmp)[EHW_GENOME_LEN] = pop;
        pop = next;
        next = tmp;
    }

    out->best_score = best.post_score;
    mem_copy_genome(out->best_genome, best.post_genome);
    out->first_40 = first_40;
}

static void run_sweep(
#ifdef MEMETIC_SWEEP_HOST_STUB
    FILE *summary
#else
    void
#endif
) {
    for (int pi = 0; pi < EHW46_NPOINTS; pi++) {
        const ehw46_param_t *p = &EHW46_TABLE[pi];
#ifndef MEMETIC_SWEEP_HOST_STUB
        publish(0xF8200000u | ((uint32_t)(p->point & 0x3F) << 8) |
                (uint32_t)(p->mode_mask & 0xFF));
#endif
        if (p->mode_mask & EHW46_MODE_BALD) {
            run_mode_ga(p, EHW46_BALDWINIAN, &results[pi][EHW46_BALDWINIAN]);
#ifdef MEMETIC_SWEEP_HOST_STUB
            write_summary_row(summary, p, EHW46_BALDWINIAN, &results[pi][EHW46_BALDWINIAN]);
#endif
        }
        if (p->mode_mask & EHW46_MODE_LAM) {
            run_mode_ga(p, EHW46_LAMARCKIAN, &results[pi][EHW46_LAMARCKIAN]);
#ifdef MEMETIC_SWEEP_HOST_STUB
            write_summary_row(summary, p, EHW46_LAMARCKIAN, &results[pi][EHW46_LAMARCKIAN]);
#endif
        }
    }
}

int main(
#ifdef MEMETIC_SWEEP_HOST_STUB
    int argc, char **argv
#else
    void
#endif
) {
#ifdef MEMETIC_SWEEP_HOST_STUB
    const char *summary_csv = "runs/ehw4_6a_sweep_summary.csv";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--summary-csv") && i + 1 < argc) summary_csv = argv[++i];
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 2;
        }
    }
    FILE *summary = fopen(summary_csv, "w");
    if (!summary) {
        perror(summary_csv);
        return 1;
    }
    fprintf(summary, "point,mode,population,generations,adapt_epochs,seed,best_correct,best_sse,best_fitness,first_40,sat_count\n");
    run_sweep(summary);
    fclose(summary);
    printf("PASS: EHW-4.6a sweep summary wrote %s (%d points)\n", summary_csv, EHW46_NPOINTS);
    return 0;
#else
    publish(0xF8000046u);
    publish(0xF8300000u | (uint32_t)EHW46_NPOINTS);
    run_sweep();
    publish(0xF8F00000u | (uint32_t)EHW46_NPOINTS);
    while (1) {
        for (int pi = 0; pi < EHW46_NPOINTS; pi++) {
            const ehw46_param_t *p = &EHW46_TABLE[pi];
            if (p->mode_mask & EHW46_MODE_BALD) {
                publish(result_word0(p->point, EHW46_BALDWINIAN, &results[pi][EHW46_BALDWINIAN]));
                publish(result_word1(p->point, EHW46_BALDWINIAN, &results[pi][EHW46_BALDWINIAN]));
            }
            if (p->mode_mask & EHW46_MODE_LAM) {
                publish(result_word0(p->point, EHW46_LAMARCKIAN, &results[pi][EHW46_LAMARCKIAN]));
                publish(result_word1(p->point, EHW46_LAMARCKIAN, &results[pi][EHW46_LAMARCKIAN]));
            }
        }
    }
    return 0;
#endif
}
