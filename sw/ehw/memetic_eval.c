#include "memetic_kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MEM_MAX_POP
#define MEM_MAX_POP 64
#endif

typedef struct {
    mem_options_t opt;
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
    cli->opt.adapt_epochs = 2;
    cli->opt.lr_shift = MEM_LR_SHIFT;
    cli->opt.init_span = 32;
    cli->opt.elites = 2;
    cli->opt.tournament = 3;
    cli->opt.crossover_ppm = 700000;
    cli->opt.mutation_ppm = 30000;
    cli->opt.mutation_step = 8;
    cli->curve_csv = "runs/ehw4_1_memetic_c_curves.csv";
    cli->summary_csv = "runs/ehw4_1_memetic_c_summary.csv";
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
        else if (!strcmp(argv[i], "--crossover-rate")) { if (++i >= argc) return -1; cli->opt.crossover_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--mutation-rate")) { if (++i >= argc) return -1; cli->opt.mutation_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--curve-csv")) { if (++i >= argc) return -1; cli->curve_csv = argv[i]; }
        else if (!strcmp(argv[i], "--summary-csv")) { if (++i >= argc) return -1; cli->summary_csv = argv[i]; }
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return -1;
        }
    }
    if (cli->opt.population <= 0 || cli->opt.population > MEM_MAX_POP) return -2;
    if (cli->opt.elites <= 0 || cli->opt.elites > cli->opt.population) return -2;
    return 0;
}

static void print_genome(FILE *f, const int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%d", genome[i]);
    }
}

static int better_score_idx(const ehw_score_t *as, int ai, const ehw_score_t *bs, int bi) {
    if (as->fitness != bs->fitness) return as->fitness > bs->fitness;
    return ai < bi;
}

static int tournament_pick(const mem_eval_t evaluated[MEM_MAX_POP], const int ranked_idx[MEM_MAX_POP],
                           const mem_options_t *opt, mem_rng_t *rng) {
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

static void sort_ranked(const mem_eval_t evaluated[MEM_MAX_POP], int ranked_idx[MEM_MAX_POP], int n) {
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

static const char *mode_name(mem_mode_t mode) {
    switch (mode) {
        case MEM_MODE_PURE_GA: return "pure_ga";
        case MEM_MODE_BALDWINIAN: return "baldwinian";
        case MEM_MODE_LAMARCKIAN: return "lamarckian";
    }
    return "?";
}

static void write_curve_row(FILE *f, const char *mode, int gen, const mem_eval_t *best) {
    fprintf(f, "%s,%d,%d,%.4f,%d,%d,%d,%d,",
            mode, gen, best->post_score.correct, best->post_score.correct / 40.0,
            best->post_score.sse, best->post_score.fitness,
            best->pre_score.correct, best->post_score.correct);
    print_genome(f, best->genome_for_next);
    fputc(',', f);
    print_genome(f, best->pre_genome);
    fputc(',', f);
    print_genome(f, best->post_genome);
    fputc('\n', f);
}

static void write_pure_sgd_curve_row(FILE *f, int gen, const ehw_score_t *best_score,
                                     const ehw_score_t *cur_score,
                                     const int8_t best_genome[EHW_GENOME_LEN],
                                     const int8_t cur_genome[EHW_GENOME_LEN]) {
    fprintf(f, "pure_sgd,%d,%d,%.4f,%d,%d,%d,%d,",
            gen, best_score->correct, best_score->correct / 40.0,
            best_score->sse, best_score->fitness, cur_score->correct, cur_score->correct);
    print_genome(f, best_genome);
    fputc(',', f);
    print_genome(f, cur_genome);
    fputc(',', f);
    print_genome(f, cur_genome);
    fputc('\n', f);
}

static void run_evolution_mode(mem_mode_t mode, const mem_options_t *opt, FILE *curve,
                               mem_mode_result_t *result) {
    static int8_t pop[MEM_MAX_POP][EHW_GENOME_LEN];
    static int8_t next[MEM_MAX_POP][EHW_GENOME_LEN];
    mem_eval_t evaluated[MEM_MAX_POP];
    int ranked_idx[MEM_MAX_POP];
    int seed_offset = (mode == MEM_MODE_PURE_GA) ? 0 : (mode == MEM_MODE_BALDWINIAN ? 101 : 202);
    mem_rng_t rng = { (uint32_t)(opt->seed + seed_offset) };
    mem_seed_population(&rng, opt->population, opt->init_span, pop);

    mem_eval_candidate(mode, pop[0], opt->adapt_epochs, opt->lr_shift, opt->seed, &evaluated[0]);
    mem_eval_t best = evaluated[0];
    int first_40 = -1;

    for (int gen = 0; gen <= opt->generations; gen++) {
        for (int i = 0; i < opt->population; i++) {
            mem_eval_candidate(mode, pop[i], opt->adapt_epochs, opt->lr_shift,
                               opt->seed + gen * 1009 + i, &evaluated[i]);
        }
        sort_ranked(evaluated, ranked_idx, opt->population);
        int top = ranked_idx[0];
        if (evaluated[top].select_score.fitness > best.select_score.fitness) {
            best = evaluated[top];
        }
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;
        write_curve_row(curve, mode_name(mode), gen, &best);
        if (gen == opt->generations) break;

        for (int i = 0; i < opt->elites; i++) {
            mem_copy_genome(next[i], evaluated[ranked_idx[i]].genome_for_next);
        }
        int nnext = opt->elites;
        while (nnext < opt->population) {
            int p1_idx = tournament_pick(evaluated, ranked_idx, opt, &rng);
            int8_t child[EHW_GENOME_LEN];
            if (mem_rng_chance(&rng, opt->crossover_ppm)) {
                int p2_idx = tournament_pick(evaluated, ranked_idx, opt, &rng);
                mem_crossover(evaluated[p1_idx].genome_for_next,
                              evaluated[p2_idx].genome_for_next, child, &rng);
            } else {
                mem_copy_genome(child, evaluated[p1_idx].genome_for_next);
            }
            mem_mutate(child, next[nnext], &rng, opt->mutation_ppm, opt->mutation_step);
            nnext++;
        }
        memcpy(pop, next, sizeof(pop));
    }

    result->mode = mode_name(mode);
    mem_copy_genome(result->best_genome, best.genome_for_next);
    mem_copy_genome(result->best_pre_genome, best.pre_genome);
    mem_copy_genome(result->best_post_genome, best.post_genome);
    result->best_pre_score = best.pre_score;
    result->best_post_score = best.post_score;
    result->first_40 = first_40;
}

static void run_pure_sgd(const mem_options_t *opt, FILE *curve, mem_mode_result_t *result) {
    int8_t genome[EHW_GENOME_LEN];
    mem_copy_genome(genome, EHW_TRAINED_GENOME);
    int8_t best_genome[EHW_GENOME_LEN];
    mem_copy_genome(best_genome, genome);
    ehw_score_t best_score = ehw_evaluate(genome);
    int first_40 = best_score.correct >= EHW_NTEST ? 0 : -1;

    for (int gen = 0; gen <= opt->generations; gen++) {
        ehw_score_t score = ehw_evaluate(genome);
        if (score.fitness > best_score.fitness) {
            best_score = score;
            mem_copy_genome(best_genome, genome);
        }
        if (first_40 < 0 && best_score.correct >= EHW_NTEST) first_40 = gen;
        write_pure_sgd_curve_row(curve, gen, &best_score, &score, best_genome, genome);
        if (gen < opt->generations) {
            int8_t adapted[EHW_GENOME_LEN];
            mem_adapt(genome, opt->population * opt->adapt_epochs, opt->lr_shift,
                      opt->seed + 50000 + gen, adapted);
            mem_copy_genome(genome, adapted);
        }
    }
    result->mode = "pure_sgd";
    mem_copy_genome(result->best_genome, best_genome);
    mem_copy_genome(result->best_pre_genome, best_genome);
    mem_copy_genome(result->best_post_genome, best_genome);
    result->best_pre_score = best_score;
    result->best_post_score = best_score;
    result->first_40 = first_40;
}

static void write_summary(FILE *f, const mem_mode_result_t *r) {
    fprintf(f, "%s,%d,%.4f,%d,%d,", r->mode, r->best_post_score.correct,
            r->best_post_score.correct / 40.0, r->best_post_score.sse,
            r->best_post_score.fitness);
    if (r->first_40 >= 0) fprintf(f, "%d", r->first_40);
    fputc(',', f);
    print_genome(f, r->best_genome);
    fputc(',', f);
    print_genome(f, r->best_pre_genome);
    fputc(',', f);
    print_genome(f, r->best_post_genome);
    fputc(',', f);
    fprintf(f, "%d\n", mem_sat_count(r->best_post_genome));
}

int main(int argc, char **argv) {
    cli_t cli;
    int pr = parse_args(&cli, argc, argv);
    if (pr) {
        fprintf(stderr, "bad arguments or population > MEM_MAX_POP\n");
        return 2;
    }

    FILE *curve = fopen(cli.curve_csv, "w");
    if (!curve) {
        perror(cli.curve_csv);
        return 1;
    }
    fprintf(curve, "mode,gen,best_correct,best_acc,best_sse,best_fitness,pre_correct,post_correct,genome,pre_genome,post_genome\n");

    mem_mode_result_t results[4];
    run_evolution_mode(MEM_MODE_PURE_GA, &cli.opt, curve, &results[0]);
    run_pure_sgd(&cli.opt, curve, &results[1]);
    run_evolution_mode(MEM_MODE_BALDWINIAN, &cli.opt, curve, &results[2]);
    run_evolution_mode(MEM_MODE_LAMARCKIAN, &cli.opt, curve, &results[3]);
    fclose(curve);

    FILE *summary = fopen(cli.summary_csv, "w");
    if (!summary) {
        perror(cli.summary_csv);
        return 1;
    }
    fprintf(summary, "mode,best_correct,best_acc,best_sse,best_fitness,first_40,genome,pre_genome,post_genome,sat_count\n");
    for (int i = 0; i < 4; i++) write_summary(summary, &results[i]);
    fclose(summary);

    for (int i = 0; i < 4; i++) {
        printf("%-11s best=%d/40 sse=%d first_40=",
               results[i].mode, results[i].best_post_score.correct, results[i].best_post_score.sse);
        if (results[i].first_40 >= 0) printf("%d", results[i].first_40);
        else printf("none");
        printf("\n");
    }
    printf("wrote %s\n", cli.curve_csv);
    printf("wrote %s\n", cli.summary_csv);
    return 0;
}
