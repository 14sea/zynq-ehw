/* EHW-5.4 firmware: same-boot hybrid structure ablation on the board.
 *
 * Default mode runs four EHW-5 arms in one firmware image and one boot:
 *   arm0 weight_only_lamarckian / none
 *   arm1 hybrid_lamarckian_pressure / bias_x3
 *   arm2 hybrid_no_adapt / gate_x3
 *   arm3 hybrid_lamarckian / bias_x3
 *
 * EHW-5.4b parameter-window contract:
 *   word0  magic = 0xE5400001
 *   word1  n_arms (1..8)
 *   word2  seed
 *   word3  population (2..16, fixed static buffer ceiling)
 *   word4  generations
 *   word5  adapt_epochs
 *   word6  feature_min_balance
 *   word7  feature_penalty
 *   word8+ arm descriptor = mode | coupling<<8 | flags<<16
 *
 * Descriptor mode:
 *   0 hybrid_lamarckian, 1 hybrid_lamarckian_pressure,
 *   2 hybrid_no_adapt, 3 weight_only_lamarckian
 * Descriptor coupling:
 *   0 replace_x3, 1 gate_x3, 2 bias_x3, 3 none
 * Descriptor flags:
 *   bit0 uses_structure, bit1 uses_adapt, bit2 uses_pressure.
 *
 * If magic is absent, the firmware uses the board-verified built-in table.
 * If magic is present but invalid, the run publishes FAIL rather than silently
 * falling back.
 *
 * Candidate evaluation uses the EHW-5.2 hardware paths:
 *   - spare-route VRC feature island at 0xF0000400
 *   - lite train-unit adaptation window at 0xF0000800
 *
 * Host mode (-DMEMETIC_STRUCT_GA_HOST_STUB) models the same MMIO protocol and
 * emits per-arm per-generation curves that must byte-match memetic_struct_eval.c.
 */

#include "memetic_struct_kernel.h"

#include <stdint.h>
#include <string.h>

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
#include <stdio.h>
#else
#include <neorv32.h>
typedef void FILE;
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define SR_BASE 0xF0000400U
#define TU_BASE 0xF0000800U
#define FB_BASE 0xF5000000U

#define SR_INPUT       2u
#define SR_OUTPUT      3u
#define SR_GEN(n)      (16u + (uint32_t)(n))

#define TU_INA(n)      (0u + (uint32_t)(n))
#define TU_Z(n)        (4u + (uint32_t)(n))
#define TU_T(n)        (8u + (uint32_t)(n))
#define TU_DW(n)       (12u + (uint32_t)(n))
#define TU_CMD         28u
#define TU_W1(n)       (32u + (uint32_t)(n))
#define TU_W2(n)       (48u + (uint32_t)(n))
#define TU_D2(n)       (64u + (uint32_t)(n))
#define TU_D1(n)       (68u + (uint32_t)(n))
#define TU_LOSS        76u
#define TU_BUSY        77u

#define TU_CMD_LOSS_D2 0x01u
#define TU_CMD_D1      0x02u
#define TU_CMD_UPD_L2  0x04u
#define TU_CMD_UPD_L1  0x08u
#define TU_CMD_CLR     0x10u

#define EHW54_PARAM_MAGIC 0xE5400001U
#define EHW54_SEED        3
#define EHW54_POP         16
#define EHW54_GENS        32
#define EHW54_ADAPT       1
#define EHW54_MAX_ARMS    8
#define EHW54_INIT_SPAN   32
#define EHW54_ELITES      2
#define EHW54_TOURN       3
#define EHW54_CROSS_PPM   700000
#define EHW54_MUT_PPM     30000
#define EHW54_MUT_STEP    8
#define EHW54_STRUCT_PPM  250000
#define EHW54_INIT_PPM    50000
#define EHW54_SEL_PPM     30000

typedef enum {
    EHW54_ARM_WEIGHT_ONLY = 0,
    EHW54_ARM_PRESSURE_BIAS = 1,
    EHW54_ARM_NO_ADAPT_GATE = 2,
    EHW54_ARM_LAMARCKIAN_BIAS = 3,
    EHW54_NARMS = 4,
} ehw54_arm_id_t;

typedef struct {
    uint8_t id;
    const char *mode;
    const char *coupling;
    ms_mode_t ms_mode;
    ms_coupling_t ms_coupling;
    uint8_t weight_only;
    uint8_t uses_structure;
    uint8_t uses_adapt;
    uint8_t uses_pressure;
    uint8_t has_expect;
    int expect_correct;
    int expect_sse;
    int expect_first40;
    int expect_ones;
    int expect_penalty;
    int expect_sat;
} ehw54_arm_t;

typedef struct {
    int correct;
    int sse;
    int first_40;
    int feature_ones;
    int feature_penalty;
    int sat_count;
} ehw54_result_t;

typedef struct {
    int n_arms;
    int seed;
    int population;
    int generations;
    int adapt_epochs;
    int feature_min_balance;
    int feature_penalty;
    uint8_t staged;
    uint8_t valid;
    ehw54_arm_t arms[EHW54_MAX_ARMS];
} ehw54_run_cfg_t;

static const ehw54_arm_t EHW54_ARMS[EHW54_NARMS] = {
    { EHW54_ARM_WEIGHT_ONLY, "weight_only_lamarckian", "none",
      MS_MODE_LAMARCKIAN, MS_COUPLING_BIAS_X3, 1, 0, 1, 0, 1, 40, 6116, 3, 0, 0, 0 },
    { EHW54_ARM_PRESSURE_BIAS, "hybrid_lamarckian_pressure", "bias_x3",
      MS_MODE_LAMARCKIAN_PRESSURE, MS_COUPLING_BIAS_X3, 0, 1, 1, 1, 1, 40, 4513, 2, 15, 0, 0 },
    { EHW54_ARM_NO_ADAPT_GATE, "hybrid_no_adapt", "gate_x3",
      MS_MODE_NO_ADAPT, MS_COUPLING_GATE_X3, 0, 1, 0, 0, 1, 40, 4615, 11, 39, 0, 0 },
    { EHW54_ARM_LAMARCKIAN_BIAS, "hybrid_lamarckian", "bias_x3",
      MS_MODE_LAMARCKIAN, MS_COUPLING_BIAS_X3, 0, 1, 1, 0, 1, 40, 5837, 5, 0, 0, 0 },
};

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
static uint8_t sr_genome_model[SR_GENOME_LEN];
static uint8_t sr_input_model;
static int32_t tu_ina[4], tu_z[4], tu_t[4], tu_dw[16], tu_w1[16], tu_w2[8];
static int32_t tu_d2_reg[2], tu_d1_reg[4], tu_loss;
static uint32_t fb_model[2048];

static int32_t tu_qmul(int32_t a, int32_t b) {
    return (int32_t)((((int64_t)a * (int64_t)b) + (1 << (MEM_FRAC - 1))) >> MEM_FRAC);
}

static int32_t tu_leaky_apply(int32_t x, int32_t z) {
    return tu_qmul(x, z >= 0 ? MEM_ONE : (MEM_ONE >> EHW_K));
}

static void tu_model_cmd(uint32_t cmd) {
    if (cmd & TU_CMD_LOSS_D2) {
        for (int i = 0; i < EHW_NOUT; i++) {
            int32_t e = mem_clamp32(tu_ina[i] - tu_t[i], -MEM_ERR_CLAMP, MEM_ERR_CLAMP - 1);
            tu_loss += tu_qmul(e, e);
            tu_d2_reg[i] = mem_clamp32(tu_leaky_apply(e, tu_z[i]), -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        }
    }
    if (cmd & TU_CMD_D1) {
        for (int i = 0; i < EHW_NH; i++) {
            tu_d1_reg[i] = mem_clamp32(tu_leaky_apply(tu_ina[i], tu_z[i]),
                                       -MEM_DELTA_CLAMP, MEM_DELTA_CLAMP - 1);
        }
    }
    if (cmd & TU_CMD_UPD_L2) {
        for (int i = 0; i < EHW_NOUT * EHW_NH; i++)
            tu_w2[i] = mem_sat16(tu_w2[i] - (tu_dw[i] >> MEM_LR_SHIFT));
    }
    if (cmd & TU_CMD_UPD_L1) {
        for (int i = 0; i < EHW_NH * EHW_NIN; i++)
            tu_w1[i] = mem_sat16(tu_w1[i] - (tu_dw[i] >> MEM_LR_SHIFT));
    }
    if (cmd & TU_CMD_CLR) tu_loss = 0;
}

static void sr_write(uint32_t word, uint32_t value) {
    if (word >= SR_GEN(0) && word <= SR_GEN(15)) sr_genome_model[word - SR_GEN(0)] = (uint8_t)value;
    else if (word == SR_INPUT) sr_input_model = (uint8_t)(value & 7u);
}

static uint32_t sr_read(uint32_t word) {
    if (word == SR_OUTPUT) return sr_eval_row(sr_genome_model, sr_input_model, &SR_FAULT_NONE_OBJ);
    return 0;
}

static void tu_write(uint32_t word, int32_t value) {
    if (word <= 3u) tu_ina[word] = value;
    else if (word >= 4u && word <= 7u) tu_z[word - 4u] = value;
    else if (word >= 8u && word <= 11u) tu_t[word - 8u] = value;
    else if (word >= 12u && word <= 27u) tu_dw[word - 12u] = value;
    else if (word == TU_CMD) tu_model_cmd((uint32_t)value);
    else if (word >= 32u && word <= 47u) tu_w1[word - 32u] = value;
    else if (word >= 48u && word <= 55u) tu_w2[word - 48u] = value;
}

static int32_t tu_read(uint32_t word) {
    if (word >= 32u && word <= 47u) return tu_w1[word - 32u];
    if (word >= 48u && word <= 55u) return tu_w2[word - 48u];
    if (word >= 64u && word <= 65u) return tu_d2_reg[word - 64u];
    if (word >= 68u && word <= 71u) return tu_d1_reg[word - 68u];
    if (word == TU_LOSS) return tu_loss;
    if (word == TU_BUSY) return 0;
    return 0;
}

static uint32_t fb_read(uint32_t word) {
    return word < 2048u ? fb_model[word] : 0;
}
#else
static void sr_write(uint32_t word, uint32_t value) {
    *(volatile uint32_t *)(SR_BASE + word * 4u) = value;
}

static uint32_t sr_read(uint32_t word) {
    return *(volatile uint32_t *)(SR_BASE + word * 4u);
}

static void tu_write(uint32_t word, int32_t value) {
    *(volatile uint32_t *)(TU_BASE + word * 4u) = (uint32_t)value;
}

static int32_t tu_read(uint32_t word) {
    return (int32_t)*(volatile uint32_t *)(TU_BASE + word * 4u);
}

static uint32_t fb_read(uint32_t word) {
    return *(volatile uint32_t *)(FB_BASE + word * 4u);
}
#endif

static ms_candidate_t pop[EHW54_POP];
static ms_candidate_t next_pop[EHW54_POP];
static ms_eval_t evaluated[EHW54_POP];
static int8_t mem_pop[EHW54_POP][EHW_GENOME_LEN];
static int8_t mem_next_pop[EHW54_POP][EHW_GENOME_LEN];
static mem_eval_t mem_evaluated[EHW54_POP];
static int ranked_idx[EHW54_POP];
static ehw54_result_t arm_results[EHW54_MAX_ARMS];

#ifndef MEMETIC_STRUCT_GA_HOST_STUB
static void publish(uint32_t word) {
    MBOX = word;
    for (volatile uint32_t d = 0; d < 4000u; d++) { }
}
#endif

static void tu_wait_idle(void) {
#ifndef MEMETIC_STRUCT_GA_HOST_STUB
    while (tu_read(TU_BUSY) & 1) { }
#endif
}

static void copy_sr(uint8_t dst[SR_GENOME_LEN], const uint8_t src[SR_GENOME_LEN]) {
    memcpy(dst, src, SR_GENOME_LEN);
}

static const char *mode_name_for(ms_mode_t mode) {
    switch (mode) {
        case MS_MODE_LAMARCKIAN: return "hybrid_lamarckian";
        case MS_MODE_LAMARCKIAN_PRESSURE: return "hybrid_lamarckian_pressure";
        case MS_MODE_NO_ADAPT: return "hybrid_no_adapt";
    }
    return "?";
}

static const char *coupling_name_for(ms_coupling_t coupling) {
    switch (coupling) {
        case MS_COUPLING_REPLACE_X3: return "replace_x3";
        case MS_COUPLING_GATE_X3: return "gate_x3";
        case MS_COUPLING_BIAS_X3: return "bias_x3";
    }
    return "?";
}

static void load_builtin_cfg(ehw54_run_cfg_t *cfg) {
    cfg->n_arms = EHW54_NARMS;
    cfg->seed = EHW54_SEED;
    cfg->population = EHW54_POP;
    cfg->generations = EHW54_GENS;
    cfg->adapt_epochs = EHW54_ADAPT;
    cfg->feature_min_balance = MS_DEFAULT_FEATURE_MIN_BALANCE;
    cfg->feature_penalty = MS_DEFAULT_FEATURE_PENALTY;
    cfg->staged = 0;
    cfg->valid = 1;
    for (int i = 0; i < EHW54_NARMS; i++) cfg->arms[i] = EHW54_ARMS[i];
}

static int decode_param_arm(uint32_t desc, int index, ehw54_arm_t *arm) {
    uint8_t mode = (uint8_t)(desc & 0xffu);
    uint8_t coupling = (uint8_t)((desc >> 8) & 0xffu);
    uint8_t flags = (uint8_t)((desc >> 16) & 0xffu);
    memset(arm, 0, sizeof(*arm));
    arm->id = (uint8_t)index;
    arm->has_expect = 0;
    arm->uses_structure = (flags & 0x01u) != 0;
    arm->uses_adapt = (flags & 0x02u) != 0;
    arm->uses_pressure = (flags & 0x04u) != 0;

    if (mode == 3u) {
        if (coupling != 3u || arm->uses_structure || !arm->uses_adapt || arm->uses_pressure) return 0;
        arm->weight_only = 1;
        arm->mode = "weight_only_lamarckian";
        arm->coupling = "none";
        arm->ms_mode = MS_MODE_LAMARCKIAN;
        arm->ms_coupling = MS_COUPLING_BIAS_X3;
        return 1;
    }

    if (mode > 2u || coupling > 2u) return 0;
    arm->weight_only = 0;
    arm->ms_mode = (ms_mode_t)mode;
    arm->ms_coupling = (ms_coupling_t)coupling;
    arm->mode = mode_name_for(arm->ms_mode);
    arm->coupling = coupling_name_for(arm->ms_coupling);
    if (!arm->uses_structure) return 0;
    if (arm->ms_mode == MS_MODE_NO_ADAPT && arm->uses_adapt) return 0;
    if (arm->ms_mode != MS_MODE_NO_ADAPT && !arm->uses_adapt) return 0;
    if ((arm->ms_mode == MS_MODE_LAMARCKIAN_PRESSURE) != (arm->uses_pressure != 0)) return 0;
    return 1;
}

static void load_runtime_cfg(ehw54_run_cfg_t *cfg) {
    load_builtin_cfg(cfg);
    if (fb_read(0) != EHW54_PARAM_MAGIC) return;

    cfg->staged = 1;
    cfg->valid = 0;
    int n_arms = (int)fb_read(1);
    int seed = (int)fb_read(2);
    int population = (int)fb_read(3);
    int generations = (int)fb_read(4);
    int adapt_epochs = (int)fb_read(5);
    int feature_min_balance = (int)fb_read(6);
    int feature_penalty = (int)fb_read(7);
    if (n_arms <= 0 || n_arms > EHW54_MAX_ARMS) return;
    if (population < EHW54_ELITES || population > EHW54_POP) return;
    if (generations < 0 || generations > 64) return;
    if (adapt_epochs < 0 || adapt_epochs > 8) return;
    if (feature_min_balance < 0 || feature_min_balance > EHW_NTEST) return;
    if (feature_penalty < 0 || feature_penalty > 1000000) return;

    cfg->n_arms = n_arms;
    cfg->seed = seed;
    cfg->population = population;
    cfg->generations = generations;
    cfg->adapt_epochs = adapt_epochs;
    cfg->feature_min_balance = feature_min_balance;
    cfg->feature_penalty = feature_penalty;
    for (int i = 0; i < n_arms; i++) {
        if (!decode_param_arm(fb_read(8u + (uint32_t)i), i, &cfg->arms[i])) return;
    }
    cfg->valid = 1;
}

static void sr_load(const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) sr_write(SR_GEN(i), genome[i]);
}

static uint8_t sr_phi_hw(const int8_t x[EHW_NIN]) {
    uint32_t row = (uint32_t)(x[0] >= 8) | ((uint32_t)(x[1] >= 8) << 1) | ((uint32_t)(x[2] >= 8) << 2);
    sr_write(SR_INPUT, row);
    return (uint8_t)(sr_read(SR_OUTPUT) & 1u);
}

static void transformed_x_hw(const int8_t x[EHW_NIN],
                             ms_coupling_t coupling,
                             uint8_t uses_structure,
                             int8_t out[EHW_NIN]) {
    out[0] = x[0];
    out[1] = x[1];
    out[2] = x[2];
    if (!uses_structure) {
        out[3] = x[3];
        return;
    }
    uint8_t phi = sr_phi_hw(x);
    if (coupling == MS_COUPLING_REPLACE_X3) out[3] = phi ? 16 : 0;
    else if (coupling == MS_COUPLING_GATE_X3) out[3] = phi ? x[3] : 0;
    else out[3] = (int8_t)ms_clamp_i8((int)x[3] + (phi ? 8 : -8));
}

static uint64_t feature_mask_hw(const uint8_t sr[SR_GENOME_LEN]) {
    uint64_t mask = 0;
    sr_load(sr);
    for (int i = 0; i < EHW_NTEST; i++) {
        if (sr_phi_hw(EHW_TEST_X[i])) mask |= 1ull << i;
    }
    return mask;
}

static void tu_master_load(const int32_t w1[EHW_NH][EHW_NIN], const int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++)
        for (int j = 0; j < EHW_NIN; j++) tu_write(TU_W1(i * EHW_NIN + j), w1[i][j]);
    for (int i = 0; i < EHW_NOUT; i++)
        for (int j = 0; j < EHW_NH; j++) tu_write(TU_W2(i * EHW_NH + j), w2[i][j]);
}

static void tu_master_read(int32_t w1[EHW_NH][EHW_NIN], int32_t w2[EHW_NOUT][EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++)
        for (int j = 0; j < EHW_NIN; j++) w1[i][j] = tu_read(TU_W1(i * EHW_NIN + j));
    for (int i = 0; i < EHW_NOUT; i++)
        for (int j = 0; j < EHW_NH; j++) w2[i][j] = tu_read(TU_W2(i * EHW_NH + j));
}

static void tu_loss_d2(const int32_t y[EHW_NOUT], const int32_t z2[EHW_NOUT],
                       uint8_t label, int32_t d2[EHW_NOUT]) {
    for (int i = 0; i < EHW_NOUT; i++) {
        tu_write(TU_INA(i), y[i]);
        tu_write(TU_Z(i), z2[i]);
        tu_write(TU_T(i), i == (int)label ? MEM_ONE : 0);
    }
    tu_write(TU_CMD, TU_CMD_LOSS_D2);
    for (int i = 0; i < EHW_NOUT; i++) d2[i] = tu_read(TU_D2(i));
}

static void tu_d1(const int32_t w2td2[EHW_NH], const int32_t z1[EHW_NH], int32_t d1[EHW_NH]) {
    for (int i = 0; i < EHW_NH; i++) {
        tu_write(TU_INA(i), w2td2[i]);
        tu_write(TU_Z(i), z1[i]);
    }
    tu_write(TU_CMD, TU_CMD_D1);
    for (int i = 0; i < EHW_NH; i++) d1[i] = tu_read(TU_D1(i));
}

static int32_t sgd_epoch_tu(int32_t w1[EHW_NH][EHW_NIN],
                            int32_t w2[EHW_NOUT][EHW_NH],
                            const int order[EHW_NTEST],
                            ms_coupling_t coupling,
                            uint8_t uses_structure) {
    int32_t sse = 0;
    tu_write(TU_CMD, TU_CMD_CLR);
    for (int oi = 0; oi < EHW_NTEST; oi++) {
        int idx = order[oi];
        int8_t x[EHW_NIN];
        uint8_t label = EHW_TEST_Y[idx];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        transformed_x_hw(EHW_TEST_X[idx], coupling, uses_structure, x);
        mem_forward_master(w1, w2, x, z1, h, z2, y);

        int32_t d2[EHW_NOUT];
        tu_loss_d2(y, z2, label, d2);
        sse = tu_read(TU_LOSS);

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
        tu_d1(w2td2, z1, d1);

        for (int i = 0; i < EHW_NOUT; i++)
            for (int j = 0; j < EHW_NH; j++) tu_write(TU_DW(i * EHW_NH + j), mem_qmul(d2[i], h[j]));
        tu_write(TU_CMD, TU_CMD_UPD_L2);
        tu_wait_idle();

        for (int i = 0; i < EHW_NH; i++) {
            for (int j = 0; j < EHW_NIN; j++) {
                int32_t x_q88 = (int32_t)x[j] << MEM_XSHIFT;
                tu_write(TU_DW(i * EHW_NIN + j), mem_qmul(d1[i], x_q88));
            }
        }
        tu_write(TU_CMD, TU_CMD_UPD_L1);
        tu_wait_idle();
        tu_master_read(w1, w2);
    }
    return sse;
}

static int32_t adapt_tu(const int8_t weight[EHW_GENOME_LEN],
                        const uint8_t sr[SR_GENOME_LEN],
                        ms_coupling_t coupling,
                        uint8_t uses_structure,
                        const ehw54_run_cfg_t *cfg,
                        int seed,
                        int8_t out_weight[EHW_GENOME_LEN]) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    int order[EHW_NTEST];
    int32_t last_sse = 0;
    mem_master_from_genome(weight, w1, w2);
    tu_master_load(w1, w2);
    if (uses_structure) sr_load(sr);
    for (int i = 0; i < EHW_NTEST; i++) order[i] = i;
    mem_rng_t rng = { (uint32_t)seed };
    for (int ep = 0; ep < cfg->adapt_epochs; ep++) {
        for (int i = EHW_NTEST - 1; i > 0; i--) {
            int j = mem_rng_range(&rng, i + 1);
            int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
        }
        last_sse = sgd_epoch_tu(w1, w2, order, coupling, uses_structure);
    }
    mem_genome_from_master(w1, w2, out_weight);
    return last_sse;
}

static ehw_score_t evaluate_hw(const int8_t weight[EHW_GENOME_LEN],
                               const uint8_t sr[SR_GENOME_LEN],
                               ms_coupling_t coupling,
                               uint8_t uses_structure) {
    int32_t w1[EHW_NH][EHW_NIN], w2[EHW_NOUT][EHW_NH];
    ehw_score_t score = {0, 0, 0};
    mem_master_from_genome(weight, w1, w2);
    if (uses_structure) sr_load(sr);
    for (int i = 0; i < EHW_NTEST; i++) {
        int8_t x[EHW_NIN];
        int32_t z1[EHW_NH], h[EHW_NH], z2[EHW_NOUT], y[EHW_NOUT];
        transformed_x_hw(EHW_TEST_X[i], coupling, uses_structure, x);
        uint8_t pred = mem_forward_master(w1, w2, x, z1, h, z2, y);
        score.correct += pred == EHW_TEST_Y[i];
        for (int k = 0; k < EHW_NOUT; k++) {
            int32_t target = (k == EHW_TEST_Y[i]) ? MEM_ONE : 0;
            int32_t err = y[k] - target;
            score.sse += mem_qmul(err, err);
        }
    }
    score.fitness = score.correct * 1000000 - score.sse;
    return score;
}

static void eval_candidate(const ehw54_arm_t *arm,
                           const ehw54_run_cfg_t *cfg,
                           const uint8_t sr[SR_GENOME_LEN],
                           const int8_t weight[EHW_GENOME_LEN],
                           int seed,
                           ms_eval_t *out) {
    out->pre_score = evaluate_hw(weight, sr, arm->ms_coupling, arm->uses_structure);
    mem_copy_genome(out->pre_weight, weight);
    if (arm->uses_adapt) {
        adapt_tu(weight, sr, arm->ms_coupling, arm->uses_structure, cfg, seed, out->post_weight);
        out->post_score = evaluate_hw(out->post_weight, sr, arm->ms_coupling, arm->uses_structure);
    } else {
        mem_copy_genome(out->post_weight, weight);
        out->post_score = out->pre_score;
    }
    copy_sr(out->sr, sr);
    out->feature_mask = arm->uses_structure ? feature_mask_hw(sr) : 0;
    out->feature_ones = arm->uses_structure ? ms_feature_ones(out->feature_mask) : 0;
    out->feature_penalty = arm->uses_pressure ? ms_structural_penalty(out->feature_mask, &(ms_options_t){
        .feature_min_balance = cfg->feature_min_balance,
        .feature_penalty = cfg->feature_penalty,
    }) : 0;
    out->select_score = out->post_score;
    out->select_score.fitness -= out->feature_penalty;
    if (arm->uses_adapt) mem_copy_genome(out->weight_for_next, out->post_weight);
    else mem_copy_genome(out->weight_for_next, weight);
}

static void sr_scaffold_genome(mem_rng_t *rng, int use_spare, uint8_t genome[SR_GENOME_LEN]) {
    genome[0] = 0x0au;
    genome[1] = 0x0au;
    genome[2] = 0x0au;
    genome[3] = use_spare ? 0x0au : 0x00u;
    genome[4] = (uint8_t)mem_rng_range(rng, 256);
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

static void sr_mutate_genome(const uint8_t in[SR_GENOME_LEN],
                             uint8_t out[SR_GENOME_LEN],
                             mem_rng_t *rng) {
    copy_sr(out, in);
    int changed = 0;
    for (int idx = 0; idx <= 4; idx++) {
        int bits = idx == 4 ? 8 : 4;
        uint8_t value = out[idx];
        for (int bit = 0; bit < bits; bit++) {
            if (mem_rng_chance(rng, EHW54_INIT_PPM)) {
                value ^= (uint8_t)(1u << bit);
                changed = 1;
            }
        }
        out[idx] = (uint8_t)(value & ((1u << bits) - 1u));
    }
    for (int idx = 5; idx < SR_GENOME_LEN; idx++) {
        int limit = idx < 13 ? 5 : 4;
        if (mem_rng_chance(rng, EHW54_SEL_PPM)) {
            out[idx] = (uint8_t)mem_rng_range(rng, limit);
            changed = 1;
        }
    }
    if (!changed) {
        int idx = mem_rng_range(rng, SR_GENOME_LEN);
        if (idx < 4) out[idx] ^= (uint8_t)(1u << mem_rng_range(rng, 4));
        else if (idx == 4) out[idx] ^= (uint8_t)(1u << mem_rng_range(rng, 8));
        else if (idx < 13) out[idx] = (uint8_t)mem_rng_range(rng, 5);
        else out[idx] = (uint8_t)mem_rng_range(rng, 4);
    }
}

static void sr_crossover_genome(const uint8_t a[SR_GENOME_LEN],
                                const uint8_t b[SR_GENOME_LEN],
                                uint8_t out[SR_GENOME_LEN],
                                mem_rng_t *rng) {
    for (int i = 0; i < SR_GENOME_LEN; i++) {
        out[i] = mem_rng_chance(rng, 500000) ? a[i] : b[i];
    }
}

static void copy_candidate(ms_candidate_t *dst, const ms_candidate_t *src) {
    copy_sr(dst->sr, src->sr);
    mem_copy_genome(dst->weight, src->weight);
}

static void mutate_hybrid(const ms_candidate_t *in, ms_candidate_t *out, mem_rng_t *rng) {
    copy_candidate(out, in);
    if (mem_rng_chance(rng, EHW54_STRUCT_PPM))
        sr_mutate_genome(in->sr, out->sr, rng);
    mem_mutate(in->weight, out->weight, rng, EHW54_MUT_PPM, EHW54_MUT_STEP);
}

static void crossover_hybrid(const ms_candidate_t *a,
                             const ms_candidate_t *b,
                             ms_candidate_t *out,
                             mem_rng_t *rng) {
    sr_crossover_genome(a->sr, b->sr, out->sr, rng);
    mem_crossover(a->weight, b->weight, out->weight, rng);
}

static void seed_hybrid_population(mem_rng_t *rng, const ehw54_run_cfg_t *cfg) {
    int8_t weight_seeds[8][EHW_GENOME_LEN];
    uint8_t sr_seeds[4][SR_GENOME_LEN];
    int weight_seed_count = cfg->population < 8 ? cfg->population : 8;
    mem_seed_population(rng, weight_seed_count, EHW54_INIT_SPAN, weight_seeds);
    copy_sr(sr_seeds[0], MS_SR_MAJORITY);
    copy_sr(sr_seeds[1], MS_SR_REPAIR);
    sr_scaffold_genome(rng, 0, sr_seeds[2]);
    sr_scaffold_genome(rng, 1, sr_seeds[3]);

    int n = 0;
    for (int s = 0; s < 4; s++) {
        for (int w = 0; w < 2 && w < weight_seed_count; w++) {
            if (n < cfg->population) {
                copy_sr(pop[n].sr, sr_seeds[s]);
                mem_copy_genome(pop[n].weight, weight_seeds[w]);
                n++;
            }
        }
    }
    while (n < cfg->population) {
        int s = mem_rng_range(rng, 4);
        sr_mutate_genome(sr_seeds[s], pop[n].sr, rng);
        int w = mem_rng_range(rng, weight_seed_count);
        int8_t tmp[EHW_GENOME_LEN];
        mem_mutate(weight_seeds[w], tmp, rng, EHW54_MUT_PPM, EHW54_MUT_STEP);
        mem_copy_genome(pop[n].weight, tmp);
        n++;
    }
}

static int better_score_idx(const ehw_score_t *as, int ai, const ehw_score_t *bs, int bi) {
    if (as->fitness != bs->fitness) return as->fitness > bs->fitness;
    return ai < bi;
}

static void sort_ranked(const ehw54_run_cfg_t *cfg) {
    for (int i = 0; i < cfg->population; i++) ranked_idx[i] = i;
    for (int i = 1; i < EHW54_POP; i++) {
        if (i >= cfg->population) break;
        int item = ranked_idx[i];
        int j = i - 1;
        while (j >= 0 && better_score_idx(&evaluated[item].select_score, item,
                                          &evaluated[ranked_idx[j]].select_score,
                                          ranked_idx[j])) {
            ranked_idx[j + 1] = ranked_idx[j];
            j--;
        }
        ranked_idx[j + 1] = item;
    }
}

static int tournament_pick(mem_rng_t *rng, const ehw54_run_cfg_t *cfg) {
    int best_pos = mem_rng_range(rng, cfg->population);
    int best_idx = ranked_idx[best_pos];
    for (int i = 1; i < EHW54_TOURN; i++) {
        int cur_pos = mem_rng_range(rng, cfg->population);
        int cur_idx = ranked_idx[cur_pos];
        if (better_score_idx(&evaluated[cur_idx].select_score, cur_idx,
                             &evaluated[best_idx].select_score, best_idx)) {
            best_idx = cur_idx;
        }
    }
    return best_idx;
}

static void eval_weight_candidate(const int8_t weight[EHW_GENOME_LEN],
                                  const ehw54_run_cfg_t *cfg,
                                  int seed,
                                  mem_eval_t *out) {
    out->pre_score = evaluate_hw(weight, MS_SR_MAJORITY, MS_COUPLING_BIAS_X3, 0);
    mem_copy_genome(out->pre_genome, weight);
    adapt_tu(weight, MS_SR_MAJORITY, MS_COUPLING_BIAS_X3, 0, cfg, seed, out->post_genome);
    out->post_score = evaluate_hw(out->post_genome, MS_SR_MAJORITY, MS_COUPLING_BIAS_X3, 0);
    out->select_score = out->post_score;
    mem_copy_genome(out->genome_for_next, out->post_genome);
}

static void sort_ranked_mem(const ehw54_run_cfg_t *cfg) {
    for (int i = 0; i < cfg->population; i++) ranked_idx[i] = i;
    for (int i = 1; i < cfg->population; i++) {
        int item = ranked_idx[i];
        int j = i - 1;
        while (j >= 0 && better_score_idx(&mem_evaluated[item].select_score, item,
                                          &mem_evaluated[ranked_idx[j]].select_score,
                                          ranked_idx[j])) {
            ranked_idx[j + 1] = ranked_idx[j];
            j--;
        }
        ranked_idx[j + 1] = item;
    }
}

static int tournament_pick_mem(mem_rng_t *rng, const ehw54_run_cfg_t *cfg) {
    int best_pos = mem_rng_range(rng, cfg->population);
    int best_idx = ranked_idx[best_pos];
    for (int i = 1; i < EHW54_TOURN; i++) {
        int cur_pos = mem_rng_range(rng, cfg->population);
        int cur_idx = ranked_idx[cur_pos];
        if (better_score_idx(&mem_evaluated[cur_idx].select_score, cur_idx,
                             &mem_evaluated[best_idx].select_score, best_idx)) {
            best_idx = cur_idx;
        }
    }
    return best_idx;
}

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
static void print_i8_genome(FILE *f, const int8_t genome[EHW_GENOME_LEN]) {
    for (int i = 0; i < EHW_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%d", genome[i]);
    }
}

static void print_sr_genome(FILE *f, const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%02x", genome[i] & 0xffu);
    }
}

static void write_curve_header(FILE *f) {
    fprintf(f, "mode,coupling,gen,best_correct,best_sse,best_fitness,select_fitness,feature_mask,feature_ones,feature_penalty,top_index,sr_genome,pre_weight,post_weight\n");
}

static void write_curve_row(FILE *f, const ehw54_arm_t *arm,
                            int gen, int top_index, const ms_eval_t *best) {
    fprintf(f, "%s,%s,%d,%d,%d,%d,%d,%010llx,%d,%d,%d,",
            arm->mode, arm->coupling, gen,
            best->post_score.correct, best->post_score.sse,
            best->post_score.fitness, best->select_score.fitness,
            (unsigned long long)best->feature_mask, best->feature_ones,
            best->feature_penalty, top_index);
    print_sr_genome(f, best->sr);
    fputc(',', f);
    print_i8_genome(f, best->pre_weight);
    fputc(',', f);
    print_i8_genome(f, best->post_weight);
    fputc('\n', f);
}

static void write_weight_curve_row(FILE *f, int gen, const mem_eval_t *best) {
    fprintf(f, "weight_only_lamarckian,none,%d,%d,%d,%d,%d,,,,,,",
            gen, best->post_score.correct, best->post_score.sse,
            best->post_score.fitness, best->select_score.fitness);
    print_i8_genome(f, best->pre_genome);
    fputc(',', f);
    print_i8_genome(f, best->post_genome);
    fputc('\n', f);
}
#endif

#ifndef MEMETIC_STRUCT_GA_HOST_STUB
static void publish_arm_heartbeat(const ehw54_arm_t *arm, int gen, int correct) {
    publish(0xF5100000u | ((uint32_t)(arm->id & 0xff) << 8) |
            (uint32_t)(gen & 0xff));
    publish(0xF5200000u | ((uint32_t)(arm->id & 0xff) << 8) |
            (uint32_t)(correct & 0xff));
}

static void publish_result_words(const ehw54_arm_t *arm, const ehw54_result_t *r) {
    uint32_t penalty_bucket = (uint32_t)(r->feature_penalty / MS_DEFAULT_FEATURE_PENALTY);
    publish(0xF5400000u | ((uint32_t)(arm->id & 0xff) << 8) | (uint32_t)(r->correct & 0xff));
    publish(0xF5500000u | ((uint32_t)(arm->id & 0xff) << 16) | ((uint32_t)r->sse & 0xffffu));
    publish(0xF5600000u | ((uint32_t)(arm->id & 0xff) << 8) |
            (uint32_t)((r->first_40 >= 0 ? r->first_40 : 0xff) & 0xff));
    publish(0xF5700000u | ((uint32_t)(arm->id & 0xff) << 16) |
            ((uint32_t)(r->feature_ones & 0xff) << 8) | (penalty_bucket & 0xffu));
}
#endif

static void run_hybrid_ga(const ehw54_arm_t *arm, const ehw54_run_cfg_t *cfg, FILE *curve,
                          ms_eval_t *best_out, int *first_40_out) {
    int seed_offset = arm->ms_mode == MS_MODE_LAMARCKIAN ? 303 :
        (arm->ms_mode == MS_MODE_NO_ADAPT ? 404 : 505);
    mem_rng_t rng = { (uint32_t)(cfg->seed + seed_offset) };
    seed_hybrid_population(&rng, cfg);
    eval_candidate(arm, cfg, pop[0].sr, pop[0].weight, cfg->seed, &evaluated[0]);
    ms_eval_t best = evaluated[0];
    int first_40 = -1;

    for (int gen = 0; gen <= cfg->generations; gen++) {
        for (int i = 0; i < cfg->population; i++) {
            eval_candidate(arm, cfg, pop[i].sr, pop[i].weight, cfg->seed + gen * 1009 + i, &evaluated[i]);
        }
        sort_ranked(cfg);
        int top = ranked_idx[0];
        if (evaluated[top].select_score.fitness > best.select_score.fitness) {
            best = evaluated[top];
        }
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;
#ifdef MEMETIC_STRUCT_GA_HOST_STUB
        write_curve_row(curve, arm, gen, top, &best);
#else
        publish_arm_heartbeat(arm, gen, best.post_score.correct);
#endif
        if (gen == cfg->generations) break;

        for (int i = 0; i < EHW54_ELITES; i++) {
            int src = ranked_idx[i];
            copy_sr(next_pop[i].sr, evaluated[src].sr);
            mem_copy_genome(next_pop[i].weight, evaluated[src].weight_for_next);
        }
        int nnext = EHW54_ELITES;
        while (nnext < cfg->population) {
            int p1 = tournament_pick(&rng, cfg);
            ms_candidate_t parent, child;
            copy_sr(parent.sr, evaluated[p1].sr);
            mem_copy_genome(parent.weight, evaluated[p1].weight_for_next);
            if (mem_rng_chance(&rng, EHW54_CROSS_PPM)) {
                int p2 = tournament_pick(&rng, cfg);
                ms_candidate_t mate;
                copy_sr(mate.sr, evaluated[p2].sr);
                mem_copy_genome(mate.weight, evaluated[p2].weight_for_next);
                crossover_hybrid(&parent, &mate, &child, &rng);
            } else {
                copy_candidate(&child, &parent);
            }
            mutate_hybrid(&child, &next_pop[nnext], &rng);
            nnext++;
        }
        memcpy(pop, next_pop, sizeof(pop));
    }
    *best_out = best;
    *first_40_out = first_40;
}

static void run_weight_ga(const ehw54_arm_t *arm, const ehw54_run_cfg_t *cfg,
                          FILE *curve, mem_eval_t *best_out, int *first_40_out) {
#ifdef MEMETIC_STRUCT_GA_HOST_STUB
    (void)arm;
#endif
    mem_rng_t rng = { (uint32_t)(cfg->seed + 202) };
    mem_seed_population(&rng, cfg->population, EHW54_INIT_SPAN, mem_pop);
    eval_weight_candidate(mem_pop[0], cfg, cfg->seed, &mem_evaluated[0]);
    mem_eval_t best = mem_evaluated[0];
    int first_40 = -1;

    for (int gen = 0; gen <= cfg->generations; gen++) {
        for (int i = 0; i < cfg->population; i++) {
            eval_weight_candidate(mem_pop[i], cfg, cfg->seed + gen * 1009 + i, &mem_evaluated[i]);
        }
        sort_ranked_mem(cfg);
        int top = ranked_idx[0];
        if (mem_evaluated[top].select_score.fitness > best.select_score.fitness) {
            best = mem_evaluated[top];
        }
        if (first_40 < 0 && best.post_score.correct >= EHW_NTEST) first_40 = gen;
#ifdef MEMETIC_STRUCT_GA_HOST_STUB
        write_weight_curve_row(curve, gen, &best);
#else
        publish_arm_heartbeat(arm, gen, best.post_score.correct);
#endif
        if (gen == cfg->generations) break;

        for (int i = 0; i < EHW54_ELITES; i++) {
            mem_copy_genome(mem_next_pop[i], mem_evaluated[ranked_idx[i]].genome_for_next);
        }
        int nnext = EHW54_ELITES;
        while (nnext < cfg->population) {
            int p1 = tournament_pick_mem(&rng, cfg);
            int8_t child[EHW_GENOME_LEN];
            if (mem_rng_chance(&rng, EHW54_CROSS_PPM)) {
                int p2 = tournament_pick_mem(&rng, cfg);
                mem_crossover(mem_evaluated[p1].genome_for_next,
                              mem_evaluated[p2].genome_for_next, child, &rng);
            } else {
                mem_copy_genome(child, mem_evaluated[p1].genome_for_next);
            }
            mem_mutate(child, mem_next_pop[nnext], &rng, EHW54_MUT_PPM, EHW54_MUT_STEP);
            nnext++;
        }
        memcpy(mem_pop, mem_next_pop, sizeof(mem_pop));
    }
    *best_out = best;
    *first_40_out = first_40;
}

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
static const char *arg_value(int argc, char **argv, const char *name, const char *fallback) {
    for (int i = 1; i + 1 < argc; i++) {
        if (!strcmp(argv[i], name)) return argv[i + 1];
    }
    return fallback;
}

static int load_param_bin(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror(path);
        return -1;
    }
    memset(fb_model, 0, sizeof(fb_model));
    uint8_t bytes[4];
    size_t n = 0;
    while (n < 2048u) {
        size_t got = fread(bytes, 1, 4, f);
        if (got == 0) break;
        if (got != 4) {
            fprintf(stderr, "FAIL: truncated param image %s\n", path);
            fclose(f);
            return -1;
        }
        fb_model[n++] = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
            ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    }
    if (fgetc(f) != EOF) {
        fprintf(stderr, "FAIL: param image %s exceeds 2048 words\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}
#endif

static int result_matches(const ehw54_arm_t *arm, const ehw54_result_t *r) {
    if (!arm->has_expect) return r->correct == EHW_NTEST;
    if (r->correct != arm->expect_correct) return 0;
    if (r->sse != arm->expect_sse) return 0;
    if (r->first_40 != arm->expect_first40) return 0;
    if (r->sat_count != arm->expect_sat) return 0;
    if (arm->uses_structure && r->feature_ones != arm->expect_ones) return 0;
    if (r->feature_penalty != arm->expect_penalty) return 0;
    return 1;
}

static void result_from_weight(const mem_eval_t *best, int first_40, ehw54_result_t *out) {
    out->correct = best->post_score.correct;
    out->sse = best->post_score.sse;
    out->first_40 = first_40;
    out->feature_ones = 0;
    out->feature_penalty = 0;
    out->sat_count = mem_sat_count(best->post_genome);
}

static void result_from_hybrid(const ms_eval_t *best, int first_40, ehw54_result_t *out) {
    out->correct = best->post_score.correct;
    out->sse = best->post_score.sse;
    out->first_40 = first_40;
    out->feature_ones = best->feature_ones;
    out->feature_penalty = best->feature_penalty;
    out->sat_count = mem_sat_count(best->post_weight);
}

static uint32_t run_all_arms(const ehw54_run_cfg_t *cfg, FILE *curve) {
    uint32_t pass = 1;
    if (!cfg->valid) pass = 0;
#ifdef MEMETIC_STRUCT_GA_HOST_STUB
    write_curve_header(curve);
#endif

    for (int ai = 0; ai < cfg->n_arms; ai++) {
        const ehw54_arm_t *arm = &cfg->arms[ai];
        if (!cfg->valid) {
            memset(&arm_results[ai], 0, sizeof(arm_results[ai]));
            continue;
        }
        if (arm->weight_only) {
            mem_eval_t best;
            int first_40;
            run_weight_ga(arm, cfg, curve, &best, &first_40);
            result_from_weight(&best, first_40, &arm_results[ai]);
        } else {
            ms_eval_t best;
            int first_40;
            run_hybrid_ga(arm, cfg, curve, &best, &first_40);
            result_from_hybrid(&best, first_40, &arm_results[ai]);
        }
        if (!result_matches(arm, &arm_results[ai])) pass = 0;
    }
    return pass;
}

#ifndef MEMETIC_STRUCT_GA_HOST_STUB
static void publish_all_results(const ehw54_run_cfg_t *cfg, uint32_t pass) {
    for (int ai = 0; ai < cfg->n_arms; ai++) {
        publish_result_words(&cfg->arms[ai], &arm_results[ai]);
    }
    publish(0xF54E0000u | ((uint32_t)(cfg->staged & 1u) << 8) | (uint32_t)(cfg->valid & 1u));
    publish(0xF54F0000u | (uint32_t)(cfg->n_arms & 0xff));
    publish(pass ? 0xF5F40000u : 0xF5F40001u);
}
#endif

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
int main(int argc, char **argv) {
#else
int main(void) {
#endif
    ehw54_run_cfg_t cfg;
#ifdef MEMETIC_STRUCT_GA_HOST_STUB
    const char *curve_path = arg_value(argc, argv, "--curve-csv", "runs/ehw5_4_struct_ab_curve.csv");
    const char *param_path = arg_value(argc, argv, "--param-bin", 0);
    if (load_param_bin(param_path) != 0) return 1;
    FILE *curve = fopen(curve_path, "w");
    if (!curve) {
        perror(curve_path);
        return 1;
    }
    load_runtime_cfg(&cfg);
    uint32_t pass = run_all_arms(&cfg, curve);
    fclose(curve);
#else
    publish(0xF5000004u);
    load_runtime_cfg(&cfg);
    publish(0xF5300000u | ((uint32_t)(cfg.staged & 1u) << 8) | (uint32_t)(cfg.valid & 1u));
    uint32_t pass = run_all_arms(&cfg, 0);
#endif

#ifdef MEMETIC_STRUCT_GA_HOST_STUB
    if (!pass) {
        for (int ai = 0; ai < cfg.n_arms; ai++) {
            const ehw54_arm_t *arm = &cfg.arms[ai];
            const ehw54_result_t *r = &arm_results[ai];
            fprintf(stderr, "FAIL: arm%d %s/%s got %d/40 sse=%d first_40=%d ones=%d penalty=%d sat=%d\n",
                    ai, arm->mode, arm->coupling, r->correct, r->sse, r->first_40,
                    r->feature_ones, r->feature_penalty, r->sat_count);
        }
        return 1;
    }
    printf("PASS: EHW-5.4 same-boot A/B host stub curves generated for %d arms (staged=%u)\n",
           cfg.n_arms, (unsigned)cfg.staged);
    return 0;
#else
    while (1) {
        publish_all_results(&cfg, pass);
    }
#endif
}
