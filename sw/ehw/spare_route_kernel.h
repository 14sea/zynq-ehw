#ifndef SPARE_ROUTE_KERNEL_H
#define SPARE_ROUTE_KERNEL_H

#include <stdint.h>

/*
 * EHW-3.0+ frozen 16-byte genome contract:
 *
 * byte  field
 * ----  ---------------------------------------------------------------
 * 0     logic_init[0]  A0 2-input LUT4 INIT, low nibble valid
 * 1     logic_init[1]  A1 2-input LUT4 INIT, low nibble valid
 * 2     logic_init[2]  A2 2-input LUT4 INIT, low nibble valid
 * 3     logic_init[3]  AS 2-input LUT4 INIT, low nibble valid
 * 4     init_out       O  3-input LUT8 INIT, full byte valid
 * 5     node_sel[0][0] A0 in0 source select into P=[x0,x1,x2,ZERO,ONE]
 * 6     node_sel[0][1] A0 in1 source select into P
 * 7     node_sel[1][0] A1 in0 source select into P
 * 8     node_sel[1][1] A1 in1 source select into P
 * 9     node_sel[2][0] A2 in0 source select into P
 * 10    node_sel[2][1] A2 in1 source select into P
 * 11    node_sel[3][0] AS in0 source select into P
 * 12    node_sel[3][1] AS in1 source select into P
 * 13    out_sel[0]     O in0 source select into [A0,A1,A2,AS]
 * 14    out_sel[1]     O in1 source select into [A0,A1,A2,AS]
 * 15    out_sel[2]     O in2 source select into [A0,A1,A2,AS]
 *
 * LUT decode:
 *   C1 node: out = (INIT >> (in1 << 1 | in0)) & 1
 *   O node : out = (INIT >> (in2 << 2 | in1 << 1 | in0)) & 1
 *
 * Validity layer:
 *   node_sel >= 5 decodes to ZERO. out_sel >= 4 decodes to A0. Muxes are pure
 *   fan-in selectors, so every byte string maps to a legal single-driver circuit.
 */

#define SR_GENOME_LEN 16
#define SR_TARGET_MASK 0xE8u
#define SR_FITNESS_MAX 8
#define SR_NODE_COUNT 4
#define SR_POOL_COUNT 5

enum {
    SR_NODE_A0 = 0,
    SR_NODE_A1 = 1,
    SR_NODE_A2 = 2,
    SR_NODE_AS = 3,
};

typedef enum {
    SR_FAULT_NONE = 0,
    SR_FAULT_STUCK0 = 1,
    SR_FAULT_STUCK1 = 2,
    SR_FAULT_DISABLE_NODE = 3,
    SR_FAULT_DISABLE_ROUTE = 4,
} sr_fault_kind_t;

typedef struct {
    sr_fault_kind_t kind;
    int node;
    int route_section; /* 0=node, 1=out */
    int route_idx;
    int route_mux;
} sr_fault_t;

static const sr_fault_t SR_FAULT_NONE_OBJ = {SR_FAULT_NONE, -1, -1, -1, -1};
static const sr_fault_t SR_FAULT_DISABLE_A1 = {SR_FAULT_DISABLE_NODE, SR_NODE_A1, -1, -1, -1};

static inline uint8_t sr_lut2(uint8_t init, uint8_t in0, uint8_t in1) {
    return (uint8_t)(((init & 0x0Fu) >> ((in1 << 1) | in0)) & 1u);
}

static inline uint8_t sr_lut3(uint8_t init, uint8_t in0, uint8_t in1, uint8_t in2) {
    return (uint8_t)((init >> ((in2 << 2) | (in1 << 1) | in0)) & 1u);
}

static inline uint8_t sr_decode_node_sel(uint8_t raw) {
    return raw < SR_POOL_COUNT ? raw : 3u; /* ZERO */
}

static inline uint8_t sr_decode_out_sel(uint8_t raw) {
    return raw < SR_NODE_COUNT ? raw : 0u; /* A0 */
}

static inline int sr_force_default_route(const sr_fault_t *fault, int section, int idx, int mux) {
    return fault->kind == SR_FAULT_DISABLE_ROUTE &&
           fault->route_section == section &&
           fault->route_idx == idx &&
           fault->route_mux == mux;
}

static inline uint8_t sr_c1_node_value(const uint8_t genome[SR_GENOME_LEN],
                                       int node,
                                       const uint8_t pool[SR_POOL_COUNT],
                                       const sr_fault_t *fault) {
    uint8_t sel0 = sr_force_default_route(fault, 0, node, 0) ? 3u : sr_decode_node_sel(genome[5 + 2 * node]);
    uint8_t sel1 = sr_force_default_route(fault, 0, node, 1) ? 3u : sr_decode_node_sel(genome[6 + 2 * node]);
    uint8_t value = sr_lut2(genome[node], pool[sel0], pool[sel1]);
    if (fault->node == node) {
        if (fault->kind == SR_FAULT_STUCK0 || fault->kind == SR_FAULT_DISABLE_NODE) return 0;
        if (fault->kind == SR_FAULT_STUCK1) return 1;
    }
    return value;
}

static inline uint8_t sr_eval_row(const uint8_t genome[SR_GENOME_LEN], int row, const sr_fault_t *fault) {
    uint8_t pool[SR_POOL_COUNT] = {
        (uint8_t)((row >> 0) & 1),
        (uint8_t)((row >> 1) & 1),
        (uint8_t)((row >> 2) & 1),
        0,
        1,
    };
    uint8_t nodes[SR_NODE_COUNT];
    for (int i = 0; i < SR_NODE_COUNT; i++) nodes[i] = sr_c1_node_value(genome, i, pool, fault);

    uint8_t ins[3];
    for (int mux = 0; mux < 3; mux++) {
        uint8_t sel = sr_force_default_route(fault, 1, 0, mux) ? 0u : sr_decode_out_sel(genome[13 + mux]);
        if (fault->kind == SR_FAULT_DISABLE_NODE && fault->node == (int)sel) ins[mux] = 0;
        else ins[mux] = nodes[sel];
    }
    return sr_lut3(genome[4], ins[0], ins[1], ins[2]);
}

static inline uint8_t sr_truth_mask(const uint8_t genome[SR_GENOME_LEN], const sr_fault_t *fault) {
    uint8_t mask = 0;
    for (int row = 0; row < 8; row++) mask |= (uint8_t)(sr_eval_row(genome, row, fault) << row);
    return mask;
}

static inline int sr_fitness(const uint8_t genome[SR_GENOME_LEN], const sr_fault_t *fault) {
    uint8_t diff = (uint8_t)(sr_truth_mask(genome, fault) ^ SR_TARGET_MASK);
    int bits = 0;
    for (int i = 0; i < 8; i++) bits += (diff >> i) & 1u;
    return SR_FITNESS_MAX - bits;
}

static inline int sr_out_uses_node(const uint8_t genome[SR_GENOME_LEN], int node) {
    for (int i = 13; i < 16; i++) {
        if ((int)sr_decode_out_sel(genome[i]) == node) return 1;
    }
    return 0;
}

static inline int sr_out_uses_spare(const uint8_t genome[SR_GENOME_LEN]) {
    return sr_out_uses_node(genome, SR_NODE_AS);
}

static inline int sr_reference_representability_check(void) {
    const uint8_t no_fault[SR_GENOME_LEN] = {
        0x0Au, 0x0Au, 0x0Au, 0x00u, 0xE8u, 0, 0, 1, 1, 2, 2, 3, 3, 0, 1, 2
    };
    const uint8_t repair[SR_GENOME_LEN] = {
        0x0Au, 0x00u, 0x0Au, 0x0Au, 0xE8u, 0, 0, 3, 3, 2, 2, 1, 1, 0, 3, 2
    };
    return sr_truth_mask(no_fault, &SR_FAULT_NONE_OBJ) == SR_TARGET_MASK &&
           sr_truth_mask(repair, &SR_FAULT_DISABLE_A1) == SR_TARGET_MASK;
}

#endif
