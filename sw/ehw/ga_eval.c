#include "ehw_kernel.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t state;
} rng_t;

typedef struct {
    ehw_score_t score;
    int idx;
} ranked_t;

typedef struct {
    int seed;
    int population;
    int generations;
    int target_correct;
    int init_span;
    int elites;
    int tournament;
    int crossover_ppm;
    int mutation_ppm;
    int mutation_step;
    int report_every;
    int verbose;
    int check_golden;
    const char *csv_path;
} options_t;

static uint32_t rng_next(rng_t *rng) {
    uint32_t x = rng->state ? rng->state : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static int rng_range(rng_t *rng, int hi) {
    return (int)(rng_next(rng) % (uint32_t)hi);
}

static int rng_int(rng_t *rng, int lo, int hi) {
    return lo + rng_range(rng, hi - lo + 1);
}

static int rng_chance(rng_t *rng, int ppm) {
    return rng_range(rng, 1000000) < ppm;
}

static int rate_to_ppm(const char *s) {
    double v = strtod(s, NULL);
    int ppm = (int)(v * 1000000.0 + 0.5);
    if (ppm < 0) return 0;
    if (ppm > 1000000) return 1000000;
    return ppm;
}

static void copy_genome(int8_t dst[EHW_GENOME_LEN], const int8_t src[EHW_GENOME_LEN]) {
    memcpy(dst, src, EHW_GENOME_LEN);
}

static void random_genome(rng_t *rng, int8_t genome[EHW_GENOME_LEN], int span) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        genome[i] = (int8_t)rng_int(rng, -span, span);
    }
}

static void mutate(const int8_t in[EHW_GENOME_LEN], int8_t out[EHW_GENOME_LEN],
                   rng_t *rng, int rate_ppm, int step) {
    copy_genome(out, in);
    int changed = 0;
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        int value = out[i];
        if (rng_chance(rng, rate_ppm)) {
            if (rng_chance(rng, 250000)) {
                value ^= 1 << rng_range(rng, 8);
                if (value >= 128) value -= 256;
            } else {
                value += rng_int(rng, -step, step);
            }
            out[i] = ehw_clamp_i8(value);
            changed = 1;
        }
    }
    if (!changed) {
        int i = rng_range(rng, EHW_GENOME_LEN);
        int delta = (rng_range(rng, 2) == 0 ? -1 : 1) * rng_int(rng, 1, step);
        out[i] = ehw_clamp_i8((int)out[i] + delta);
    }
}

static void crossover(const int8_t a[EHW_GENOME_LEN], const int8_t b[EHW_GENOME_LEN],
                      int8_t out[EHW_GENOME_LEN], rng_t *rng) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        out[i] = rng_chance(rng, 500000) ? a[i] : b[i];
    }
}

static int better_ranked(const ranked_t *a, const ranked_t *b) {
    if (a->score.fitness != b->score.fitness) return a->score.fitness > b->score.fitness;
    return a->idx < b->idx;
}

static void stable_sort(ranked_t *ranked, int n) {
    for (int i = 1; i < n; i++) {
        ranked_t item = ranked[i];
        int j = i - 1;
        while (j >= 0 && better_ranked(&item, &ranked[j])) {
            ranked[j + 1] = ranked[j];
            j--;
        }
        ranked[j + 1] = item;
    }
}

static int tournament_pick(const ranked_t *ranked, int pop_size, rng_t *rng, int k) {
    int best = rng_range(rng, pop_size);
    for (int i = 1; i < k; i++) {
        int cur = rng_range(rng, pop_size);
        if (better_ranked(&ranked[cur], &ranked[best])) best = cur;
    }
    return ranked[best].idx;
}

static void write_genome_csv(FILE *f, const int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%d", genome[i]);
    }
}

static void print_tile(const int8_t genome[EHW_GENOME_LEN]) {
    puts("W1 tile:");
    for (int r = 0; r < EHW_NH; r++) {
        printf("  ");
        for (int c = 0; c < EHW_NIN; c++) printf("%4d", genome[r * EHW_NIN + c]);
        putchar('\n');
    }
    puts("W2 tile (padded):");
    const int base = EHW_NH * EHW_NIN;
    for (int r = 0; r < 4; r++) {
        printf("  ");
        for (int c = 0; c < 4; c++) {
            int v = (r < EHW_NOUT) ? genome[base + r * EHW_NH + c] : 0;
            printf("%4d", v);
        }
        putchar('\n');
    }
}

static void defaults(options_t *o) {
    o->seed = 3;
    o->population = 64;
    o->generations = 200;
    o->target_correct = 40;
    o->init_span = 32;
    o->elites = 2;
    o->tournament = 3;
    o->crossover_ppm = 700000;
    o->mutation_ppm = 30000;
    o->mutation_step = 8;
    o->report_every = 10;
    o->verbose = 1;
    o->check_golden = 0;
    o->csv_path = "runs/ehw0_c_twin.csv";
}

static int parse_int_arg(int *dst, int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return -1;
    *dst = atoi(argv[++(*i)]);
    return 0;
}

static int parse_args(options_t *o, int argc, char **argv) {
    defaults(o);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) { if (parse_int_arg(&o->seed, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--population")) { if (parse_int_arg(&o->population, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--generations")) { if (parse_int_arg(&o->generations, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--target-correct")) { if (parse_int_arg(&o->target_correct, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--init-span")) { if (parse_int_arg(&o->init_span, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--elites")) { if (parse_int_arg(&o->elites, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--tournament")) { if (parse_int_arg(&o->tournament, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--mutation-step")) { if (parse_int_arg(&o->mutation_step, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--report-every")) { if (parse_int_arg(&o->report_every, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--crossover-rate")) { if (++i >= argc) return -1; o->crossover_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--mutation-rate")) { if (++i >= argc) return -1; o->mutation_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--csv")) { if (++i >= argc) return -1; o->csv_path = argv[i]; }
        else if (!strcmp(argv[i], "--quiet")) { o->verbose = 0; }
        else if (!strcmp(argv[i], "--check-golden")) { o->check_golden = 1; o->csv_path = NULL; o->verbose = 0; }
        else if (!strcmp(argv[i], "--rng")) { if (++i >= argc) return -1; if (strcmp(argv[i], "xorshift")) return -2; }
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    options_t opt;
    int pr = parse_args(&opt, argc, argv);
    if (pr == -2) {
        fputs("C twin supports only --rng xorshift\n", stderr);
        return 2;
    }
    if (pr) {
        fputs("bad arguments\n", stderr);
        return 2;
    }
    if (opt.check_golden) {
        ehw_score_t trained = ehw_evaluate(EHW_TRAINED_GENOME);
        int mism = ehw_golden_mismatches(EHW_TRAINED_GENOME);
        printf("golden_mismatches=%d label_correct=%d/40 sse=%d fitness=%d\n",
               mism, trained.correct, trained.sse, trained.fitness);
        return mism == 0 ? 0 : 1;
    }

    rng_t rng = {(uint32_t)opt.seed};
    int pop_size = opt.population;
    int8_t (*pop)[EHW_GENOME_LEN] = calloc((size_t)pop_size, sizeof(*pop));
    int8_t (*next)[EHW_GENOME_LEN] = calloc((size_t)pop_size, sizeof(*next));
    ranked_t *ranked = calloc((size_t)pop_size, sizeof(*ranked));
    if (!pop || !next || !ranked) {
        fputs("allocation failed\n", stderr);
        return 2;
    }

    for (int i = 0; i < pop_size; i++) random_genome(&rng, pop[i], opt.init_span);
    copy_genome(pop[0], EHW_TRAINED_GENOME);
    for (int i = 1; i < 8 && i < pop_size; i++) {
        mutate(EHW_TRAINED_GENOME, pop[i], &rng, 250000, 4);
    }

    FILE *csv = opt.csv_path ? fopen(opt.csv_path, "w") : NULL;
    if (opt.csv_path && !csv) {
        fprintf(stderr, "open %s: %s\n", opt.csv_path, strerror(errno));
        return 2;
    }
    if (csv) fputs("gen,best_correct,best_acc,best_sse,best_fitness,genome\n", csv);

    int8_t best_genome[EHW_GENOME_LEN];
    copy_genome(best_genome, pop[0]);
    ehw_score_t best_score = ehw_evaluate(best_genome);

    for (int gen = 0; gen <= opt.generations; gen++) {
        for (int i = 0; i < pop_size; i++) {
            ranked[i].score = ehw_evaluate(pop[i]);
            ranked[i].idx = i;
        }
        stable_sort(ranked, pop_size);
        if (ranked[0].score.fitness > best_score.fitness) {
            best_score = ranked[0].score;
            copy_genome(best_genome, pop[ranked[0].idx]);
        }

        if (csv) {
            fprintf(csv, "%d,%d,%.4f,%d,%d,", gen, best_score.correct,
                    (double)best_score.correct / (double)EHW_NTEST,
                    best_score.sse, best_score.fitness);
            write_genome_csv(csv, best_genome);
            fputc('\n', csv);
        }
        if (opt.verbose && (gen == 0 || gen == opt.generations || gen % opt.report_every == 0)) {
            printf("gen %4d: acc=%2d/%d sse=%6d fitness=%d\n", gen, best_score.correct,
                   EHW_NTEST, best_score.sse, best_score.fitness);
        }
        if (best_score.correct >= opt.target_correct) break;

        for (int i = 0; i < opt.elites; i++) copy_genome(next[i], pop[ranked[i].idx]);
        int nnext = opt.elites;
        while (nnext < pop_size) {
            int p1 = tournament_pick(ranked, pop_size, &rng, opt.tournament);
            int8_t child[EHW_GENOME_LEN];
            if (rng_chance(&rng, opt.crossover_ppm)) {
                int p2 = tournament_pick(ranked, pop_size, &rng, opt.tournament);
                crossover(pop[p1], pop[p2], child, &rng);
            } else {
                copy_genome(child, pop[p1]);
            }
            mutate(child, next[nnext], &rng, opt.mutation_ppm, opt.mutation_step);
            nnext++;
        }
        int8_t (*tmp)[EHW_GENOME_LEN] = pop;
        pop = next;
        next = tmp;
    }

    if (csv) fclose(csv);
    puts("\n== EHW-0.1 C twin best ==");
    printf("accuracy=%d/%d (%.3f) sse=%d fitness=%d\n", best_score.correct, EHW_NTEST,
           (double)best_score.correct / (double)EHW_NTEST, best_score.sse, best_score.fitness);
    print_tile(best_genome);
    if (opt.csv_path) printf("csv=%s\n", opt.csv_path);

    free(pop);
    free(next);
    free(ranked);
    return (best_score.correct >= opt.target_correct) ? 0 : 1;
}
