/* EHW-3.2 firmware: board-resident GA using the fabric spare-routing VRC.
 *
 * Same frozen 16-byte genome contract and GA as spare_route_eval.c, but fitness
 * is measured through the spare_route_vrc register interface at 0xF0000000.
 * Firmware writes the 16 config bytes, sets the fault register, and reads the
 * fabric-computed mask/fitness. The host stub mirrors the register protocol
 * before any board build.
 *
 * Mailbox tags:
 *   0xE3200000            reached main
 *   0xE32100mm            no-fault champion mask
 *   0xE32200ff            no-fault champion fitness
 *   0xE32300mm            degraded mask after DISABLE_NODE(A1)
 *   0xE32400ff            degraded fitness
 *   0xE32500mm            repaired mask under DISABLE_NODE(A1)
 *   0xE32600ff            repaired fitness
 *   0xE32700uu            repaired uses bits: bit0=A1, bit1=AS
 *   0xE3280001            DONE/PASS
 *   0xB0..0xBF low8       no-fault champion genome[0..15]
 *   0xC0..0xCF low8       repaired champion genome[0..15]
 */
#include "spare_route_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef SR_HOST_STUB
#include <stdio.h>
static uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#else
#include <neorv32.h>
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define SR_BASE      0xF0000000U
#define SR_CTRL      0x000U
#define SR_INPUT     0x008U
#define SR_OUTPUT    0x00CU
#define SR_MASK      0x010U
#define SR_FITNESS   0x014U
#define SR_FAULT     0x018U
#define SR_USES      0x01CU
#define SR_GEN_BASE  0x040U

#define POP          256
#define GENS         1000
#define ELITES       4
#define TOURNAMENT   3
#define CROSS_PPM    700000
#define INIT_MUT_PPM 50000
#define SEL_MUT_PPM  30000
#define SEED_NOFAULT 3
#define SEED_REPAIR  4

typedef struct { uint32_t state; } rng_t;
typedef struct { int key[3]; int idx; } ranked_t;

#ifdef SR_HOST_STUB
static uint8_t stub_genome[SR_GENOME_LEN];
static uint8_t stub_input;
static sr_fault_t stub_fault = {SR_FAULT_NONE, -1, -1, -1, -1};

static sr_fault_t unpack_fault(uint32_t value) {
    sr_fault_t f;
    f.kind = (sr_fault_kind_t)(value & 7u);
    f.node = (int)((value >> 4) & 7u);
    f.route_section = (int)((value >> 8) & 3u);
    f.route_idx = (int)((value >> 10) & 15u);
    f.route_mux = (int)((value >> 14) & 15u);
    return f;
}

static uint32_t pack_fault(const sr_fault_t *f) {
    return ((uint32_t)f->kind & 7u) |
           (((uint32_t)f->node & 7u) << 4) |
           (((uint32_t)f->route_section & 3u) << 8) |
           (((uint32_t)f->route_idx & 15u) << 10) |
           (((uint32_t)f->route_mux & 15u) << 14);
}

static uint32_t sr_vrc_read(uint32_t off) {
    if (off == SR_OUTPUT)
        return sr_eval_row(stub_genome, stub_input, &stub_fault);
    if (off == SR_MASK)
        return sr_truth_mask(stub_genome, &stub_fault);
    if (off == SR_FITNESS)
        return (uint32_t)sr_fitness(stub_genome, &stub_fault);
    if (off == SR_FAULT)
        return pack_fault(&stub_fault);
    if (off == SR_USES)
        return (uint32_t)(sr_out_uses_node(stub_genome, SR_NODE_A1) |
                          (sr_out_uses_spare(stub_genome) << 1));
    if (off >= SR_GEN_BASE && off < SR_GEN_BASE + SR_GENOME_LEN * 4U)
        return stub_genome[(off - SR_GEN_BASE) >> 2];
    return 0;
}

static void sr_vrc_write(uint32_t off, uint32_t value) {
    if (off == SR_CTRL && (value & 0x10U)) {
        memset(stub_genome, 0, sizeof(stub_genome));
        stub_input = 0;
        stub_fault = SR_FAULT_NONE_OBJ;
    } else if (off == SR_INPUT) {
        stub_input = (uint8_t)(value & 7U);
    } else if (off == SR_FAULT) {
        stub_fault = unpack_fault(value);
    } else if (off >= SR_GEN_BASE && off < SR_GEN_BASE + SR_GENOME_LEN * 4U) {
        stub_genome[(off - SR_GEN_BASE) >> 2] = (uint8_t)value;
    }
}
#else
static uint32_t sr_vrc_read(uint32_t off) {
    return *(volatile uint32_t *)(SR_BASE + off);
}

static void sr_vrc_write(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(SR_BASE + off) = value;
}
#endif

static uint32_t pack_fault_word(const sr_fault_t *f) {
    return ((uint32_t)f->kind & 7u) |
           (((uint32_t)f->node & 7u) << 4) |
           (((uint32_t)f->route_section & 3u) << 8) |
           (((uint32_t)f->route_idx & 15u) << 10) |
           (((uint32_t)f->route_mux & 15u) << 14);
}

static uint32_t rng_next(rng_t *rng) {
    uint32_t x = rng->state ? rng->state : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static int rng_range(rng_t *rng, int hi) { return (int)(rng_next(rng) % (uint32_t)hi); }
static int rng_chance(rng_t *rng, int ppm) { return rng_range(rng, 1000000) < ppm; }
static void copy_genome(uint8_t dst[SR_GENOME_LEN], const uint8_t src[SR_GENOME_LEN]) {
    memcpy(dst, src, SR_GENOME_LEN);
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
    genome[13] = 0;
    genome[14] = use_spare ? 3 : 1;
    genome[15] = 2;
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

static void sr_vrc_load_genome(const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++)
        sr_vrc_write(SR_GEN_BASE + (uint32_t)i * 4U, genome[i]);
}

static void sr_vrc_set_fault(const sr_fault_t *fault) {
    sr_vrc_write(SR_FAULT, pack_fault_word(fault));
}

static int sr_vrc_fitness(const uint8_t genome[SR_GENOME_LEN], const sr_fault_t *fault) {
    sr_vrc_load_genome(genome);
    sr_vrc_set_fault(fault);
    return (int)(sr_vrc_read(SR_FITNESS) & 0x0FU);
}

static uint8_t sr_vrc_mask(const uint8_t genome[SR_GENOME_LEN], const sr_fault_t *fault) {
    sr_vrc_load_genome(genome);
    sr_vrc_set_fault(fault);
    return (uint8_t)(sr_vrc_read(SR_MASK) & 0xFFU);
}

static uint32_t sr_vrc_uses(const uint8_t genome[SR_GENOME_LEN]) {
    sr_vrc_load_genome(genome);
    return sr_vrc_read(SR_USES) & 3U;
}

static int out_uses_node_direct(const uint8_t genome[SR_GENOME_LEN], int node) {
    return sr_out_uses_node(genome, node);
}

static void score_key(const uint8_t genome[SR_GENOME_LEN], const sr_fault_t *fault, int key[3]) {
    int fit = sr_vrc_fitness(genome, fault);
    key[0] = fit;
    if (fault->kind == SR_FAULT_NONE) {
        key[1] = fit - sr_vrc_fitness(genome, &SR_FAULT_DISABLE_A1);
        key[2] = out_uses_node_direct(genome, SR_NODE_A1);
    } else {
        key[1] = out_uses_node_direct(genome, SR_NODE_AS);
        key[2] = !out_uses_node_direct(genome, SR_NODE_A1);
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

static int ranked_gt(const ranked_t *a, const ranked_t *b, const uint8_t (*pop)[SR_GENOME_LEN]) {
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
    if (fault->kind == SR_FAULT_NONE)
        return sr_vrc_fitness(best, fault) == SR_FITNESS_MAX &&
               sr_vrc_fitness(best, &SR_FAULT_DISABLE_A1) < SR_FITNESS_MAX;
    return sr_vrc_fitness(best, fault) == SR_FITNESS_MAX &&
           out_uses_node_direct(best, SR_NODE_AS) &&
           !out_uses_node_direct(best, SR_NODE_A1);
}

static uint8_t pop_a[POP][SR_GENOME_LEN];
static uint8_t pop_b[POP][SR_GENOME_LEN];
static ranked_t ranked[POP];

static int run_ga(int seed, const sr_fault_t *fault, const uint8_t inject[SR_GENOME_LEN],
                  uint8_t best[SR_GENOME_LEN]) {
    rng_t rng = {(uint32_t)seed};
    uint8_t (*pop)[SR_GENOME_LEN] = pop_a;
    uint8_t (*next)[SR_GENOME_LEN] = pop_b;

    int n = 0;
    for (int i = 0; i < 16 && n < POP; i++) scaffold_genome(&rng, 0, pop[n++]);
    for (int i = 0; i < 16 && n < POP; i++) scaffold_genome(&rng, 1, pop[n++]);
    if (inject && n < POP) copy_genome(pop[n++], inject);
    while (n < POP) random_genome(&rng, pop[n++]);

    copy_genome(best, pop[0]);
    int best_key[3] = {-1, -1, -1};
    int best_gen = 0;

    for (int gen = 0; gen <= GENS; gen++) {
        for (int i = 0; i < POP; i++) {
            score_key(pop[i], fault, ranked[i].key);
            ranked[i].idx = i;
        }
        stable_sort(ranked, POP, pop);
        if (key_gt(ranked[0].key, best_key)) {
            best_key[0] = ranked[0].key[0];
            best_key[1] = ranked[0].key[1];
            best_key[2] = ranked[0].key[2];
            best_gen = gen;
            copy_genome(best, pop[ranked[0].idx]);
        }
        if (accepted(best, fault))
            return best_gen;

        int nnext = 0;
        for (; nnext < ELITES && nnext < POP; nnext++) copy_genome(next[nnext], pop[ranked[nnext].idx]);
        while (nnext < POP) {
            int p1 = tournament_pick(ranked, POP, &rng, TOURNAMENT);
            uint8_t child[SR_GENOME_LEN];
            if (rng_chance(&rng, CROSS_PPM)) {
                int p2 = tournament_pick(ranked, POP, &rng, TOURNAMENT);
                crossover(pop[p1], pop[p2], child, &rng);
            } else {
                copy_genome(child, pop[p1]);
            }
            mutate(child, next[nnext], &rng, INIT_MUT_PPM, SEL_MUT_PPM);
            nnext++;
        }
        uint8_t (*tmp)[SR_GENOME_LEN] = pop;
        pop = next;
        next = tmp;
    }
    return best_gen;
}

static void publish(uint32_t w) {
    MBOX = w;
#ifndef SR_HOST_STUB
    for (volatile int d = 0; d < 4000; d++) { }
#endif
}

static void publish_genome(uint32_t tag_base, const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++)
        publish(((tag_base + (uint32_t)i) << 24) | (uint32_t)genome[i]);
}

static uint8_t nofault_best[SR_GENOME_LEN];
static uint8_t repair_best[SR_GENOME_LEN];

int main(void) {
#ifndef SR_HOST_STUB
    for (volatile uint32_t d = 0; d < 300000u; d++) { }
#endif
    publish(0xE3200000u);

    int gen0 = run_ga(SEED_NOFAULT, &SR_FAULT_NONE_OBJ, 0, nofault_best);
    uint8_t nofault_mask = sr_vrc_mask(nofault_best, &SR_FAULT_NONE_OBJ);
    int nofault_fit = sr_vrc_fitness(nofault_best, &SR_FAULT_NONE_OBJ);
    uint8_t degraded_mask = sr_vrc_mask(nofault_best, &SR_FAULT_DISABLE_A1);
    int degraded_fit = sr_vrc_fitness(nofault_best, &SR_FAULT_DISABLE_A1);

    int gen1 = run_ga(SEED_REPAIR, &SR_FAULT_DISABLE_A1, nofault_best, repair_best);
    uint8_t repair_mask = sr_vrc_mask(repair_best, &SR_FAULT_DISABLE_A1);
    int repair_fit = sr_vrc_fitness(repair_best, &SR_FAULT_DISABLE_A1);
    uint32_t uses = sr_vrc_uses(repair_best);

    publish(0xE3210000u | nofault_mask);
    publish(0xE3220000u | (uint32_t)nofault_fit);
    publish(0xE3230000u | degraded_mask);
    publish(0xE3240000u | (uint32_t)degraded_fit);
    publish(0xE3250000u | repair_mask);
    publish(0xE3260000u | (uint32_t)repair_fit);
    publish(0xE3270000u | uses);
    publish_genome(0xB0u, nofault_best);
    publish_genome(0xC0u, repair_best);

#ifdef SR_HOST_STUB
    printf("SPARE_ROUTE_VRC nofault gen=%d fit=%d/8 mask=%02x degraded_fit=%d/8 degraded_mask=%02x\n",
           gen0, nofault_fit, nofault_mask, degraded_fit, degraded_mask);
    printf("SPARE_ROUTE_VRC repair gen=%d fit=%d/8 mask=%02x uses=%u genome=",
           gen1, repair_fit, repair_mask, (unsigned)uses);
    for (int i = 0; i < SR_GENOME_LEN; i++) printf("%s%02x", i ? " " : "", repair_best[i]);
    printf("\n");
    return (nofault_fit == 8 && degraded_fit < 8 && repair_fit == 8 &&
            repair_mask == SR_TARGET_MASK && uses == 2u) ? 0 : 1;
#else
    for (;;) {
        publish(0xE3280001u);
        publish(0xE3250000u | repair_mask);
        publish(0xE3260000u | (uint32_t)repair_fit);
        publish(0xE3270000u | uses);
    }
#endif
}

