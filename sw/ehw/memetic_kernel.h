#ifndef MEMETIC_KERNEL_H
#define MEMETIC_KERNEL_H

#include "ehw_kernel.h"

#include <stdint.h>
#include <string.h>

#define MEM_FRAC 8
#define MEM_ONE (1 << MEM_FRAC)
#define MEM_WSHIFT 2
#define MEM_XSHIFT 2
#define MEM_DSHIFT 2
#define MEM_LR_SHIFT 7
#define MEM_ERR_CLAMP (1 << 20)
#define MEM_DELTA_CLAMP (1 << 14)
#define MEM_QMIN (-(1 << 15))
#define MEM_QMAX ((1 << 15) - 1)

typedef struct {
    uint32_t state;
} mem_rng_t;

typedef struct {
    ehw_score_t select_score;
    ehw_score_t pre_score;
    ehw_score_t post_score;
    int8_t genome_for_next[EHW_GENOME_LEN];
    int8_t pre_genome[EHW_GENOME_LEN];
    int8_t post_genome[EHW_GENOME_LEN];
} mem_eval_t;

typedef struct {
    const char *mode;
    int8_t best_genome[EHW_GENOME_LEN];
    int8_t best_pre_genome[EHW_GENOME_LEN];
    int8_t best_post_genome[EHW_GENOME_LEN];
    ehw_score_t best_pre_score;
    ehw_score_t best_post_score;
    int first_40;
} mem_mode_result_t;

typedef struct {
    int seed;
    int population;
    int generations;
    int adapt_epochs;
    int lr_shift;
    int init_span;
    int elites;
    int tournament;
    int crossover_ppm;
    int mutation_ppm;
    int mutation_step;
} mem_options_t;

typedef enum {
    MEM_MODE_PURE_GA = 0,
    MEM_MODE_BALDWINIAN = 1,
    MEM_MODE_LAMARCKIAN = 2,
} mem_mode_t;

static inline uint32_t mem_rng_next(mem_rng_t *rng) {
    uint32_t x = rng->state ? rng->state : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

static inline int mem_rng_range(mem_rng_t *rng, int hi) {
    return (int)(mem_rng_next(rng) % (uint32_t)hi);
}

static inline int mem_rng_int(mem_rng_t *rng, int lo, int hi) {
    return lo + mem_rng_range(rng, hi - lo + 1);
}

static inline int mem_rng_chance(mem_rng_t *rng, int ppm) {
    return mem_rng_range(rng, 1000000) < ppm;
}

static inline int32_t mem_clamp32(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline int32_t mem_sat16(int32_t v) {
    return mem_clamp32(v, MEM_QMIN, MEM_QMAX);
}

static inline int32_t mem_qmul(int32_t a, int32_t b) {
    return (int32_t)((((int64_t)a * (int64_t)b) + (1 << (MEM_FRAC - 1))) >> MEM_FRAC);
}

static inline int32_t mem_leaky_d(int32_t z) {
    return (z >= 0) ? MEM_ONE : (MEM_ONE >> EHW_K);
}

static inline void mem_copy_genome(int8_t dst[EHW_GENOME_LEN], const int8_t src[EHW_GENOME_LEN]) {
    memcpy(dst, src, EHW_GENOME_LEN);
}

static inline int mem_sat_count(const int8_t genome[EHW_GENOME_LEN]) {
    int n = 0;
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        n += (genome[i] == 127 || genome[i] == -128);
    }
    return n;
}

static inline void mem_random_genome(mem_rng_t *rng, int8_t genome[EHW_GENOME_LEN], int span) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        genome[i] = (int8_t)mem_rng_int(rng, -span, span);
    }
}

static inline void mem_mutate(const int8_t in[EHW_GENOME_LEN], int8_t out[EHW_GENOME_LEN],
                              mem_rng_t *rng, int rate_ppm, int step) {
    mem_copy_genome(out, in);
    int changed = 0;
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        int value = out[i];
        if (mem_rng_chance(rng, rate_ppm)) {
            if (mem_rng_chance(rng, 250000)) {
                value ^= 1 << mem_rng_range(rng, 8);
                if (value >= 128) value -= 256;
            } else {
                value += mem_rng_int(rng, -step, step);
            }
            out[i] = ehw_clamp_i8(value);
            changed = 1;
        }
    }
    if (!changed) {
        int i = mem_rng_range(rng, EHW_GENOME_LEN);
        int delta = (mem_rng_range(rng, 2) == 0 ? -1 : 1) * mem_rng_int(rng, 1, step);
        out[i] = ehw_clamp_i8((int)out[i] + delta);
    }
}

static inline void mem_crossover(const int8_t a[EHW_GENOME_LEN], const int8_t b[EHW_GENOME_LEN],
                                 int8_t out[EHW_GENOME_LEN], mem_rng_t *rng) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        out[i] = mem_rng_chance(rng, 500000) ? a[i] : b[i];
    }
}

static inline void mem_seed_population(mem_rng_t *rng, int pop_size, int init_span,
                                       int8_t pop[][EHW_GENOME_LEN]) {
    for (int i = 0; i < pop_size; i++) {
        mem_random_genome(rng, pop[i], init_span);
    }
    mem_copy_genome(pop[0], EHW_TRAINED_GENOME);
    int n = pop_size < 8 ? pop_size : 8;
    for (int i = 1; i < n; i++) {
        mem_mutate(EHW_TRAINED_GENOME, pop[i], rng, 250000, 4);
    }
}

static inline void mem_master_from_genome(const int8_t genome[EHW_GENOME_LEN],
                                          int32_t w1[EHW_NH][EHW_NIN],
                                          int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int r = 0; r < EHW_NH; r++) {
        for (int c = 0; c < EHW_NIN; c++) {
            w1[r][c] = (int32_t)genome[r * EHW_NIN + c] << MEM_WSHIFT;
        }
    }
    const int base = EHW_NH * EHW_NIN;
    for (int r = 0; r < EHW_NOUT; r++) {
        for (int c = 0; c < EHW_NH; c++) {
            w2[r][c] = (int32_t)genome[base + r * EHW_NH + c] << MEM_WSHIFT;
        }
    }
}

static inline void mem_genome_from_master(const int32_t w1[EHW_NH][EHW_NIN],
                                          const int32_t w2[EHW_NOUT][EHW_NH],
                                          int8_t genome[EHW_GENOME_LEN]) {
    for (int r = 0; r < EHW_NH; r++) {
        for (int c = 0; c < EHW_NIN; c++) {
            genome[r * EHW_NIN + c] = ehw_q8(w1[r][c], MEM_WSHIFT);
        }
    }
    const int base = EHW_NH * EHW_NIN;
    for (int r = 0; r < EHW_NOUT; r++) {
        for (int c = 0; c < EHW_NH; c++) {
            genome[base + r * EHW_NH + c] = ehw_q8(w2[r][c], MEM_WSHIFT);
        }
    }
}

static inline int32_t mem_dot4_i8(const int8_t row[4], const int8_t x[4]) {
    return (int32_t)row[0] * x[0] + (int32_t)row[1] * x[1] +
           (int32_t)row[2] * x[2] + (int32_t)row[3] * x[3];
}

static inline uint8_t mem_forward_master(const int32_t w1[EHW_NH][EHW_NIN],
                                         const int32_t w2[EHW_NOUT][EHW_NH],
                                         const int8_t x[EHW_NIN],
                                         int32_t z1[EHW_NH],
                                         int32_t h[EHW_NH],
                                         int32_t z2[EHW_NOUT],
                                         int32_t y[EHW_NOUT]) {
    int8_t w1_i8[EHW_NH][EHW_NIN];
    int8_t w2_i8[EHW_NOUT][EHW_NH];
    int8_t h_i8[EHW_NH];
    for (int r = 0; r < EHW_NH; r++) {
        for (int c = 0; c < EHW_NIN; c++) w1_i8[r][c] = ehw_q8(w1[r][c], MEM_WSHIFT);
    }
    for (int r = 0; r < EHW_NOUT; r++) {
        for (int c = 0; c < EHW_NH; c++) w2_i8[r][c] = ehw_q8(w2[r][c], MEM_WSHIFT);
    }
    for (int r = 0; r < EHW_NH; r++) {
        int32_t acc = mem_dot4_i8(w1_i8[r], x);
        z1[r] = mem_sat16(((acc + (1 << 3)) >> 4) + EHW_B1[r]);
        h[r] = ehw_leaky(z1[r]);
        h_i8[r] = ehw_q8(h[r], EHW_XSHIFT_H);
    }
    for (int r = 0; r < EHW_NOUT; r++) {
        int32_t acc = mem_dot4_i8(w2_i8[r], h_i8);
        z2[r] = mem_sat16(((acc + (1 << 2)) >> 3) + EHW_B2[r]);
        y[r] = ehw_leaky(z2[r]);
    }
    return (y[1] > y[0]) ? 1 : 0;
}

static inline int32_t mem_sgd_epoch(int32_t w1[EHW_NH][EHW_NIN],
                                    int32_t w2[EHW_NOUT][EHW_NH],
                                    const int order[EHW_NTEST],
                                    int lr_shift) {
    int32_t sse = 0;
    for (int oi = 0; oi < EHW_NTEST; oi++) {
        int idx = order[oi];
        const int8_t *x = EHW_TEST_X[idx];
        uint8_t label = EHW_TEST_Y[idx];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        mem_forward_master(w1, w2, x, z1, h, z2, y);

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
        for (int i = 0; i < EHW_NOUT; i++) {
            for (int j = 0; j < EHW_NH; j++) dw2[i][j] = mem_qmul(d2[i], h[j]);
        }

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

static inline int32_t mem_adapt(const int8_t genome[EHW_GENOME_LEN],
                                int epochs, int lr_shift, int seed,
                                int8_t out_genome[EHW_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    mem_master_from_genome(genome, w1, w2);
    mem_rng_t rng = { (uint32_t)seed };
    int order[EHW_NTEST];
    int32_t last_sse = 0;
    for (int i = 0; i < EHW_NTEST; i++) order[i] = i;
    for (int ep = 0; ep < epochs; ep++) {
        for (int i = EHW_NTEST - 1; i > 0; i--) {
            int j = mem_rng_range(&rng, i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        last_sse = mem_sgd_epoch(w1, w2, order, lr_shift);
    }
    mem_genome_from_master(w1, w2, out_genome);
    return last_sse;
}

static inline void mem_eval_candidate(mem_mode_t mode, const int8_t genome[EHW_GENOME_LEN],
                                      int epochs, int lr_shift, int seed, mem_eval_t *out) {
    out->pre_score = ehw_evaluate(genome);
    mem_copy_genome(out->pre_genome, genome);
    if (mode == MEM_MODE_PURE_GA) {
        out->select_score = out->pre_score;
        out->post_score = out->pre_score;
        mem_copy_genome(out->genome_for_next, genome);
        mem_copy_genome(out->post_genome, genome);
        return;
    }
    mem_adapt(genome, epochs, lr_shift, seed, out->post_genome);
    out->post_score = ehw_evaluate(out->post_genome);
    out->select_score = out->post_score;
    if (mode == MEM_MODE_BALDWINIAN) mem_copy_genome(out->genome_for_next, genome);
    else mem_copy_genome(out->genome_for_next, out->post_genome);
}

#endif
