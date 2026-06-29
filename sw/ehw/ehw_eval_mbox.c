// EHW-0.2 bring-up firmware: evaluate one compiled genome on the VRC array and
// publish the score over the PS-visible mailbox.
//
// This is intentionally mailbox-only. The copied M7 references note that NEORV32
// uart0 is not pinned out in the current dfx_top, so true host-sent genomes need
// an explicit PS->PL command path. This firmware validates the core path first:
// C fixed-point evaluator -> register-loaded 4x4 VRC -> mailbox.
//
/*
 * Board build, once copied into the NEORV32 firmware tree:
 *   make APP_SRC=ehw_eval_mbox.c NEORV32_HOME=../../rtl_src/neorv32_tpu/neorv32 \
 *        RISCV_PREFIX=riscv64-unknown-elf- USER_FLAGS+="-specs=picolibc.specs" clean install
 *
 * Host syntax/smoke build:
 *   cc -std=c99 -Wall -Wextra -DEHW_HOST_STUB -I sw/ehw sw/ehw/ehw_eval_mbox.c
 */

#include "ehw_champion.h"

#include <stdint.h>

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

#define MB_BOOT          0xE0000000u
#define MB_ARRAY_OK      0xE1000000u
#define MB_SCORE(c, s)   (0xE2000000u | (((uint32_t)(c) & 0xFFu) << 16) | ((uint32_t)(s) & 0xFFFFu))
#define MB_FITNESS(f)    (0xE3000000u | ((uint32_t)(f) & 0x00FFFFFFu))

static void hold(void) {
#ifndef EHW_HOST_STUB
    for (volatile uint32_t d = 0; d < 500000u; d++) { }
#endif
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

int main(void) {
#ifndef EHW_HOST_STUB
    neorv32_rte_setup();
#endif
    MBOX = MB_BOOT;
    hold();

    {
        const int8_t w[4][4] = {{1,1,1,1},{1,2,3,4},{2,2,2,2},{1,0,1,0}};
        const int8_t x[4] = {2,3,4,5};
        int32_t acc[4];
        array_macc(w, x, acc);
        MBOX = MB_ARRAY_OK | ((uint32_t)acc[0] & 0xFFFFu); // expect low16=14
        hold();
    }

    ehw_score_t score = evaluate_vrc(EHW_CHAMPION_GENOME);
    MBOX = MB_SCORE(score.correct, score.sse);
    hold();
    MBOX = MB_FITNESS(score.fitness);

#ifdef EHW_HOST_STUB
    printf("score correct=%d sse=%d fitness=%d mbox=0x%08x\n",
           score.correct, score.sse, score.fitness, (uint32_t)MBOX);
    return (score.correct == 40 && score.sse == 4799 && score.fitness == 39995201) ? 0 : 1;
#else
    uint32_t led = 0xF;
    while (1) {
        MBOX = MB_SCORE(score.correct, score.sse);
        hold();
        MBOX = MB_FITNESS(score.fitness);
        neorv32_gpio_port_set(led);
        led ^= 0xF;
        hold();
    }
#endif
}
