#ifndef MEMETIC_STRUCT_KERNEL_H
#define MEMETIC_STRUCT_KERNEL_H

#include "memetic_kernel.h"
#include "spare_route_kernel.h"

#include <stdint.h>

/*
 * EHW-5.1 frozen first hybrid genome contract:
 *
 * bytes  0..15  EHW-3 spare-route feature genome
 * bytes 16..39  EHW-4 INT8 seed-weight genome
 *
 * The spare-route genome uses the exact LUT/select decode in
 * spare_route_kernel.h. The weight genome uses the exact Q8.8 master-weight
 * decode and fixed-point SGD path in memetic_kernel.h.
 */
#define MS_HYBRID_GENOME_LEN (SR_GENOME_LEN + EHW_GENOME_LEN)
#define MS_MAX_POP 64
#define MS_DEFAULT_FEATURE_MIN_BALANCE 8
#define MS_DEFAULT_FEATURE_PENALTY 50000

typedef enum {
    MS_COUPLING_REPLACE_X3 = 0,
    MS_COUPLING_GATE_X3 = 1,
    MS_COUPLING_BIAS_X3 = 2,
} ms_coupling_t;

typedef enum {
    MS_MODE_LAMARCKIAN = 0,
    MS_MODE_LAMARCKIAN_PRESSURE = 1,
    MS_MODE_NO_ADAPT = 2,
} ms_mode_t;

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
    int struct_mutation_ppm;
    int struct_init_mutation_ppm;
    int struct_sel_mutation_ppm;
    int feature_min_balance;
    int feature_penalty;
} ms_options_t;

typedef struct {
    uint8_t sr[SR_GENOME_LEN];
    int8_t weight[EHW_GENOME_LEN];
} ms_candidate_t;

typedef struct {
    ehw_score_t select_score;
    ehw_score_t pre_score;
    ehw_score_t post_score;
    uint8_t sr[SR_GENOME_LEN];
    int8_t weight_for_next[EHW_GENOME_LEN];
    int8_t pre_weight[EHW_GENOME_LEN];
    int8_t post_weight[EHW_GENOME_LEN];
    uint64_t feature_mask;
    int feature_ones;
    int feature_penalty;
} ms_eval_t;

typedef struct {
    const char *mode;
    const char *coupling;
    ehw_score_t best_score;
    int first_40;
    uint8_t sr[SR_GENOME_LEN];
    int8_t pre_weight[EHW_GENOME_LEN];
    int8_t post_weight[EHW_GENOME_LEN];
    uint64_t feature_mask;
    int feature_ones;
    int feature_penalty;
    int degraded_correct;
    int repaired_correct;
} ms_result_t;

static const uint8_t MS_SR_MAJORITY[SR_GENOME_LEN] = {
    0x0a, 0x0a, 0x0a, 0x00, 0xe8, 0, 0, 1, 1, 2, 2, 3, 3, 0, 1, 2
};

static const uint8_t MS_SR_REPAIR[SR_GENOME_LEN] = {
    0x0a, 0x00, 0x0a, 0x0a, 0xe8, 0, 0, 3, 3, 2, 2, 1, 1, 0, 3, 2
};

static inline int ms_clamp_i8(int v) {
    return v < -128 ? -128 : (v > 127 ? 127 : v);
}

static inline int ms_feature_ones(uint64_t mask) {
    int n = 0;
    for (int i = 0; i < EHW_NTEST; i++) n += (int)((mask >> i) & 1ull);
    return n;
}

static inline int ms_structural_penalty(uint64_t mask, const ms_options_t *opt) {
    int ones = ms_feature_ones(mask);
    int balance = ones < (EHW_NTEST - ones) ? ones : (EHW_NTEST - ones);
    int deficit = opt->feature_min_balance - balance;
    return deficit > 0 ? deficit * opt->feature_penalty : 0;
}

#endif
