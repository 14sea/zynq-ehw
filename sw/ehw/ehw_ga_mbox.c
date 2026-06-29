// EHW-0.3 bridge firmware: board-resident deterministic GA with mailbox progress.
//
// Host stub:
//   cc -std=c99 -Wall -Wextra -DEHW_HOST_STUB -I sw/ehw -o /tmp/ehw_ga_mbox sw/ehw/ehw_ga_mbox.c
//   /tmp/ehw_ga_mbox runs/tests/ehw0_ga_mbox.csv
//
// Board path:
//   Uses the same VRC array access pattern as ehw_eval_mbox.c. The GA runs on
//   NEORV32; the PS only watches mailbox progress/champion words.

#include "ehw_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef EHW_HOST_STUB
#include <stdio.h>
static volatile uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#define FENCE do { } while (0)
#else
#include <neorv32.h>
#define TPU_BASE  0xF0000000U
#define TPU_CTRL    (*(volatile uint32_t *)(TPU_BASE + 0x00))
#define TPU_STATUS  (*(volatile uint32_t *)(TPU_BASE + 0x04))
#define TPU_W_ADDR  (*(volatile uint32_t *)(TPU_BASE + 0x08))
#define TPU_X_IN    (*(volatile uint32_t *)(TPU_BASE + 0x10))
#define TPU_W_DATA4 (*(volatile uint32_t *)(TPU_BASE + 0x14))
#define TPU_RES(r)  (*(volatile uint32_t *)(TPU_BASE + 0x20 + (r)*4))
#define MBOX        (*(volatile uint32_t *)0xF1000000U)
#define FENCE __asm__ volatile("fence" ::: "memory")
#endif

#ifndef EHW_GA_POP
#define EHW_GA_POP 32
#endif
#ifndef EHW_GA_GENS
#define EHW_GA_GENS 64
#endif
#ifndef EHW_GA_TARGET
#define EHW_GA_TARGET 40
#endif
#ifndef EHW_GA_INIT_SPAN
#define EHW_GA_INIT_SPAN 32
#endif
#ifndef EHW_GA_ELITES
#define EHW_GA_ELITES 2
#endif
#ifndef EHW_GA_TOURNAMENT
#define EHW_GA_TOURNAMENT 3
#endif
#ifndef EHW_GA_CROSSOVER_PPM
#define EHW_GA_CROSSOVER_PPM 700000
#endif
#ifndef EHW_GA_MUTATION_PPM
#define EHW_GA_MUTATION_PPM 30000
#endif
#ifndef EHW_GA_MUTATION_STEP
#define EHW_GA_MUTATION_STEP 8
#endif

#define MB_GA_BOOT       0xE8000000u
#define MB_GA_GEN(g, c)  (0xE9000000u | (((uint32_t)(g) & 0xFFu) << 8) | ((uint32_t)(c) & 0xFFu))
#define MB_GA_SSE(s)     (0xEA000000u | ((uint32_t)(s) & 0x00FFFFFFu))
#define MB_GA_FIT(f)     (0xEB000000u | ((uint32_t)(f) & 0x00FFFFFFu))
#define MB_GA_DONE       0xEC000000u

typedef struct {
    uint32_t state;
} rng_t;

typedef struct {
    ehw_score_t score;
    int idx;
} ranked_t;

static int8_t pop_a[EHW_GA_POP][EHW_GENOME_LEN];
static int8_t pop_b[EHW_GA_POP][EHW_GENOME_LEN];
static ranked_t ranked[EHW_GA_POP];

static void hold(void) {
#ifndef EHW_HOST_STUB
    for (volatile uint32_t d = 0; d < 300000u; d++) { }
#endif
}

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

static void copy_genome(int8_t dst[EHW_GENOME_LEN], const int8_t src[EHW_GENOME_LEN]) {
    memcpy(dst, src, EHW_GENOME_LEN);
}

static void random_genome(rng_t *rng, int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        genome[i] = (int8_t)rng_int(rng, -EHW_GA_INIT_SPAN, EHW_GA_INIT_SPAN);
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

#ifdef EHW_HOST_STUB
static void array_macc(const int8_t w[4][4], const int8_t x[4], int32_t acc[4]) {
    for (int r = 0; r < 4; r++) acc[r] = ehw_dot4(w[r], x);
}
#else
static void array_macc(const int8_t w[4][4], const int8_t x[4], int32_t acc[4]) {
    for (int r = 0; r < 4; r++) {
        TPU_W_ADDR  = (uint32_t)(r << 2);
        TPU_W_DATA4 = ((uint32_t)(uint8_t)w[r][3] << 24) |
                      ((uint32_t)(uint8_t)w[r][2] << 16) |
                      ((uint32_t)(uint8_t)w[r][1] <<  8) |
                       (uint32_t)(uint8_t)w[r][0];
    }
    TPU_CTRL = 0x10;
    TPU_X_IN = ((uint32_t)(uint8_t)x[3] << 24) |
               ((uint32_t)(uint8_t)x[2] << 16) |
               ((uint32_t)(uint8_t)x[1] <<  8) |
                (uint32_t)(uint8_t)x[0];
    FENCE;
    TPU_CTRL = 0x01;
    while (!(TPU_STATUS & 1)) { }
    for (int r = 0; r < 4; r++) acc[r] = (int32_t)TPU_RES(r);
}
#endif

static uint8_t forward_vrc(const int8_t genome[EHW_GENOME_LEN], const int8_t x[4], int32_t y[2]) {
    int8_t w1[4][4];
    int8_t w2[4][4] = {{0}};
    int8_t hidden[4];
    int32_t acc[4];

    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            w1[r][c] = genome[r * 4 + c];
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 4; c++)
            w2[r][c] = genome[16 + r * 4 + c];

    array_macc(w1, x, acc);
    for (int r = 0; r < 4; r++) {
        int32_t hp = (acc[r] + (1 << 3)) >> 4;
        hidden[r] = ehw_q8(ehw_leaky(ehw_sat16(hp + EHW_B1[r])), EHW_XSHIFT_H);
    }

    array_macc(w2, hidden, acc);
    for (int r = 0; r < 2; r++) {
        int32_t yp = (acc[r] + (1 << 2)) >> 3;
        y[r] = ehw_leaky(ehw_sat16(yp + EHW_B2[r]));
    }
    return (y[1] > y[0]) ? 1 : 0;
}

static ehw_score_t evaluate_vrc(const int8_t genome[EHW_GENOME_LEN]) {
    ehw_score_t score = {0, 0, 0};
    for (int i = 0; i < EHW_NTEST; i++) {
        int32_t y[2];
        uint8_t pred = forward_vrc(genome, EHW_TEST_X[i], y);
        score.correct += (pred == EHW_TEST_Y[i]);
        for (int k = 0; k < 2; k++) {
            int32_t target = (k == EHW_TEST_Y[i]) ? 256 : 0;
            int32_t err = y[k] - target;
            score.sse += (err * err + 128) >> 8;
        }
    }
    score.fitness = score.correct * 1000000 - score.sse;
    return score;
}

static int better_ranked(const ranked_t *a, const ranked_t *b) {
    if (a->score.fitness != b->score.fitness) return a->score.fitness > b->score.fitness;
    return a->idx < b->idx;
}

static void stable_sort(int n) {
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

static int tournament_pick(rng_t *rng) {
    int best = rng_range(rng, EHW_GA_POP);
    for (int i = 1; i < EHW_GA_TOURNAMENT; i++) {
        int cur = rng_range(rng, EHW_GA_POP);
        if (better_ranked(&ranked[cur], &ranked[best])) best = cur;
    }
    return ranked[best].idx;
}

static void publish_genome(const int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < 8; i++) {
        uint32_t b0 = (uint8_t)genome[i * 3 + 0];
        uint32_t b1 = (uint8_t)genome[i * 3 + 1];
        uint32_t b2 = (uint8_t)genome[i * 3 + 2];
        MBOX = ((uint32_t)(0xD0 + i) << 24) | (b0 << 16) | (b1 << 8) | b2;
        hold();
    }
}

static void publish_progress(int gen, ehw_score_t score) {
    MBOX = MB_GA_GEN(gen, score.correct);
    hold();
    MBOX = MB_GA_SSE(score.sse);
    hold();
    MBOX = MB_GA_FIT(score.fitness);
    hold();
}

#ifdef EHW_HOST_STUB
static void write_genome_csv(FILE *f, const int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%d", genome[i]);
    }
}
#endif

int main(int argc, char **argv) {
#ifndef EHW_HOST_STUB
    (void)argc;
    (void)argv;
    neorv32_rte_setup();
#endif

    rng_t rng = {3u};
    int8_t (*pop)[EHW_GENOME_LEN] = pop_a;
    int8_t (*next)[EHW_GENOME_LEN] = pop_b;
    int8_t best_genome[EHW_GENOME_LEN];
    ehw_score_t best_score;

    MBOX = MB_GA_BOOT;
    hold();

    for (int i = 0; i < EHW_GA_POP; i++) random_genome(&rng, pop[i]);
    copy_genome(pop[0], EHW_TRAINED_GENOME);
    for (int i = 1; i < 8; i++) {
        mutate(EHW_TRAINED_GENOME, pop[i], &rng, 250000, 4);
    }
    copy_genome(best_genome, pop[0]);
    best_score = evaluate_vrc(best_genome);

#ifdef EHW_HOST_STUB
    FILE *csv = NULL;
    if (argc > 1) {
        csv = fopen(argv[1], "w");
        if (!csv) return 2;
        fputs("gen,best_correct,best_acc,best_sse,best_fitness,genome\n", csv);
    }
#endif

    for (int gen = 0; gen <= EHW_GA_GENS; gen++) {
        for (int i = 0; i < EHW_GA_POP; i++) {
            ranked[i].score = evaluate_vrc(pop[i]);
            ranked[i].idx = i;
        }
        stable_sort(EHW_GA_POP);
        if (ranked[0].score.fitness > best_score.fitness) {
            best_score = ranked[0].score;
            copy_genome(best_genome, pop[ranked[0].idx]);
        }

#ifdef EHW_HOST_STUB
        if (csv) {
            fprintf(csv, "%d,%d,%.4f,%d,%d,", gen, best_score.correct,
                    (double)best_score.correct / (double)EHW_NTEST,
                    best_score.sse, best_score.fitness);
            write_genome_csv(csv, best_genome);
            fputc('\n', csv);
        }
#endif
        publish_progress(gen, best_score);
        if (best_score.correct >= EHW_GA_TARGET) break;

        for (int i = 0; i < EHW_GA_ELITES; i++) copy_genome(next[i], pop[ranked[i].idx]);
        int nnext = EHW_GA_ELITES;
        while (nnext < EHW_GA_POP) {
            int p1 = tournament_pick(&rng);
            int8_t child[EHW_GENOME_LEN];
            if (rng_chance(&rng, EHW_GA_CROSSOVER_PPM)) {
                int p2 = tournament_pick(&rng);
                crossover(pop[p1], pop[p2], child, &rng);
            } else {
                copy_genome(child, pop[p1]);
            }
            mutate(child, next[nnext], &rng, EHW_GA_MUTATION_PPM, EHW_GA_MUTATION_STEP);
            nnext++;
        }
        int8_t (*tmp)[EHW_GENOME_LEN] = pop;
        pop = next;
        next = tmp;
    }

#ifdef EHW_HOST_STUB
    if (csv) fclose(csv);
    printf("board_ga_stub correct=%d sse=%d fitness=%d\n",
           best_score.correct, best_score.sse, best_score.fitness);
#endif

    MBOX = MB_GA_DONE | ((uint32_t)best_score.correct & 0xFFu);
    hold();
    publish_genome(best_genome);

#ifdef EHW_HOST_STUB
    return (best_score.correct == 40 && best_score.sse == 4799 && best_score.fitness == 39995201) ? 0 : 1;
#else
    while (1) {
        publish_progress(EHW_GA_GENS, best_score);
        publish_genome(best_genome);
    }
#endif
}
