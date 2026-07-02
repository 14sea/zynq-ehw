/* EHW-1.1 board-resident CGP GA on NEORV32 (software LUT-grid eval).
 *
 * Evolves a 2-bit multiplier as a fixed-routing CGP grid (cgp_kernel.h), runs the
 * GA resident on the soft-core, and publishes progress + champion over the PS
 * mailbox at 0x41200000. The GA (rng/operators/loop/defaults) is COPIED VERBATIM
 * from sw/ehw/cgp_eval.c so it is bit-exact with the host oracle; the only changes
 * are static arrays (no malloc) and mailbox output (no CSV).
 *
 * Host gate (must match sim/oracle_cgp.py / cgp_eval.c before any board build):
 *   cc -std=c99 -DCGP_HOST_STUB -I sw/ehw -o /tmp/cgp_ga_mbox sw/ehw/cgp_ga_mbox.c
 *   /tmp/cgp_ga_mbox            # prints champion; compare to the host champion
 *
 * Mailbox tags (single 32-bit word at 0x41200000, cycled):
 *   0xC8000000            reached main
 *   0xC900ggrr            gen progress: gg=gen low8, rr=best rows_correct (/16)
 *   0xCA0000ff            best fitness (0..64)
 *   0xCB0000nn            active nodes
 *   0xCC0000rr            DONE: rr=final rows_correct (expect 16)
 *   0xB0..0xBB low16      champion genome[0..11] (one uint16 LUT-INIT per word)
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
/* firmware writes the PL-side mailbox input at 0xF1000000; PS reads it back through
 * AXI-GPIO at 0x41200000 (see docs/hw_notes.md). */
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define POP        64
#define GENS       200
#define ELITES     2
#define TOURNAMENT 3
#define CROSS_PPM  700000
#define MUT_PPM    30000
#define SEED       3

typedef struct { uint32_t state; } rng_t;
typedef struct { int fitness; int idx; } ranked_t;

/* ---- VERBATIM from cgp_eval.c (bit-exact) ---- */
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
/* ---- end verbatim ---- */

static uint16_t pop_a[POP][CGP_GENOME_LEN];
static uint16_t pop_b[POP][CGP_GENOME_LEN];
static ranked_t ranked[POP];

static void publish(uint32_t w) {
    MBOX = w;
#ifndef CGP_HOST_STUB
    for (volatile int d = 0; d < 4000; d++) { }   /* let a slow PS md poll catch it */
#endif
}

int main(void) {
#ifndef CGP_HOST_STUB
    for (volatile uint32_t d = 0; d < 300000u; d++) { }   /* historical "post-config settle" (~6 ms) — later shown unnecessary (the xpart-M7.1 settle was an image_gen-bug artifact, see sw/patches/image_gen_lma_fix/); kept to preserve the board-verified binary */
#endif
    publish(0xC8000000u);

    rng_t rng = { (uint32_t)SEED };
    uint16_t (*pop)[CGP_GENOME_LEN] = pop_a;
    uint16_t (*next)[CGP_GENOME_LEN] = pop_b;
    for (int i = 0; i < POP; i++) random_genome(&rng, pop[i]);

    uint16_t best_genome[CGP_GENOME_LEN];
    copy_genome(best_genome, pop[0]);
    int best_fit = cgp_evaluate(best_genome);

    for (int gen = 0; gen <= GENS; gen++) {
        for (int i = 0; i < POP; i++) { ranked[i].fitness = cgp_evaluate(pop[i]); ranked[i].idx = i; }
        stable_sort(ranked, POP);
        if (ranked[0].fitness > best_fit) {
            best_fit = ranked[0].fitness;
            copy_genome(best_genome, pop[ranked[0].idx]);
        }
        int rows = cgp_rows_correct(best_genome);
        publish(0xC9000000u | ((uint32_t)(gen & 0xFF) << 8) | (uint32_t)(rows & 0xFF));
        publish(0xCA000000u | (uint32_t)(best_fit & 0xFFFF));
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

    int rows = cgp_rows_correct(best_genome);
    int nodes = cgp_active_nodes(best_genome);

#ifdef CGP_HOST_STUB
    printf("CGP_BOARD champion rows=%d/16 fitness=%d/64 nodes=%d genome=", rows, best_fit, nodes);
    for (int i = 0; i < CGP_GENOME_LEN; i++) printf("%s%04x", i ? " " : "", best_genome[i]);
    printf("\n");
    return rows == 16 ? 0 : 1;
#else
    /* loop publishing the final result so a slow poller catches everything */
    for (;;) {
        publish(0xCC000000u | (uint32_t)(rows & 0xFF));
        publish(0xCB000000u | (uint32_t)(nodes & 0xFF));
        publish(0xCA000000u | (uint32_t)(best_fit & 0xFFFF));
        for (int i = 0; i < CGP_GENOME_LEN; i++)
            publish(((uint32_t)(0xB0 + i) << 24) | (uint32_t)best_genome[i]);
    }
#endif
}
