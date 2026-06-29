#include "cgp_kernel.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t state;
} rng_t;

typedef struct {
    int fitness;
    int idx;
} ranked_t;

typedef struct {
    int seed;
    int population;
    int generations;
    int elites;
    int tournament;
    int crossover_ppm;
    int mutation_ppm;
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

static void copy_genome(uint16_t dst[CGP_GENOME_LEN], const uint16_t src[CGP_GENOME_LEN]) {
    memcpy(dst, src, sizeof(uint16_t) * CGP_GENOME_LEN);
}

static void random_genome(rng_t *rng, uint16_t genome[CGP_GENOME_LEN]) {
    for (int i = 0; i < 4; i++) {
        genome[i] = CGP_PASS[i];
        genome[4 + i] = CGP_PASS[i];
        genome[8 + i] = (uint16_t)rng_range(rng, 65536);
    }
}

static void mutate(const uint16_t in[CGP_GENOME_LEN], uint16_t out[CGP_GENOME_LEN],
                   rng_t *rng, int rate_ppm) {
    copy_genome(out, in);
    int changed = 0;
    for (int node = 8; node < 12; node++) {
        uint16_t value = out[node];
        for (int bit = 0; bit < 16; bit++) {
            if (rng_chance(rng, rate_ppm)) {
                value ^= (uint16_t)(1u << bit);
                changed = 1;
            }
        }
        out[node] = value;
    }
    if (!changed) {
        int node = 8 + rng_range(rng, 4);
        out[node] ^= (uint16_t)(1u << rng_range(rng, 16));
    }
}

static void crossover(const uint16_t a[CGP_GENOME_LEN], const uint16_t b[CGP_GENOME_LEN],
                      uint16_t out[CGP_GENOME_LEN], rng_t *rng) {
    for (int i = 0; i < 4; i++) {
        out[i] = CGP_PASS[i];
        out[4 + i] = CGP_PASS[i];
        out[8 + i] = rng_chance(rng, 500000) ? a[8 + i] : b[8 + i];
    }
}

static int better_ranked(const ranked_t *a, const ranked_t *b) {
    if (a->fitness != b->fitness) return a->fitness > b->fitness;
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
        if (ranked[cur].fitness > ranked[best].fitness) best = cur;
    }
    return ranked[best].idx;
}

static void write_genome(FILE *f, const uint16_t genome[CGP_GENOME_LEN]) {
    for (int i = 0; i < CGP_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%04x", genome[i]);
    }
}

static void defaults(options_t *o) {
    o->seed = 3;
    o->population = 64;
    o->generations = 200;
    o->elites = 2;
    o->tournament = 3;
    o->crossover_ppm = 700000;
    o->mutation_ppm = 30000;
    o->report_every = 20;
    o->verbose = 1;
    o->check_golden = 0;
    o->csv_path = "runs/ehw1_0_cgp_c.csv";
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
        else if (!strcmp(argv[i], "--elites")) { if (parse_int_arg(&o->elites, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--tournament")) { if (parse_int_arg(&o->tournament, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--report-every")) { if (parse_int_arg(&o->report_every, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--crossover-ppm")) { if (parse_int_arg(&o->crossover_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--mutation-ppm")) { if (parse_int_arg(&o->mutation_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--crossover-rate")) { if (++i >= argc) return -1; o->crossover_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--mutation-rate")) { if (++i >= argc) return -1; o->mutation_ppm = rate_to_ppm(argv[i]); }
        else if (!strcmp(argv[i], "--csv")) { if (++i >= argc) return -1; o->csv_path = argv[i]; }
        else if (!strcmp(argv[i], "--quiet")) { o->verbose = 0; }
        else if (!strcmp(argv[i], "--check-golden")) { o->check_golden = 1; o->csv_path = NULL; o->verbose = 0; }
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    options_t opt;
    if (parse_args(&opt, argc, argv)) {
        fputs("bad arguments\n", stderr);
        return 2;
    }
    if (opt.check_golden) {
        int fit = cgp_evaluate(CGP_GOLDEN_GENOME);
        printf("golden_fitness=%d/64 rows=%d/16 genome=", fit, cgp_rows_correct(CGP_GOLDEN_GENOME));
        write_genome(stdout, CGP_GOLDEN_GENOME);
        putchar('\n');
        return fit == CGP_FITNESS_MAX ? 0 : 1;
    }

    rng_t rng = {(uint32_t)opt.seed};
    int pop_size = opt.population;
    uint16_t (*pop)[CGP_GENOME_LEN] = calloc((size_t)pop_size, sizeof(*pop));
    uint16_t (*next)[CGP_GENOME_LEN] = calloc((size_t)pop_size, sizeof(*next));
    ranked_t *ranked = calloc((size_t)pop_size, sizeof(*ranked));
    if (!pop || !next || !ranked) {
        fputs("allocation failed\n", stderr);
        return 2;
    }
    for (int i = 0; i < pop_size; i++) random_genome(&rng, pop[i]);

    uint16_t best_genome[CGP_GENOME_LEN];
    copy_genome(best_genome, pop[0]);
    int best_fit = cgp_evaluate(best_genome);
    int best_gen = 0;

    FILE *csv = opt.csv_path ? fopen(opt.csv_path, "w") : NULL;
    if (opt.csv_path && !csv) {
        fprintf(stderr, "open %s: %s\n", opt.csv_path, strerror(errno));
        return 2;
    }
    if (csv) fputs("gen,best_fitness,rows_correct,active_nodes,genome\n", csv);

    for (int gen = 0; gen <= opt.generations; gen++) {
        for (int i = 0; i < pop_size; i++) {
            ranked[i].fitness = cgp_evaluate(pop[i]);
            ranked[i].idx = i;
        }
        stable_sort(ranked, pop_size);
        if (ranked[0].fitness > best_fit) {
            best_fit = ranked[0].fitness;
            best_gen = gen;
            copy_genome(best_genome, pop[ranked[0].idx]);
        }
        if (csv) {
            fprintf(csv, "%d,%d,%d,%d,", gen, best_fit, cgp_rows_correct(best_genome),
                    cgp_active_nodes(best_genome));
            write_genome(csv, best_genome);
            fputc('\n', csv);
        }
        if (opt.verbose && (gen == 0 || gen == opt.generations || gen % opt.report_every == 0)) {
            printf("gen %4d: fitness=%2d/64 rows=%2d/16\n", gen, best_fit, cgp_rows_correct(best_genome));
        }
        if (best_fit >= CGP_FITNESS_MAX) break;

        for (int i = 0; i < opt.elites; i++) copy_genome(next[i], pop[ranked[i].idx]);
        int nnext = opt.elites;
        while (nnext < pop_size) {
            int p1 = tournament_pick(ranked, pop_size, &rng, opt.tournament);
            uint16_t child[CGP_GENOME_LEN];
            if (rng_chance(&rng, opt.crossover_ppm)) {
                int p2 = tournament_pick(ranked, pop_size, &rng, opt.tournament);
                crossover(pop[p1], pop[p2], child, &rng);
            } else {
                copy_genome(child, pop[p1]);
            }
            mutate(child, next[nnext], &rng, opt.mutation_ppm);
            nnext++;
        }
        uint16_t (*tmp)[CGP_GENOME_LEN] = pop;
        pop = next;
        next = tmp;
    }

    if (csv) fclose(csv);
    puts("\n== EHW-1.0 CGP C twin best ==");
    printf("fitness=%d/64 rows=%d/16 generation=%d active_nodes=%d\n",
           best_fit, cgp_rows_correct(best_genome), best_gen, cgp_active_nodes(best_genome));
    printf("genome=");
    write_genome(stdout, best_genome);
    putchar('\n');
    if (opt.csv_path) printf("csv=%s\n", opt.csv_path);

    free(pop);
    free(next);
    free(ranked);
    return best_fit == CGP_FITNESS_MAX ? 0 : 1;
}
