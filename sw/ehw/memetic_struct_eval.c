#include "memetic_struct_kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ms_options_t opt;
    const char *curve_csv;
    const char *summary_csv;
} cli_t;

static int rate_to_ppm(const char *s) {
    double v = strtod(s, NULL);
    int ppm = (int)(v * 1000000.0 + 0.5);
    if (ppm < 0) return 0;
    if (ppm > 1000000) return 1000000;
    return ppm;
}

static void defaults(cli_t *cli) {
    cli->opt.seed = 3;
    cli->opt.population = 16;
    cli->opt.generations = 32;
    cli->opt.adapt_epochs = 1;
    cli->opt.lr_shift = MEM_LR_SHIFT;
    cli->opt.init_span = 32;
    cli->opt.elites = 2;
    cli->opt.tournament = 3;
    cli->opt.crossover_ppm = 700000;
    cli->opt.mutation_ppm = 30000;
    cli->opt.mutation_step = 8;
    cli->opt.struct_mutation_ppm = 250000;
    cli->opt.struct_init_mutation_ppm = 50000;
    cli->opt.struct_sel_mutation_ppm = 30000;
    cli->opt.feature_min_balance = MS_DEFAULT_FEATURE_MIN_BALANCE;
    cli->opt.feature_penalty = MS_DEFAULT_FEATURE_PENALTY;
    cli->curve_csv = "runs/ehw5_1_struct_c_curves.csv";
    cli->summary_csv = "runs/ehw5_1_struct_c_summary.csv";
}

static int parse_int(int *dst, int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return -1;
    *dst = atoi(argv[++(*i)]);
    return 0;
}

static int parse_args(cli_t *cli, int argc, char **argv) {
    defaults(cli);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) { if (parse_int(&cli->opt.seed, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--population")) { if (parse_int(&cli->opt.population, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--generations")) { if (parse_int(&cli->opt.generations, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--adapt-epochs")) { if (parse_int(&cli->opt.adapt_epochs, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--lr-shift")) { if (parse_int(&cli->opt.lr_shift, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--init-span")) { if (parse_int(&cli->opt.init_span, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--elites")) { if (parse_int(&cli->opt.elites, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--tournament")) { if (parse_int(&cli->opt.tournament, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--mutation-step")) { if (parse_int(&cli->opt.mutation_step, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--struct-mutation-ppm")) { if (parse_int(&cli->opt.struct_mutation_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--struct-init-mutation-ppm")) { if (parse_int(&cli->opt.struct_init_mutation_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--struct-sel-mutation-ppm")) { if (parse_int(&cli->opt.struct_sel_mutation_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--feature-min-balance")) { if (parse_int(&cli->opt.feature_min_balance, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--feature-penalty")) { if (parse_int(&cli->opt.feature_penalty, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--crossover-rate")) { if (++i >= argc) return -1; cli->opt.crossover_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--mutation-rate")) { if (++i >= argc) return -1; cli->opt.mutation_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--curve-csv")) { if (++i >= argc) return -1; cli->curve_csv = argv[i]; }
        else if (!strcmp(argv[i], "--summary-csv")) { if (++i >= argc) return -1; cli->summary_csv = argv[i]; }
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return -1;
        }
    }
    if (cli->opt.population <= 0 || cli->opt.population > MS_MAX_POP) return -2;
    if (cli->opt.elites <= 0 || cli->opt.elites > cli->opt.population) return -2;
    return 0;
}

static const char *mode_name(ms_mode_t mode) {
    switch (mode) {
        case MS_MODE_LAMARCKIAN: return "hybrid_lamarckian";
        case MS_MODE_LAMARCKIAN_PRESSURE: return "hybrid_lamarckian_pressure";
        case MS_MODE_NO_ADAPT: return "hybrid_no_adapt";
    }
    return "?";
}

static const char *coupling_name(ms_coupling_t coupling) {
    switch (coupling) {
        case MS_COUPLING_REPLACE_X3: return "replace_x3";
        case MS_COUPLING_GATE_X3: return "gate_x3";
        case MS_COUPLING_BIAS_X3: return "bias_x3";
    }
    return "?";
}

static void copy_sr(uint8_t dst[SR_GENOME_LEN], const uint8_t src[SR_GENOME_LEN]) {
    memcpy(dst, src, SR_GENOME_LEN);
}

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

static uint8_t phi_for_x(const uint8_t sr_genome[SR_GENOME_LEN],
                         const int8_t x[EHW_NIN],
                         const sr_fault_t *fault) {
    int row = (x[0] >= 8) | ((x[1] >= 8) << 1) | ((x[2] >= 8) << 2);
    return sr_eval_row(sr_genome, row, fault);
}

static void transformed_x(const uint8_t sr_genome[SR_GENOME_LEN],
                          const int8_t x[EHW_NIN],
                          ms_coupling_t coupling,
                          const sr_fault_t *fault,
                          int8_t out[EHW_NIN]) {
    uint8_t phi = phi_for_x(sr_genome, x, fault);
    out[0] = x[0];
    out[1] = x[1];
    out[2] = x[2];
    if (coupling == MS_COUPLING_REPLACE_X3) out[3] = phi ? 16 : 0;
    else if (coupling == MS_COUPLING_GATE_X3) out[3] = phi ? x[3] : 0;
    else out[3] = (int8_t)ms_clamp_i8((int)x[3] + (phi ? 8 : -8));
}

static uint64_t feature_mask_for_dataset(const uint8_t sr_genome[SR_GENOME_LEN],
                                         const sr_fault_t *fault) {
    uint64_t mask = 0;
    for (int i = 0; i < EHW_NTEST; i++) {
        if (phi_for_x(sr_genome, EHW_TEST_X[i], fault)) mask |= 1ull << i;
    }
    return mask;
}

static uint8_t forward_master_dataset(const int32_t w1[EHW_NH][EHW_NIN],
                                      const int32_t w2[EHW_NOUT][EHW_NH],
                                      const int8_t x[EHW_NIN],
                                      int32_t z1[EHW_NH],
                                      int32_t h[EHW_NH],
                                      int32_t z2[EHW_NOUT],
                                      int32_t y[EHW_NOUT]) {
    return mem_forward_master(w1, w2, x, z1, h, z2, y);
}

static ehw_score_t evaluate_dataset(const int8_t weight[EHW_GENOME_LEN],
                                    const uint8_t sr_genome[SR_GENOME_LEN],
                                    ms_coupling_t coupling,
                                    const sr_fault_t *fault) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    mem_master_from_genome(weight, w1, w2);
    ehw_score_t score = {0, 0, 0};
    for (int i = 0; i < EHW_NTEST; i++) {
        int8_t x[EHW_NIN];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        transformed_x(sr_genome, EHW_TEST_X[i], coupling, fault, x);
        uint8_t pred = forward_master_dataset(w1, w2, x, z1, h, z2, y);
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

static int32_t sgd_epoch_dataset(int32_t w1[EHW_NH][EHW_NIN],
                                 int32_t w2[EHW_NOUT][EHW_NH],
                                 const uint8_t sr_genome[SR_GENOME_LEN],
                                 ms_coupling_t coupling,
                                 const int order[EHW_NTEST],
                                 int lr_shift) {
    int32_t sse = 0;
    for (int oi = 0; oi < EHW_NTEST; oi++) {
        int idx = order[oi];
        int8_t x[EHW_NIN];
        transformed_x(sr_genome, EHW_TEST_X[idx], coupling, &SR_FAULT_NONE_OBJ, x);
        uint8_t label = EHW_TEST_Y[idx];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        forward_master_dataset(w1, w2, x, z1, h, z2, y);

        int32_t err[EHW_NOUT];
        for (int k = 0; k < EHW_NOUT; k++) {
            int32_t target = (k == label) ? MEM_ONE : 0;
            err[k] = mem_clamp32(y[k] - target, -MEM_ERR_CLAMP, MEM_ERR_CLAMP - 1);
            sse += mem_qmul(err[k], err[k]);
        }

        int32_t d2[EHW_NOUT];
        for (int k = 0; k < EHW_NOUT; k++) {
            d2[k] = mem_clamp32(mem_qmul(err[k], mem_leaky_d(z2[k])),
                                -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        }

        int32_t dw2[EHW_NOUT][EHW_NH];
        for (int i = 0; i < EHW_NOUT; i++)
            for (int j = 0; j < EHW_NH; j++)
                dw2[i][j] = mem_qmul(d2[i], h[j]);

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
        for (int i = 0; i < EHW_NH; i++) {
            d1[i] = mem_clamp32(mem_qmul(w2td2[i], mem_leaky_d(z1[i])),
                                -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        }

        int32_t dw1[EHW_NH][EHW_NIN];
        for (int i = 0; i < EHW_NH; i++) {
            for (int j = 0; j < EHW_NIN; j++) {
                int32_t x_q88 = (int32_t)x[j] << MEM_XSHIFT;
                dw1[i][j] = mem_qmul(d1[i], x_q88);
            }
        }

        for (int i = 0; i < EHW_NOUT; i++) {
            for (int j = 0; j < EHW_NH; j++) {
                w2[i][j] = mem_sat16(w2[i][j] - (dw2[i][j] >> lr_shift));
            }
        }
        for (int i = 0; i < EHW_NH; i++) {
            for (int j = 0; j < EHW_NIN; j++) {
                w1[i][j] = mem_sat16(w1[i][j] - (dw1[i][j] >> lr_shift));
            }
        }
    }
    return sse;
}

static int32_t adapt_dataset(const int8_t weight[EHW_GENOME_LEN],
                             const uint8_t sr_genome[SR_GENOME_LEN],
                             ms_coupling_t coupling,
                             int epochs,
                             int lr_shift,
                             int seed,
                             int8_t out_weight[EHW_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    int order[EHW_NTEST];
    int32_t last_sse = 0;
    mem_master_from_genome(weight, w1, w2);
    for (int i = 0; i < EHW_NTEST; i++) order[i] = i;
    mem_rng_t rng = { (uint32_t)seed };
    for (int ep = 0; ep < epochs; ep++) {
        for (int i = EHW_NTEST - 1; i > 0; i--) {
            int j = mem_rng_range(&rng, i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        last_sse = sgd_epoch_dataset(w1, w2, sr_genome, coupling, order, lr_shift);
    }
    mem_genome_from_master(w1, w2, out_weight);
    return last_sse;
}

static void eval_struct_candidate(ms_mode_t mode,
                                  ms_coupling_t coupling,
                                  const uint8_t sr_genome[SR_GENOME_LEN],
                                  const int8_t weight[EHW_GENOME_LEN],
                                  const ms_options_t *opt,
                                  int seed,
                                  ms_eval_t *out) {
    out->pre_score = evaluate_dataset(weight, sr_genome, coupling, &SR_FAULT_NONE_OBJ);
    mem_copy_genome(out->pre_weight, weight);
    if (mode == MS_MODE_NO_ADAPT) {
        mem_copy_genome(out->post_weight, weight);
        out->post_score = out->pre_score;
    } else {
        adapt_dataset(weight, sr_genome, coupling, opt->adapt_epochs, opt->lr_shift,
                      seed, out->post_weight);
        out->post_score = evaluate_dataset(out->post_weight, sr_genome, coupling, &SR_FAULT_NONE_OBJ);
    }
    copy_sr(out->sr, sr_genome);
    out->feature_mask = feature_mask_for_dataset(sr_genome, &SR_FAULT_NONE_OBJ);
    out->feature_ones = ms_feature_ones(out->feature_mask);
    out->feature_penalty = (mode == MS_MODE_LAMARCKIAN_PRESSURE)
        ? ms_structural_penalty(out->feature_mask, opt) : 0;
    out->select_score = out->post_score;
    out->select_score.fitness -= out->feature_penalty;
    if (mode == MS_MODE_NO_ADAPT) mem_copy_genome(out->weight_for_next, weight);
    else mem_copy_genome(out->weight_for_next, out->post_weight);
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
                             mem_rng_t *rng,
                             int init_ppm,
                             int sel_ppm) {
    copy_sr(out, in);
    int changed = 0;
    for (int idx = 0; idx <= 4; idx++) {
        int bits = idx == 4 ? 8 : 4;
        uint8_t value = out[idx];
        for (int bit = 0; bit < bits; bit++) {
            if (mem_rng_chance(rng, init_ppm)) {
                value ^= (uint8_t)(1u << bit);
                changed = 1;
            }
        }
        out[idx] = (uint8_t)(value & ((1u << bits) - 1u));
    }
    for (int idx = 5; idx < SR_GENOME_LEN; idx++) {
        int limit = idx < 13 ? 5 : 4;
        if (mem_rng_chance(rng, sel_ppm)) {
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

static void mutate_hybrid(const ms_candidate_t *in,
                          ms_candidate_t *out,
                          mem_rng_t *rng,
                          const ms_options_t *opt) {
    copy_candidate(out, in);
    if (mem_rng_chance(rng, opt->struct_mutation_ppm)) {
        sr_mutate_genome(in->sr, out->sr, rng, opt->struct_init_mutation_ppm,
                         opt->struct_sel_mutation_ppm);
    }
    mem_mutate(in->weight, out->weight, rng, opt->mutation_ppm, opt->mutation_step);
}

static void crossover_hybrid(const ms_candidate_t *a,
                             const ms_candidate_t *b,
                             ms_candidate_t *out,
                             mem_rng_t *rng) {
    sr_crossover_genome(a->sr, b->sr, out->sr, rng);
    mem_crossover(a->weight, b->weight, out->weight, rng);
}

static void seed_hybrid_population(mem_rng_t *rng,
                                   const ms_options_t *opt,
                                   ms_candidate_t pop[MS_MAX_POP]) {
    int weight_seed_count = opt->population < 8 ? opt->population : 8;
    int8_t weight_seeds[8][EHW_GENOME_LEN];
    uint8_t sr_seeds[4][SR_GENOME_LEN];
    mem_seed_population(rng, weight_seed_count, opt->init_span, weight_seeds);
    copy_sr(sr_seeds[0], MS_SR_MAJORITY);
    copy_sr(sr_seeds[1], MS_SR_REPAIR);
    sr_scaffold_genome(rng, 0, sr_seeds[2]);
    sr_scaffold_genome(rng, 1, sr_seeds[3]);

    int n = 0;
    for (int s = 0; s < 4; s++) {
        for (int w = 0; w < 2 && w < weight_seed_count; w++) {
            if (n < opt->population) {
                copy_sr(pop[n].sr, sr_seeds[s]);
                mem_copy_genome(pop[n].weight, weight_seeds[w]);
                n++;
            }
        }
    }
    while (n < opt->population) {
        int s = mem_rng_range(rng, 4);
        sr_mutate_genome(sr_seeds[s], pop[n].sr, rng, opt->struct_init_mutation_ppm,
                         opt->struct_sel_mutation_ppm);
        int w = mem_rng_range(rng, weight_seed_count);
        int8_t tmp[EHW_GENOME_LEN];
        mem_mutate(weight_seeds[w], tmp, rng, opt->mutation_ppm, opt->mutation_step);
        mem_copy_genome(pop[n].weight, tmp);
        n++;
    }
}

static int better_score_idx(const ehw_score_t *as, int ai, const ehw_score_t *bs, int bi) {
    if (as->fitness != bs->fitness) return as->fitness > bs->fitness;
    return ai < bi;
}

static int tournament_pick_struct(const ms_eval_t evaluated[MS_MAX_POP],
                                  const int ranked_idx[MS_MAX_POP],
                                  const ms_options_t *opt,
                                  mem_rng_t *rng) {
    int best_pos = mem_rng_range(rng, opt->population);
    int best_idx = ranked_idx[best_pos];
    for (int i = 1; i < opt->tournament; i++) {
        int cur_pos = mem_rng_range(rng, opt->population);
        int cur_idx = ranked_idx[cur_pos];
        if (better_score_idx(&evaluated[cur_idx].select_score, cur_idx,
                             &evaluated[best_idx].select_score, best_idx)) {
            best_idx = cur_idx;
        }
    }
    return best_idx;
}

static void sort_ranked_struct(const ms_eval_t evaluated[MS_MAX_POP],
                               int ranked_idx[MS_MAX_POP],
                               int n) {
    for (int i = 0; i < n; i++) ranked_idx[i] = i;
    for (int i = 1; i < n; i++) {
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

static void write_curve_row(FILE *f,
                            ms_mode_t mode,
                            ms_coupling_t coupling,
                            int gen,
                            int top_index,
                            const ms_eval_t *best) {
    fprintf(f, "%s,%s,%d,%d,%d,%d,%d,%010llx,%d,%d,%d,",
            mode_name(mode), coupling_name(coupling), gen,
            best->post_score.correct, best->post_score.sse,
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

static void write_weight_curve_row(FILE *f,
                                   int gen,
                                   int top_index,
                                   const mem_eval_t *best) {
    (void)top_index;
    fprintf(f, "weight_only_lamarckian,none,%d,%d,%d,%d,%d,,,,,,",
            gen, best->post_score.correct, best->post_score.sse,
            best->post_score.fitness, best->select_score.fitness);
    print_i8_genome(f, best->pre_genome);
    fputc(',', f);
    print_i8_genome(f, best->post_genome);
    fputc('\n', f);
}

static void run_hybrid_mode(ms_mode_t mode,
                            ms_coupling_t coupling,
                            const ms_options_t *opt,
                            FILE *curve,
                            ms_result_t *result) {
    int seed_offset = (mode == MS_MODE_LAMARCKIAN) ? 303 :
        (mode == MS_MODE_NO_ADAPT ? 404 : 505);
    mem_rng_t rng = { (uint32_t)(opt->seed + seed_offset) };
    ms_candidate_t pop[MS_MAX_POP], next[MS_MAX_POP];
    ms_eval_t evaluated[MS_MAX_POP];
    int ranked_idx[MS_MAX_POP];
    seed_hybrid_population(&rng, opt, pop);
    eval_struct_candidate(mode, coupling, pop[0].sr, pop[0].weight, opt, opt->seed, &evaluated[0]);
    ms_eval_t best = evaluated[0];
    int first_40 = -1;

    for (int gen = 0; gen <= opt->generations; gen++) {
        for (int i = 0; i < opt->population; i++) {
            eval_struct_candidate(mode, coupling, pop[i].sr, pop[i].weight, opt,
                                  opt->seed + gen * 1009 + i, &evaluated[i]);
        }
        sort_ranked_struct(evaluated, ranked_idx, opt->population);
        int top = ranked_idx[0];
        if (evaluated[top].select_score.fitness > best.select_score.fitness) {
            best = evaluated[top];
        }
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;
        write_curve_row(curve, mode, coupling, gen, top, &best);
        if (gen == opt->generations) break;

        for (int i = 0; i < opt->elites; i++) {
            int src = ranked_idx[i];
            copy_sr(next[i].sr, evaluated[src].sr);
            mem_copy_genome(next[i].weight, evaluated[src].weight_for_next);
        }
        int nnext = opt->elites;
        while (nnext < opt->population) {
            int p1 = tournament_pick_struct(evaluated, ranked_idx, opt, &rng);
            ms_candidate_t parent, child;
            copy_sr(parent.sr, evaluated[p1].sr);
            mem_copy_genome(parent.weight, evaluated[p1].weight_for_next);
            if (mem_rng_chance(&rng, opt->crossover_ppm)) {
                int p2 = tournament_pick_struct(evaluated, ranked_idx, opt, &rng);
                ms_candidate_t mate;
                copy_sr(mate.sr, evaluated[p2].sr);
                mem_copy_genome(mate.weight, evaluated[p2].weight_for_next);
                crossover_hybrid(&parent, &mate, &child, &rng);
            } else {
                copy_candidate(&child, &parent);
            }
            mutate_hybrid(&child, &next[nnext], &rng, opt);
            nnext++;
        }
        memcpy(pop, next, sizeof(pop));
    }

    sr_fault_t fault_a1 = SR_FAULT_DISABLE_A1;
    ehw_score_t degraded = evaluate_dataset(best.post_weight, best.sr, coupling, &fault_a1);
    ehw_score_t repaired = evaluate_dataset(best.post_weight, MS_SR_REPAIR, coupling, &fault_a1);
    result->mode = mode_name(mode);
    result->coupling = coupling_name(coupling);
    result->best_score = best.post_score;
    result->first_40 = first_40;
    copy_sr(result->sr, best.sr);
    mem_copy_genome(result->pre_weight, best.pre_weight);
    mem_copy_genome(result->post_weight, best.post_weight);
    result->feature_mask = best.feature_mask;
    result->feature_ones = best.feature_ones;
    result->feature_penalty = best.feature_penalty;
    result->degraded_correct = degraded.correct;
    result->repaired_correct = repaired.correct;
}

static void sort_ranked_mem(const mem_eval_t evaluated[MS_MAX_POP], int ranked_idx[MS_MAX_POP], int n) {
    for (int i = 0; i < n; i++) ranked_idx[i] = i;
    for (int i = 1; i < n; i++) {
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

static int tournament_pick_mem(const mem_eval_t evaluated[MS_MAX_POP],
                               const int ranked_idx[MS_MAX_POP],
                               const ms_options_t *opt,
                               mem_rng_t *rng) {
    int best_pos = mem_rng_range(rng, opt->population);
    int best_idx = ranked_idx[best_pos];
    for (int i = 1; i < opt->tournament; i++) {
        int cur_pos = mem_rng_range(rng, opt->population);
        int cur_idx = ranked_idx[cur_pos];
        if (better_score_idx(&evaluated[cur_idx].select_score, cur_idx,
                             &evaluated[best_idx].select_score, best_idx)) {
            best_idx = cur_idx;
        }
    }
    return best_idx;
}

static void run_weight_baseline(const ms_options_t *opt, FILE *curve,
                                mem_mode_result_t *result) {
    int8_t pop[MS_MAX_POP][EHW_GENOME_LEN];
    int8_t next[MS_MAX_POP][EHW_GENOME_LEN];
    mem_eval_t evaluated[MS_MAX_POP];
    int ranked_idx[MS_MAX_POP];
    mem_rng_t rng = { (uint32_t)(opt->seed + 202) };
    mem_seed_population(&rng, opt->population, opt->init_span, pop);
    mem_eval_candidate(MEM_MODE_LAMARCKIAN, pop[0], opt->adapt_epochs, opt->lr_shift,
                       opt->seed, &evaluated[0]);
    mem_eval_t best = evaluated[0];
    int first_40 = -1;

    for (int gen = 0; gen <= opt->generations; gen++) {
        for (int i = 0; i < opt->population; i++) {
            mem_eval_candidate(MEM_MODE_LAMARCKIAN, pop[i], opt->adapt_epochs,
                               opt->lr_shift, opt->seed + gen * 1009 + i,
                               &evaluated[i]);
        }
        sort_ranked_mem(evaluated, ranked_idx, opt->population);
        int top = ranked_idx[0];
        if (evaluated[top].select_score.fitness > best.select_score.fitness) {
            best = evaluated[top];
        }
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;
        write_weight_curve_row(curve, gen, top, &best);
        if (gen == opt->generations) break;

        for (int i = 0; i < opt->elites; i++) {
            mem_copy_genome(next[i], evaluated[ranked_idx[i]].genome_for_next);
        }
        int nnext = opt->elites;
        while (nnext < opt->population) {
            int p1 = tournament_pick_mem(evaluated, ranked_idx, opt, &rng);
            int8_t child[EHW_GENOME_LEN];
            if (mem_rng_chance(&rng, opt->crossover_ppm)) {
                int p2 = tournament_pick_mem(evaluated, ranked_idx, opt, &rng);
                mem_crossover(evaluated[p1].genome_for_next,
                              evaluated[p2].genome_for_next, child, &rng);
            } else {
                mem_copy_genome(child, evaluated[p1].genome_for_next);
            }
            mem_mutate(child, next[nnext], &rng, opt->mutation_ppm, opt->mutation_step);
            nnext++;
        }
        memcpy(pop, next, sizeof(pop));
    }

    result->mode = "lamarckian";
    mem_copy_genome(result->best_genome, best.genome_for_next);
    mem_copy_genome(result->best_pre_genome, best.pre_genome);
    mem_copy_genome(result->best_post_genome, best.post_genome);
    result->best_pre_score = best.pre_score;
    result->best_post_score = best.post_score;
    result->first_40 = first_40;
}

static void write_summary(FILE *f, const mem_mode_result_t *baseline,
                          const ms_result_t results[], int nresults) {
    fprintf(f, "mode,coupling,best_correct,best_sse,best_fitness,first_40,sat_count,feature_mask,feature_ones,feature_penalty,degraded_correct,repaired_correct,sr_genome,post_weight\n");
    fprintf(f, "weight_only_lamarckian,none,%d,%d,%d,",
            baseline->best_post_score.correct, baseline->best_post_score.sse,
            baseline->best_post_score.fitness);
    if (baseline->first_40 >= 0) fprintf(f, "%d", baseline->first_40);
    fprintf(f, ",%d,,,,,,,", mem_sat_count(baseline->best_post_genome));
    print_i8_genome(f, baseline->best_post_genome);
    fputc('\n', f);

    for (int i = 0; i < nresults; i++) {
        const ms_result_t *r = &results[i];
        fprintf(f, "%s,%s,%d,%d,%d,",
                r->mode, r->coupling, r->best_score.correct, r->best_score.sse,
                r->best_score.fitness);
        if (r->first_40 >= 0) fprintf(f, "%d", r->first_40);
        fprintf(f, ",%d,%010llx,%d,%d,%d,%d,",
                mem_sat_count(r->post_weight), (unsigned long long)r->feature_mask,
                r->feature_ones, r->feature_penalty,
                r->degraded_correct, r->repaired_correct);
        print_sr_genome(f, r->sr);
        fputc(',', f);
        print_i8_genome(f, r->post_weight);
        fputc('\n', f);
    }
}

static int self_check(void) {
    if (sr_truth_mask(MS_SR_MAJORITY, &SR_FAULT_NONE_OBJ) != SR_TARGET_MASK) return -1;
    sr_fault_t fault_a1 = SR_FAULT_DISABLE_A1;
    if (sr_truth_mask(MS_SR_REPAIR, &fault_a1) != SR_TARGET_MASK) return -2;
    return 0;
}

int main(int argc, char **argv) {
    cli_t cli;
    int pr = parse_args(&cli, argc, argv);
    if (pr) {
        fprintf(stderr, "bad arguments or population > MS_MAX_POP\n");
        return 2;
    }
    if (self_check()) {
        fprintf(stderr, "spare-route structural self-check failed\n");
        return 3;
    }

    FILE *curve = fopen(cli.curve_csv, "w");
    if (!curve) {
        perror(cli.curve_csv);
        return 1;
    }
    fprintf(curve, "mode,coupling,gen,best_correct,best_sse,best_fitness,select_fitness,feature_mask,feature_ones,feature_penalty,top_index,sr_genome,pre_weight,post_weight\n");

    mem_mode_result_t baseline;
    ms_result_t results[7];
    int n = 0;
    run_weight_baseline(&cli.opt, curve, &baseline);
    run_hybrid_mode(MS_MODE_LAMARCKIAN, MS_COUPLING_REPLACE_X3, &cli.opt, curve, &results[n++]);
    run_hybrid_mode(MS_MODE_LAMARCKIAN, MS_COUPLING_GATE_X3, &cli.opt, curve, &results[n++]);
    run_hybrid_mode(MS_MODE_LAMARCKIAN, MS_COUPLING_BIAS_X3, &cli.opt, curve, &results[n++]);
    run_hybrid_mode(MS_MODE_LAMARCKIAN_PRESSURE, MS_COUPLING_REPLACE_X3, &cli.opt, curve, &results[n++]);
    run_hybrid_mode(MS_MODE_LAMARCKIAN_PRESSURE, MS_COUPLING_GATE_X3, &cli.opt, curve, &results[n++]);
    run_hybrid_mode(MS_MODE_LAMARCKIAN_PRESSURE, MS_COUPLING_BIAS_X3, &cli.opt, curve, &results[n++]);
    run_hybrid_mode(MS_MODE_NO_ADAPT, MS_COUPLING_GATE_X3, &cli.opt, curve, &results[n++]);
    fclose(curve);

    FILE *summary = fopen(cli.summary_csv, "w");
    if (!summary) {
        perror(cli.summary_csv);
        return 1;
    }
    write_summary(summary, &baseline, results, n);
    fclose(summary);

    printf("weight_only_lamarckian best=%d/40 sse=%d first_40=",
           baseline.best_post_score.correct, baseline.best_post_score.sse);
    if (baseline.first_40 >= 0) printf("%d\n", baseline.first_40);
    else printf("none\n");
    for (int i = 0; i < n; i++) {
        printf("%s:%s best=%d/40 sse=%d first_40=",
               results[i].mode, results[i].coupling,
               results[i].best_score.correct, results[i].best_score.sse);
        if (results[i].first_40 >= 0) printf("%d", results[i].first_40);
        else printf("none");
        printf(" feature_ones=%d penalty=%d faultA1=%d/40\n",
               results[i].feature_ones, results[i].feature_penalty,
               results[i].degraded_correct);
    }
    printf("wrote %s\n", cli.curve_csv);
    printf("wrote %s\n", cli.summary_csv);
    return 0;
}
