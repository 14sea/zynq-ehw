#ifndef EHW_KERNEL_H
#define EHW_KERNEL_H

#include <stdint.h>

#define EHW_K 2
#define EHW_XSHIFT_H 3
#define EHW_NIN 4
#define EHW_NH 4
#define EHW_NOUT 2
#define EHW_NTEST 40
#define EHW_GENOME_LEN (EHW_NH * EHW_NIN + EHW_NOUT * EHW_NH)

typedef struct {
    int32_t fitness;
    int32_t correct;
    int32_t sse;
} ehw_score_t;

static const int16_t EHW_B1[EHW_NH] = {10, 8, -12, -11};
static const int16_t EHW_B2[EHW_NOUT] = {111, 136};

static const int8_t EHW_TEST_X[EHW_NTEST][EHW_NIN] = {
    {5, 12, 12, 6},    {11, 18, 18, 16}, {2, 13, 14, 2},   {1, 7, 6, 2},
    {1, 6, 3, 4},      {6, 3, 0, 7},     {10, 15, 15, 11}, {5, 8, 9, 7},
    {1, 11, 9, 3},     {18, 15, 15, 16}, {1, 11, 11, 1},   {11, 10, 11, 13},
    {7, 12, 12, 9},    {10, 9, 8, 9},    {4, 3, 3, 6},     {1, 12, 12, 2},
    {7, 6, 7, 7},      {7, 12, 11, 10},  {3, 8, 6, 5},     {0, 11, 8, 4},
    {9, 12, 12, 10},   {5, 4, 4, 6},     {12, 11, 12, 13}, {10, 6, 9, 9},
    {9, 9, 10, 9},     {2, 6, 2, 7},     {0, 9, 9, 2},     {2, 5, 1, 7},
    {3, 3, 3, 4},      {3, 6, 1, 9},     {10, 13, 12, 14},{8, 14, 12, 12},
    {1, 8, 8, 2},      {9, 10, 10, 9},   {0, 6, 5, 3},     {1, 12, 10, 3},
    {8, 10, 9, 8},     {1, 10, 10, 2},   {13, 10, 10, 13},{8, 16, 16, 15},
};

static const uint8_t EHW_TEST_Y[EHW_NTEST] = {
    0, 0, 1, 1, 1, 1, 0, 0, 1, 0,
    1, 0, 0, 0, 1, 1, 0, 0, 1, 1,
    0, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    0, 0, 1, 0, 1, 1, 0, 1, 0, 0,
};

static const uint8_t EHW_M753_GOLD_CLS[EHW_NTEST] = {
    0, 0, 0, 1, 1, 1, 0, 1, 1, 0,
    1, 0, 0, 0, 1, 1, 1, 0, 1, 1,
    0, 1, 0, 0, 0, 1, 1, 1, 1, 1,
    0, 0, 1, 0, 1, 1, 0, 1, 0, 0,
};

static const int8_t EHW_TRAINED_GENOME[EHW_GENOME_LEN] = {
    3, -3, -3, -2,
    13, 19, 21, 18,
    -3, -3, -1, 0,
    1, 0, -2, -3,
    0, 27, 3, 0,
    14, -14, 5, 13,
};

static inline int8_t ehw_clamp_i8(int32_t v) {
    return (v < -128) ? -128 : (v > 127) ? 127 : (int8_t)v;
}

static inline int16_t ehw_sat16(int32_t v) {
    return (v < -32768) ? -32768 : (v > 32767) ? 32767 : (int16_t)v;
}

static inline int32_t ehw_leaky(int32_t v) {
    return (v >= 0) ? v : (v >> EHW_K);
}

static inline int8_t ehw_q8(int32_t v, int shift) {
    return ehw_clamp_i8((v + (1 << (shift - 1))) >> shift);
}

static inline int32_t ehw_dot4(const int8_t *row, const int8_t *x) {
    return (int32_t)row[0] * x[0] + (int32_t)row[1] * x[1] +
           (int32_t)row[2] * x[2] + (int32_t)row[3] * x[3];
}

static inline uint8_t ehw_forward(const int8_t genome[EHW_GENOME_LEN],
                                  const int8_t x[EHW_NIN],
                                  int32_t y[EHW_NOUT]) {
    int8_t hidden[EHW_NH];
    for (int r = 0; r < EHW_NH; r++) {
        const int8_t *row = &genome[r * EHW_NIN];
        int32_t raw = ehw_dot4(row, x);
        int32_t hp = (raw + (1 << 3)) >> 4;
        hidden[r] = ehw_q8(ehw_leaky(ehw_sat16(hp + EHW_B1[r])), EHW_XSHIFT_H);
    }

    const int8_t *w2 = &genome[EHW_NH * EHW_NIN];
    for (int r = 0; r < EHW_NOUT; r++) {
        int32_t raw = ehw_dot4(&w2[r * EHW_NH], hidden);
        int32_t yp = (raw + (1 << 2)) >> 3;
        y[r] = ehw_leaky(ehw_sat16(yp + EHW_B2[r]));
    }
    return (y[1] > y[0]) ? 1 : 0;
}

static inline ehw_score_t ehw_evaluate(const int8_t genome[EHW_GENOME_LEN]) {
    ehw_score_t score = {0, 0, 0};
    for (int i = 0; i < EHW_NTEST; i++) {
        int32_t y[EHW_NOUT];
        uint8_t pred = ehw_forward(genome, EHW_TEST_X[i], y);
        score.correct += (pred == EHW_TEST_Y[i]);
        for (int k = 0; k < EHW_NOUT; k++) {
            int32_t target = (k == EHW_TEST_Y[i]) ? 256 : 0;
            int32_t err = y[k] - target;
            score.sse += (err * err + 128) >> 8;
        }
    }
    score.fitness = score.correct * 1000000 - score.sse;
    return score;
}

static inline int ehw_golden_mismatches(const int8_t genome[EHW_GENOME_LEN]) {
    int mismatches = 0;
    for (int i = 0; i < EHW_NTEST; i++) {
        int32_t y[EHW_NOUT];
        uint8_t pred = ehw_forward(genome, EHW_TEST_X[i], y);
        mismatches += (pred != EHW_M753_GOLD_CLS[i]);
    }
    return mismatches;
}

#endif
