/* EHW-3.3 POST firmware: evaluate a baked spare-routing island.
 *
 * This does not write genome/config registers. The island's LUT/select INITs are
 * baked into fabric. Firmware drives INPUT (0x08), reads OUTPUT (0x0c), computes
 * mask/fitness, and publishes mailbox words. After ICAP edits the baked LUT INITs,
 * the same loop should flip from broken baseline (c8, 7/8) to repaired (e8, 8/8)
 * without resetting PS/NEORV32.
 */
#include "spare_route_kernel.h"

#include <stdint.h>

#ifdef SR_HOST_STUB
#include <stdio.h>
static uint32_t MBOX_STUB;
#define MBOX MBOX_STUB
#else
#include <neorv32.h>
#define MBOX (*(volatile uint32_t *)0xF1000000U)
#endif

#define SR_BASE   0xF0000000U
#define SR_INPUT  0x008U
#define SR_OUTPUT 0x00CU
#define SR_MARKER 0x020U

#ifdef SR_HOST_STUB
static uint8_t stub_input;
static const uint8_t stub_genome[SR_GENOME_LEN] = {
#ifdef SR_BAKED_REPAIR
    0x0bu, 0x09u, 0x09u, 0x03u, 0xb1u, 0x00u, 0x04u, 0x04u,
    0x01u, 0x02u, 0x00u, 0x00u, 0x01u, 0x02u, 0x03u, 0x00u,
#else
    0x0au, 0x08u, 0x01u, 0x0fu, 0x32u, 0x01u, 0x04u, 0x00u,
    0x02u, 0x02u, 0x00u, 0x04u, 0x01u, 0x01u, 0x02u, 0x00u,
#endif
};
static const sr_fault_t stub_fault = {SR_FAULT_DISABLE_NODE, SR_NODE_A1, -1, -1, -1};

static uint32_t sr_read(uint32_t off) {
    if (off == SR_MARKER) {
#ifdef SR_BAKED_REPAIR
        return 0x53524231u;
#else
        return 0x53524230u;
#endif
    }
    if (off == SR_OUTPUT)
        return sr_eval_row(stub_genome, stub_input, &stub_fault);
    return 0;
}

static void sr_write(uint32_t off, uint32_t value) {
    if (off == SR_INPUT)
        stub_input = (uint8_t)(value & 7u);
}
#else
static uint32_t sr_read(uint32_t off) {
    return *(volatile uint32_t *)(SR_BASE + off);
}

static void sr_write(uint32_t off, uint32_t value) {
    *(volatile uint32_t *)(SR_BASE + off) = value;
}
#endif

static uint8_t eval_idx(int idx) {
    sr_write(SR_INPUT, (uint32_t)(idx & 7));
    return (uint8_t)(sr_read(SR_OUTPUT) & 1u);
}

static uint8_t eval_mask(void) {
    uint8_t mask = 0;
    for (int idx = 0; idx < 8; idx++)
        mask |= (uint8_t)(eval_idx(idx) << idx);
    return mask;
}

static int fitness_of_mask(uint8_t mask) {
    uint8_t diff = (uint8_t)(mask ^ SR_TARGET_MASK);
    int correct = 0;
    for (int bit = 0; bit < 8; bit++)
        correct += !((diff >> bit) & 1u);
    return correct;
}

static void publish(uint32_t w) {
    MBOX = w;
#ifndef SR_HOST_STUB
    for (volatile int d = 0; d < 4000; d++) { }
#endif
}

int main(void) {
#ifndef SR_HOST_STUB
    for (volatile uint32_t d = 0; d < 300000u; d++) { }
#endif
    publish(0xE3300000u);

    for (;;) {
        uint32_t marker = sr_read(SR_MARKER);
        uint8_t mask = eval_mask();
        int fitness = fitness_of_mask(mask);

#ifdef SR_HOST_STUB
        printf("SR_BAKED marker=0x%08x mask=0x%02x fitness=%d/8\n", marker, mask, fitness);
#ifdef SR_BAKED_REPAIR
        return marker == 0x53524231u && mask == 0xe8u && fitness == 8 ? 0 : 1;
#else
        return marker == 0x53524230u && mask == 0xc8u && fitness == 7 ? 0 : 1;
#endif
#else
        publish(0xE3310000u | (marker & 0x00FFFFFFu));
        publish(0xE3320000u | (uint32_t)mask);
        publish(0xE3330000u | (uint32_t)(fitness & 0xFF));
#endif
    }
}

