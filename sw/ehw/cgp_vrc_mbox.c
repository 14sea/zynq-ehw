/* EHW-1.1-fabric firmware: board-resident CGP GA using the fabric CGP VRC.
 *
 * Same fixed-routing 3x4 LUT4 genome and GA as cgp_eval.c / cgp_ga_mbox.c, but
 * fitness is measured through the cgp_vrc register interface at 0xF0000000.
 * Firmware writes 12 LUT INIT registers, drives the 16 truth-table inputs, and
 * reads the fabric output nibble. The host stub below mirrors the same register
 * protocol before any board build.
 *
 * Mailbox tags:
 *   0xD8000000            reached main
 *   0xD900ggrr            gen progress: gg=gen low8, rr=best rows_correct (/16)
 *   0xDA0000ff            best fitness (0..64)
 *   0xDB0000nn            active nodes
 *   0xDC0000rr            DONE rows (expect 16)
 *   0xA0..0xAB low16      champion genome[0..11]
 */
#include "cgp_kernel.h"
#include <stdint.h>
#include <string.h>

#ifdef CGP_HOST_STUB
#include <stdio.h>
static uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#else
#include <neorv32.h>
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define CGP_BASE      0xF0000000U
#define CGP_CTRL      0x000U
#define CGP_INPUT     0x008U
#define CGP_OUTPUT    0x00CU
#define CGP_INIT_BASE 0x040U

#define POP        64
#define GENS       200
#define ELITES     2
#define TOURNAMENT 3
#define CROSS_PPM  700000
#define MUT_PPM    30000
#define SEED       3

typedef struct { uint32_t state; } rng_t;
typedef struct { int fitness; int idx; } ranked_t;

#ifdef CGP_HOST_STUB
static uint16_t stub_genome[CGP_GENOME_LEN];
static uint8_t stub_input;

static uint32_t cgp_vrc_read(uint32_t off) {
    if (off == CGP_OUTPUT) {
        uint8_t out[4];
        cgp_eval_grid(stub_genome, stub_input, out);
        return (uint32_t)(out[0] | (out[1] << 1) | (out[2] << 2) | (out[3] << 3));
    }
    if (off >= CGP_INIT_BASE && off < CGP_INIT_BASE + CGP_GENOME_LEN * 4U)
        return stub_genome[(off - CGP_INIT_BASE) >> 2];
    return 0;
}

static void cgp_vrc_write(uint32_t off, uint32_t value) {
    if (off == CGP_CTRL && (value & 0x10U)) {
        memset(stub_genome, 0, sizeof(stub_genome));
        stub_input = 0;
    } else if (off == CGP_INPUT) {
        stub_input = (uint8_t)(value & 0x0FU);
    } else if (off >= CGP_INIT_BASE && off < CGP_INIT_BASE + CGP_GENOME_LEN * 4U) {
        stub_genome[(off - CGP_INIT_BASE) >> 2] = (uint16_t)value;
    }
}
#else
static uint32_t cgp_vrc_read(uint32_t off) {
    return *(volatile uint32_t *)(CGP_BASE + off);
}

static void cgp_vrc_write(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(CGP_BASE + off) = value;
}
#endif

static uint32_t rng_next(rng_t *rng) {
    uint32_t x = rng->state ? rng->state : 0x6D2B79F5u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng->state = x; return x;
}
static int rng_range(rng_t *rng, int hi) { return (int)(rng_next(rng) % (uint32_t)hi); }
static int rng_chance(rng_t *rng, int ppm) { return rng_range(rng, 1000000) < ppm; }
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
            if (rng_chance(rng, rate_ppm)) { value ^= (uint16_t)(1u << bit); changed = 1; }
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
        while (j >= 0 && better_ranked(&item, &ranked[j])) { ranked[j + 1] = ranked[j]; j--; }
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

static void cgp_vrc_load_genome(const uint16_t genome[CGP_GENOME_LEN]) {
    for (int i = 0; i < CGP_GENOME_LEN; i++)
        cgp_vrc_write(CGP_INIT_BASE + (uint32_t)i * 4U, genome[i]);
}

static uint8_t cgp_vrc_eval_idx(int idx) {
    cgp_vrc_write(CGP_INPUT, (uint32_t)(idx & 0x0F));
    return (uint8_t)(cgp_vrc_read(CGP_OUTPUT) & 0x0F);
}

static int cgp_vrc_evaluate(const uint16_t genome[CGP_GENOME_LEN]) {
    int correct = 0;
    cgp_vrc_load_genome(genome);
    for (int idx = 0; idx < 16; idx++) {
        uint8_t out = cgp_vrc_eval_idx(idx);
        for (int bit = 0; bit < 4; bit++)
            correct += (((out >> bit) & 1U) == cgp_gold_bit(bit, idx));
    }
    return correct;
}

static int cgp_vrc_rows_correct(const uint16_t genome[CGP_GENOME_LEN]) {
    int rows = 0;
    cgp_vrc_load_genome(genome);
    for (int idx = 0; idx < 16; idx++) {
        uint8_t out = cgp_vrc_eval_idx(idx);
        int ok = 1;
        for (int bit = 0; bit < 4; bit++)
            ok &= (((out >> bit) & 1U) == cgp_gold_bit(bit, idx));
        rows += ok;
    }
    return rows;
}

static void publish(uint32_t w) {
    MBOX = w;
#ifndef CGP_HOST_STUB
    for (volatile int d = 0; d < 4000; d++) { }
#endif
}

static uint16_t pop_a[POP][CGP_GENOME_LEN];
static uint16_t pop_b[POP][CGP_GENOME_LEN];
static ranked_t ranked[POP];

int main(void) {
#ifndef CGP_HOST_STUB
    for (volatile uint32_t d = 0; d < 300000u; d++) { }
#endif
    publish(0xD8000000u);
    if (cgp_vrc_evaluate(CGP_GOLDEN_GENOME) != CGP_FITNESS_MAX)
        publish(0xDF000001u);

    rng_t rng = { (uint32_t)SEED };
    uint16_t (*pop)[CGP_GENOME_LEN] = pop_a;
    uint16_t (*next)[CGP_GENOME_LEN] = pop_b;
    for (int i = 0; i < POP; i++) random_genome(&rng, pop[i]);

    uint16_t best_genome[CGP_GENOME_LEN];
    copy_genome(best_genome, pop[0]);
    int best_fit = cgp_vrc_evaluate(best_genome);

    for (int gen = 0; gen <= GENS; gen++) {
        for (int i = 0; i < POP; i++) { ranked[i].fitness = cgp_vrc_evaluate(pop[i]); ranked[i].idx = i; }
        stable_sort(ranked, POP);
        if (ranked[0].fitness > best_fit) {
            best_fit = ranked[0].fitness;
            copy_genome(best_genome, pop[ranked[0].idx]);
        }
        int rows = cgp_vrc_rows_correct(best_genome);
        publish(0xD9000000u | ((uint32_t)(gen & 0xFF) << 8) | (uint32_t)(rows & 0xFF));
        publish(0xDA000000u | (uint32_t)(best_fit & 0xFFFF));
        if (best_fit >= CGP_FITNESS_MAX) break;

        for (int i = 0; i < ELITES; i++) copy_genome(next[i], pop[ranked[i].idx]);
        int nnext = ELITES;
        while (nnext < POP) {
            int p1 = tournament_pick(ranked, POP, &rng, TOURNAMENT);
            uint16_t child[CGP_GENOME_LEN];
            if (rng_chance(&rng, CROSS_PPM)) {
                int p2 = tournament_pick(ranked, POP, &rng, TOURNAMENT);
                crossover(pop[p1], pop[p2], child, &rng);
            } else {
                copy_genome(child, pop[p1]);
            }
            mutate(child, next[nnext], &rng, MUT_PPM);
            nnext++;
        }
        uint16_t (*tmp)[CGP_GENOME_LEN] = pop; pop = next; next = tmp;
    }

    int rows = cgp_vrc_rows_correct(best_genome);
    int nodes = cgp_active_nodes(best_genome);

#ifdef CGP_HOST_STUB
    printf("CGP_VRC champion rows=%d/16 fitness=%d/64 nodes=%d genome=", rows, best_fit, nodes);
    for (int i = 0; i < CGP_GENOME_LEN; i++) printf("%s%04x", i ? " " : "", best_genome[i]);
    printf("\n");
    return rows == 16 ? 0 : 1;
#else
    for (;;) {
        publish(0xDC000000u | (uint32_t)(rows & 0xFF));
        publish(0xDB000000u | (uint32_t)(nodes & 0xFF));
        publish(0xDA000000u | (uint32_t)(best_fit & 0xFFFF));
        for (int i = 0; i < CGP_GENOME_LEN; i++)
            publish(((uint32_t)(0xA0 + i) << 24) | (uint32_t)best_genome[i]);
    }
#endif
}
