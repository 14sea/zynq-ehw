/* EHW-1.2 POST firmware: evaluate a baked CGP multiplier RM.
 *
 * This does not write any LUT INIT registers. It drives INPUT (0x08), reads OUTPUT
 * (0x0c), computes truth-table rows/fitness in firmware, and publishes mailbox
 * words. After ICAP edits the baked LUT4 INITs, the same loop should flip from the
 * baseline result to the champion result without resetting PS/NEORV32.
 */
#include "cgp_kernel.h"
#include <stdint.h>

#ifdef CGP_HOST_STUB
#include <stdio.h>
static uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#else
#include <neorv32.h>
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define CGP_BASE   0xF0000000U
#define CGP_INPUT  0x008U
#define CGP_OUTPUT 0x00CU
#define CGP_MARKER 0x020U

#ifdef CGP_HOST_STUB
static uint8_t stub_input;
static const uint16_t stub_base[CGP_GENOME_LEN] = {
    0xAAAAu, 0xCCCCu, 0xF0F0u, 0xFF00u,
    0xAAAAu, 0xCCCCu, 0xF0F0u, 0xFF00u,
#ifdef CGP_BAKED_CHAMPION
    0xA0A0u, 0x6AC0u, 0x4C00u, 0x8000u,
#else
    0x0000u, 0x0000u, 0x0000u, 0x0000u,
#endif
};

static uint32_t cgp_read(uint32_t off) {
    if (off == CGP_MARKER) {
#ifdef CGP_BAKED_CHAMPION
        return 0x43475031u;
#else
        return 0x43475030u;
#endif
    }
    if (off == CGP_OUTPUT) {
        uint8_t out[4];
        cgp_eval_grid(stub_base, stub_input, out);
        return (uint32_t)(out[0] | (out[1] << 1) | (out[2] << 2) | (out[3] << 3));
    }
    return 0;
}

static void cgp_write(uint32_t off, uint32_t value) {
    if (off == CGP_INPUT)
        stub_input = (uint8_t)(value & 0x0FU);
}
#else
static uint32_t cgp_read(uint32_t off) {
    return *(volatile uint32_t *)(CGP_BASE + off);
}

static void cgp_write(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(CGP_BASE + off) = value;
}
#endif

static uint8_t eval_idx(int idx) {
    cgp_write(CGP_INPUT, (uint32_t)(idx & 0x0F));
    return (uint8_t)(cgp_read(CGP_OUTPUT) & 0x0F);
}

static void eval_all(int *rows, int *fitness) {
    int r = 0;
    int f = 0;
    for (int idx = 0; idx < 16; idx++) {
        uint8_t out = eval_idx(idx);
        int ok = 1;
        for (int bit = 0; bit < 4; bit++) {
            int good = (((out >> bit) & 1U) == cgp_gold_bit(bit, idx));
            f += good;
            ok &= good;
        }
        r += ok;
    }
    *rows = r;
    *fitness = f;
}

static void publish(uint32_t w) {
    MBOX = w;
#ifndef CGP_HOST_STUB
    for (volatile int d = 0; d < 4000; d++) { }
#endif
}

int main(void) {
#ifndef CGP_HOST_STUB
    for (volatile uint32_t d = 0; d < 300000u; d++) { }
#endif
    publish(0xE2000000u);

    for (;;) {
        int rows = 0;
        int fitness = 0;
        uint32_t marker = cgp_read(CGP_MARKER);
        eval_all(&rows, &fitness);

#ifdef CGP_HOST_STUB
        printf("CGP_BAKED marker=0x%08x rows=%d/16 fitness=%d/64\n", marker, rows, fitness);
        return (marker == 0x43475031u && rows == 16 && fitness == 64) ||
               (marker == 0x43475030u && rows == 7 && fitness == 50) ? 0 : 1;
#else
        publish(0xE3000000u | (uint32_t)(rows & 0xFF));
        publish(0xE4000000u | (uint32_t)(fitness & 0xFF));
        publish(0xE5000000u | (marker & 0x00FFFFFFu));
#endif
    }
}
