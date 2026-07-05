/* EHW-5.5 POST firmware: reveal probe for the no-fault baked spare-route island.
 *
 * The island's LUT/select INITs are baked into fabric (rm_ehw55_baked, marker
 * "SR55", MS_SR_MAJORITY baseline, NO_FAULT=1). Firmware drives INPUT (0x08),
 * reads OUTPUT (0x0c), and publishes the 8-row truth mask AND the 40-sample
 * EHW feature mask. After ICAP edits ONLY g0/g1/g7/g8/g12/g14, the same loop
 * must flip from the baseline (truth 0xe8, feature 0xfbc5dabfc7, 28 ones) to
 * the EHW-5.4a arm1 structural champion (truth 0xa0, feature 0xd2c1d02a42,
 * 15 ones) without resetting PS/NEORV32 and with the marker unchanged.
 *
 * Mailbox words (steady republish loop, EHW-3.2 lesson). All payloads are
 * <=16 bits so they never overlap the 16-bit tag (a 24-bit payload under a
 * 0xE55x tag would OR into the tag byte and lose feature-mask bits):
 *   0xE5500000              boot
 *   0xE551mmmm              marker bits [31:16] ("SR" of 0x53523535)
 *   0xE552mmmm              marker bits [15:0]  ("55")
 *   0xE55300tt              8-row truth mask
 *   0xE554ffff              feature mask bits [15:0]
 *   0xE555ffff              feature mask bits [31:16]
 *   0xE556hhnn              hh = feature mask bits [39:32], nn = ones count
 */
#include "spare_route_kernel.h"
#include "ehw_kernel.h"

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

#define EHW55_MARKER 0x53523535u /* "SR55" */

#ifdef SR_HOST_STUB
static uint8_t stub_input;
static const uint8_t stub_genome[SR_GENOME_LEN] = {
#ifdef EHW55_CHAMP
    0x08u, 0x00u, 0x0au, 0x00u, 0xe8u, 0x00u, 0x00u, 0x03u,
    0x03u, 0x02u, 0x02u, 0x03u, 0x01u, 0x00u, 0x03u, 0x02u,
#else
    0x0au, 0x0au, 0x0au, 0x00u, 0xe8u, 0x00u, 0x00u, 0x01u,
    0x01u, 0x02u, 0x02u, 0x03u, 0x03u, 0x00u, 0x01u, 0x02u,
#endif
};

static uint32_t sr_read(uint32_t off) {
    if (off == SR_MARKER)
        return EHW55_MARKER;
    if (off == SR_OUTPUT)
        return sr_eval_row(stub_genome, stub_input, &SR_FAULT_NONE_OBJ);
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

static uint8_t truth_mask(void) {
    uint8_t mask = 0;
    for (int idx = 0; idx < 8; idx++)
        mask |= (uint8_t)(eval_idx(idx) << idx);
    return mask;
}

static uint64_t feature_mask(void) {
    uint64_t mask = 0;
    for (int i = 0; i < EHW_NTEST; i++) {
        const int8_t *x = EHW_TEST_X[i];
        int row = (x[0] >= 8) | ((x[1] >= 8) << 1) | ((x[2] >= 8) << 2);
        mask |= (uint64_t)eval_idx(row) << i;
    }
    return mask;
}

static int ones40(uint64_t mask) {
    int n = 0;
    for (int i = 0; i < EHW_NTEST; i++)
        n += (int)((mask >> i) & 1u);
    return n;
}

#ifdef SR_HOST_STUB
static uint32_t stub_words[8];
static int stub_nwords;
#endif

static void publish(uint32_t w) {
    MBOX = w;
#ifdef SR_HOST_STUB
    if (stub_nwords < 8) stub_words[stub_nwords++] = w;
#else
    for (volatile int d = 0; d < 4000; d++) { }
#endif
}

static void publish_evidence(uint32_t marker, uint8_t tm, uint64_t fm, int ones) {
    publish(0xE5510000u | (marker >> 16));
    publish(0xE5520000u | (marker & 0xFFFFu));
    publish(0xE5530000u | (uint32_t)tm);
    publish(0xE5540000u | (uint32_t)(fm & 0xFFFFu));
    publish(0xE5550000u | (uint32_t)((fm >> 16) & 0xFFFFu));
    publish(0xE5560000u | (uint32_t)(((fm >> 32) & 0xFFu) << 8) | (uint32_t)(ones & 0xFF));
}

int main(void) {
    publish(0xE5500000u);

    for (;;) {
        uint32_t marker = sr_read(SR_MARKER);
        uint8_t tm = truth_mask();
        uint64_t fm = feature_mask();
        int ones = ones40(fm);

#ifdef SR_HOST_STUB
        /* Assert the exact published word encodings, not just the computed
         * values — a lossy tag/payload overlap once slipped past a
         * values-only stub. */
        {
            static const uint32_t expect_base[8] = {
                0xE5500000u, 0xE5515352u, 0xE5523535u, 0xE55300E8u,
                0xE554BFC7u, 0xE555C5DAu, 0xE556FB1Cu, 0u,
            };
            static const uint32_t expect_champ[8] = {
                0xE5500000u, 0xE5515352u, 0xE5523535u, 0xE55300A0u,
                0xE5542A42u, 0xE555C1D0u, 0xE556D20Fu, 0u,
            };
#ifdef EHW55_CHAMP
            const uint32_t *expect = expect_champ;
            int ok = marker == EHW55_MARKER && tm == 0xa0u &&
                     fm == 0xd2c1d02a42ull && ones == 15;
#else
            const uint32_t *expect = expect_base;
            int ok = marker == EHW55_MARKER && tm == 0xe8u &&
                     fm == 0xfbc5dabfc7ull && ones == 28;
#endif
            publish_evidence(marker, tm, fm, ones);
            for (int i = 0; i < 7; i++) {
                if (stub_words[i] != expect[i]) {
                    printf("FAIL word[%d]=0x%08x expect 0x%08x\n",
                           i, (unsigned)stub_words[i], (unsigned)expect[i]);
                    ok = 0;
                }
            }
            printf("EHW55 marker=0x%08x truth=0x%02x feature=0x%010llx ones=%d words-ok=%d\n",
                   (unsigned)marker, tm, (unsigned long long)fm, ones, ok);
            return ok ? 0 : 1;
        }
#else
        publish_evidence(marker, tm, fm, ones);
#endif
    }
}
