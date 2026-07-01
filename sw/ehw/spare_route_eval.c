#include "spare_route_kernel.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t state;
} rng_t;

typedef struct {
    int key[3];
    int idx;
} ranked_t;

typedef struct {
    int seed;
    int population;
    int generations;
    int elites;
    int tournament;
    int crossover_ppm;
    int init_mutation_ppm;
    int sel_mutation_ppm;
    int recovery;
    int check_contract;
    int dump_faults;
    int have_inject;
    uint8_t inject[SR_GENOME_LEN];
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

static void copy_genome(uint8_t dst[SR_GENOME_LEN], const uint8_t src[SR_GENOME_LEN]) {
    memcpy(dst, src, SR_GENOME_LEN);
}

static void write_genome(FILE *f, const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%02x", genome[i]);
    }
}

static int parse_genome(const char *s, uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) {
        char *end = NULL;
        unsigned long v = strtoul(s, &end, 16);
        if (end == s || v > 255ul) return -1;
        genome[i] = (uint8_t)v;
        s = end;
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
    }
    return *s == '\0' ? 0 : -1;
}

static void random_genome(rng_t *rng, uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < 4; i++) genome[i] = (uint8_t)rng_range(rng, 16);
    genome[4] = (uint8_t)rng_range(rng, 256);
    for (int i = 5; i < 13; i++) genome[i] = (uint8_t)rng_range(rng, 5);
    for (int i = 13; i < 16; i++) genome[i] = (uint8_t)rng_range(rng, 4);
}

static void scaffold_genome(rng_t *rng, int use_spare, uint8_t genome[SR_GENOME_LEN]) {
    genome[0] = 0x0Au;
    genome[1] = 0x0Au;
    genome[2] = 0x0Au;
    genome[3] = use_spare ? 0x0Au : 0x00u;
    genome[4] = (uint8_t)rng_range(rng, 256);
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

static void mutate(const uint8_t in[SR_GENOME_LEN], uint8_t out[SR_GENOME_LEN],
                   rng_t *rng, int init_ppm, int sel_ppm) {
    copy_genome(out, in);
    int changed = 0;
    for (int idx = 0; idx <= 4; idx++) {
        int bits = idx == 4 ? 8 : 4;
        uint8_t value = out[idx];
        for (int bit = 0; bit < bits; bit++) {
            if (rng_chance(rng, init_ppm)) {
                value ^= (uint8_t)(1u << bit);
                changed = 1;
            }
        }
        out[idx] = (uint8_t)(value & ((1u << bits) - 1u));
    }
    for (int idx = 5; idx < 16; idx++) {
        int limit = idx < 13 ? 5 : 4;
        if (rng_chance(rng, sel_ppm)) {
            out[idx] = (uint8_t)rng_range(rng, limit);
            changed = 1;
        }
    }
    if (!changed) {
        int idx = rng_range(rng, SR_GENOME_LEN);
        if (idx < 4) out[idx] ^= (uint8_t)(1u << rng_range(rng, 4));
        else if (idx == 4) out[idx] ^= (uint8_t)(1u << rng_range(rng, 8));
        else if (idx < 13) out[idx] = (uint8_t)rng_range(rng, 5);
        else out[idx] = (uint8_t)rng_range(rng, 4);
    }
}

static void crossover(const uint8_t a[SR_GENOME_LEN], const uint8_t b[SR_GENOME_LEN],
                      uint8_t out[SR_GENOME_LEN], rng_t *rng) {
    for (int i = 0; i < SR_GENOME_LEN; i++) out[i] = rng_chance(rng, 500000) ? a[i] : b[i];
}

static void score_key(const uint8_t genome[SR_GENOME_LEN], const sr_fault_t *fault, int key[3]) {
    int fit = sr_fitness(genome, fault);
    key[0] = fit;
    if (fault->kind == SR_FAULT_NONE) {
        key[1] = fit - sr_fitness(genome, &SR_FAULT_DISABLE_A1);
        key[2] = sr_out_uses_node(genome, SR_NODE_A1);
    } else {
        key[1] = sr_out_uses_spare(genome);
        key[2] = !sr_out_uses_node(genome, SR_NODE_A1);
    }
}

static int genome_lex_gt(const uint8_t a[SR_GENOME_LEN], const uint8_t b[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return 0;
}

static int key_gt(const int a[3], const int b[3]) {
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return 0;
}

static int ranked_gt(const ranked_t *a, const ranked_t *b,
                     const uint8_t (*pop)[SR_GENOME_LEN]) {
    if (key_gt(a->key, b->key)) return 1;
    if (key_gt(b->key, a->key)) return 0;
    return genome_lex_gt(pop[a->idx], pop[b->idx]);
}

static void stable_sort(ranked_t *ranked, int n, const uint8_t (*pop)[SR_GENOME_LEN]) {
    for (int i = 1; i < n; i++) {
        ranked_t item = ranked[i];
        int j = i - 1;
        while (j >= 0 && ranked_gt(&item, &ranked[j], pop)) {
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
        if (key_gt(ranked[cur].key, ranked[best].key)) best = cur;
    }
    return ranked[best].idx;
}

static int accepted(const uint8_t best[SR_GENOME_LEN], const sr_fault_t *fault) {
    if (fault->kind == SR_FAULT_NONE) {
        return sr_fitness(best, fault) == SR_FITNESS_MAX &&
               sr_fitness(best, &SR_FAULT_DISABLE_A1) < SR_FITNESS_MAX;
    }
    return sr_fitness(best, fault) == SR_FITNESS_MAX &&
           sr_out_uses_spare(best) &&
           !sr_out_uses_node(best, SR_NODE_A1);
}

static void defaults(options_t *opt) {
    memset(opt, 0, sizeof(*opt));
    opt->seed = 3;
    opt->population = 256;
    opt->generations = 1000;
    opt->elites = 4;
    opt->tournament = 3;
    opt->crossover_ppm = 700000;
    opt->init_mutation_ppm = 50000;
    opt->sel_mutation_ppm = 30000;
    opt->csv_path = "runs/ehw3_1_spare_route_c.csv";
}

static int parse_int_arg(int *dst, int argc, char **argv, int *i) {
    if (*i + 1 >= argc) return -1;
    *dst = atoi(argv[++(*i)]);
    return 0;
}

static int parse_args(options_t *opt, int argc, char **argv) {
    defaults(opt);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--seed")) { if (parse_int_arg(&opt->seed, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--population")) { if (parse_int_arg(&opt->population, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--generations")) { if (parse_int_arg(&opt->generations, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--elites")) { if (parse_int_arg(&opt->elites, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--tournament")) { if (parse_int_arg(&opt->tournament, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--crossover-ppm")) { if (parse_int_arg(&opt->crossover_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--init-mutation-ppm")) { if (parse_int_arg(&opt->init_mutation_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--sel-mutation-ppm")) { if (parse_int_arg(&opt->sel_mutation_ppm, argc, argv, &i)) return -1; }
        else if (!strcmp(argv[i], "--mode")) {
            if (++i >= argc) return -1;
            if (!strcmp(argv[i], "nofault")) opt->recovery = 0;
            else if (!strcmp(argv[i], "recovery")) opt->recovery = 1;
            else return -1;
        }
        else if (!strcmp(argv[i], "--inject")) {
            if (++i >= argc || parse_genome(argv[i], opt->inject)) return -1;
            opt->have_inject = 1;
        }
        else if (!strcmp(argv[i], "--csv")) { if (++i >= argc) return -1; opt->csv_path = argv[i]; }
        else if (!strcmp(argv[i], "--check-contract")) { opt->check_contract = 1; opt->csv_path = NULL; }
        else if (!strcmp(argv[i], "--dump-fault-masks")) {
            if (++i >= argc || parse_genome(argv[i], opt->inject)) return -1;
            opt->dump_faults = 1;
            opt->csv_path = NULL;
        }
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
    if (opt.check_contract) {
        int ok = sr_reference_representability_check();
        printf("representability_self_check=%s target=0xe8 output_lut=LUT8\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    if (opt.dump_faults) {
        const sr_fault_t faults[] = {
            {SR_FAULT_NONE, -1, -1, -1, -1},
            {SR_FAULT_STUCK0, SR_NODE_A1, -1, -1, -1},
            {SR_FAULT_STUCK1, SR_NODE_A1, -1, -1, -1},
            {SR_FAULT_DISABLE_NODE, SR_NODE_A1, -1, -1, -1},
            {SR_FAULT_DISABLE_ROUTE, -1, 1, 0, 1},
            {SR_FAULT_DISABLE_ROUTE, -1, 0, SR_NODE_A1, 0},
        };
        const char *labels[] = {
            "FAULT_NONE",
            "FAULT_STUCK0(A1)",
            "FAULT_STUCK1(A1)",
            "FAULT_DISABLE_NODE(A1)",
            "FAULT_DISABLE_ROUTE(O.in1)",
            "FAULT_DISABLE_ROUTE(A1.in0)",
        };
        for (int i = 0; i < 6; i++) {
            printf("%s,%02x,%d\n", labels[i], sr_truth_mask(opt.inject, &faults[i]),
                   sr_fitness(opt.inject, &faults[i]));
        }
        return 0;
    }

    const sr_fault_t *fault = opt.recovery ? &SR_FAULT_DISABLE_A1 : &SR_FAULT_NONE_OBJ;
    rng_t rng = {(uint32_t)opt.seed};
    int pop_size = opt.population;
    uint8_t (*pop)[SR_GENOME_LEN] = calloc((size_t)pop_size, sizeof(*pop));
    uint8_t (*next)[SR_GENOME_LEN] = calloc((size_t)pop_size, sizeof(*next));
    ranked_t *ranked = calloc((size_t)pop_size, sizeof(*ranked));
    if (!pop || !next || !ranked) {
        fputs("allocation failed\n", stderr);
        return 2;
    }

    int n = 0;
    for (int i = 0; i < 16 && n < pop_size; i++) scaffold_genome(&rng, 0, pop[n++]);
    for (int i = 0; i < 16 && n < pop_size; i++) scaffold_genome(&rng, 1, pop[n++]);
    if (opt.have_inject && n < pop_size) copy_genome(pop[n++], opt.inject);
    while (n < pop_size) random_genome(&rng, pop[n++]);

    uint8_t best[SR_GENOME_LEN];
    copy_genome(best, pop[0]);
    int best_key[3] = {-1, -1, -1};
    int best_gen = 0;

    FILE *csv = opt.csv_path ? fopen(opt.csv_path, "w") : NULL;
    if (opt.csv_path && !csv) {
        fprintf(stderr, "open %s: %s\n", opt.csv_path, strerror(errno));
        return 2;
    }
    if (csv) {
        fputs("gen,best_gen,best_fitness,mask,no_fault_mask,disable_a1_fitness,"
              "uses_a1,uses_as,accepted,genome\n", csv);
    }

    for (int gen = 0; gen <= opt.generations; gen++) {
        for (int i = 0; i < pop_size; i++) {
            score_key(pop[i], fault, ranked[i].key);
            ranked[i].idx = i;
        }
        stable_sort(ranked, pop_size, pop);
        if (key_gt(ranked[0].key, best_key)) {
            best_key[0] = ranked[0].key[0];
            best_key[1] = ranked[0].key[1];
            best_key[2] = ranked[0].key[2];
            best_gen = gen;
            copy_genome(best, pop[ranked[0].idx]);
        }

        int best_fit = sr_fitness(best, fault);
        int acc = accepted(best, fault);
        if (csv) {
            fprintf(csv, "%d,%d,%d,%02x,%02x,%d,%d,%d,%d,",
                    gen,
                    best_gen,
                    best_fit,
                    sr_truth_mask(best, fault),
                    sr_truth_mask(best, &SR_FAULT_NONE_OBJ),
                    sr_fitness(best, &SR_FAULT_DISABLE_A1),
                    sr_out_uses_node(best, SR_NODE_A1),
                    sr_out_uses_spare(best),
                    acc);
            write_genome(csv, best);
            fputc('\n', csv);
        }
        if (acc) break;

        int nnext = 0;
        for (; nnext < opt.elites && nnext < pop_size; nnext++) copy_genome(next[nnext], pop[ranked[nnext].idx]);
        while (nnext < pop_size) {
            int p1 = tournament_pick(ranked, pop_size, &rng, opt.tournament);
            uint8_t child[SR_GENOME_LEN];
            if (rng_chance(&rng, opt.crossover_ppm)) {
                int p2 = tournament_pick(ranked, pop_size, &rng, opt.tournament);
                crossover(pop[p1], pop[p2], child, &rng);
            } else {
                copy_genome(child, pop[p1]);
            }
            mutate(child, next[nnext], &rng, opt.init_mutation_ppm, opt.sel_mutation_ppm);
            nnext++;
        }
        uint8_t (*tmp)[SR_GENOME_LEN] = pop;
        pop = next;
        next = tmp;
    }

    if (csv) fclose(csv);
    printf("fitness=%d/8 mask=0x%02x best_gen=%d genome=",
           sr_fitness(best, fault), sr_truth_mask(best, fault), best_gen);
    write_genome(stdout, best);
    putchar('\n');

    free(pop);
    free(next);
    free(ranked);
    return 0;
}
