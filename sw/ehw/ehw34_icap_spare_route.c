/* EHW-3.4 micro firmware: per-eval ICAPE2 spare-routing evolution.
 *
 * The PS stages a tiny bank of candidate frame-write sequences into the framebuf
 * once, then grants ICAP ownership (PCAP_PR=0). NEORV32 evaluates each candidate by:
 *   1. streaming all frame sequences for that candidate through rtl/xbus_icap.v,
 *   2. sweeping the live spare-routing island's 8 input rows,
 *   3. scoring the observed truth table against 3-input majority (0xe8).
 *
 * This build uses internal ICAPE2 only. It does not instantiate PS-HWICAP; do not
 * run PS-HWICAP readreg/writeseq tooling against this bitstream.
 */
#include "spare_route_kernel.h"

#include <stdint.h>

#ifdef EHW34_HOST_STUB
#include <stdio.h>
#include <string.h>
#endif

#ifndef EHW34_HOST_STUB
#include <neorv32.h>
#endif

#define MBOX_BASE 0xF1000000u
#define ICAP_BASE 0xF3000000u
#define SR_BASE   0xF4000000u
#define FB_BASE   0xF5000000u

#define ICAP_FILL   0x00u
#define ICAP_WRESET 0x04u
#define ICAP_WBURST 0x08u
#define ICAP_SWMODE 0x0cu
#define ICAP_STATUS 0x1cu

#define SR_INPUT  0x008u
#define SR_OUTPUT 0x00cu
#define SR_MARKER 0x020u

#define EHW34_MAGIC       0x45483334u /* "EH34" */
#define EHW34_DESC_BASE   4u
#define EHW34_MAX_CAND    4u
#define EHW34_MAX_SEQ     16u
#define EHW34_DESC_WORDS  (5u + EHW34_MAX_SEQ * 2u)
#define EHW34_SW_MODE     1u

typedef struct {
    uint8_t genome[SR_GENOME_LEN];
    uint8_t observed;
    uint8_t fitness;
} eval_t;

#ifdef EHW34_HOST_STUB
static const char *host_labels[EHW34_MAX_CAND] = {"base", "logic", "route", "repair"};
static const uint8_t host_genomes[EHW34_MAX_CAND][SR_GENOME_LEN] = {
    {0x0au, 0x08u, 0x01u, 0x0fu, 0x32u, 0x01u, 0x04u, 0x00u,
     0x02u, 0x02u, 0x00u, 0x04u, 0x01u, 0x01u, 0x02u, 0x00u},
    {0x0bu, 0x09u, 0x09u, 0x03u, 0xb1u, 0x01u, 0x04u, 0x00u,
     0x02u, 0x02u, 0x00u, 0x04u, 0x01u, 0x01u, 0x02u, 0x00u},
    {0x0au, 0x08u, 0x01u, 0x0fu, 0x32u, 0x00u, 0x04u, 0x04u,
     0x01u, 0x02u, 0x00u, 0x00u, 0x01u, 0x02u, 0x03u, 0x00u},
    {0x0bu, 0x09u, 0x09u, 0x03u, 0xb1u, 0x00u, 0x04u, 0x04u,
     0x01u, 0x02u, 0x00u, 0x00u, 0x01u, 0x02u, 0x03u, 0x00u},
};
#else
#define MBOX (*(volatile uint32_t *)MBOX_BASE)

static void publish(uint32_t w) {
    MBOX = w;
    for (volatile int d = 0; d < 4000; d++) { }
}
#endif

static uint8_t popcount8(uint8_t x) {
    uint8_t n = 0;
    for (int i = 0; i < 8; i++)
        n += (uint8_t)((x >> i) & 1u);
    return n;
}

static uint8_t score_mask(uint8_t observed) {
    return (uint8_t)(8u - popcount8((uint8_t)(observed ^ SR_TARGET_MASK)));
}

#ifdef EHW34_HOST_STUB
static uint8_t eval_candidate_host(const uint8_t genome[SR_GENOME_LEN]) {
    return sr_truth_mask(genome, &SR_FAULT_DISABLE_A1);
}
#else
static void unpack_genome_word(uint32_t word, uint8_t *dst) {
    dst[0] = (uint8_t)(word >> 24);
    dst[1] = (uint8_t)(word >> 16);
    dst[2] = (uint8_t)(word >> 8);
    dst[3] = (uint8_t)word;
}

static uint32_t fb_read(uint32_t word_index) {
    return *(volatile uint32_t *)(FB_BASE + word_index * 4u);
}

static uint32_t mmio_read(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

static void mmio_write(uint32_t addr, uint32_t value) {
    *(volatile uint32_t *)addr = value;
}

static int icap_idle(void) {
    return (mmio_read(ICAP_BASE + ICAP_STATUS) & 0x6u) == 0;
}

static int icap_write_seq(uint32_t word_off, uint32_t word_len) {
    if (word_len == 0 || word_len > 255)
        return -1;

    mmio_write(ICAP_BASE + ICAP_SWMODE, EHW34_SW_MODE);
    mmio_write(ICAP_BASE + ICAP_WRESET, 0);
    for (uint32_t i = 0; i < word_len; i++)
        mmio_write(ICAP_BASE + ICAP_FILL, fb_read(word_off + i));
    mmio_write(ICAP_BASE + ICAP_WBURST, word_len);

    for (uint32_t spin = 0; spin < 1000000u; spin++) {
        if (icap_idle())
            return 0;
    }
    return -2;
}

static uint8_t eval_candidate_hw(void) {
    uint8_t observed = 0;
    for (uint32_t row = 0; row < 8; row++) {
        mmio_write(SR_BASE + SR_INPUT, row);
        for (volatile int d = 0; d < 8; d++) { }
        if (mmio_read(SR_BASE + SR_OUTPUT) & 1u)
            observed |= (uint8_t)(1u << row);
    }
    return observed;
}
#endif

static eval_t evaluate_candidate(uint32_t idx) {
    eval_t e;
#ifdef EHW34_HOST_STUB
    for (int i = 0; i < SR_GENOME_LEN; i++)
        e.genome[i] = host_genomes[idx][i];
    e.observed = eval_candidate_host(e.genome);
#else
    uint32_t desc = EHW34_DESC_BASE + idx * EHW34_DESC_WORDS;
    for (uint32_t w = 0; w < 4; w++)
        unpack_genome_word(fb_read(desc + w), &e.genome[w * 4u]);

    uint32_t nseq = fb_read(desc + 4u);
    if (nseq > EHW34_MAX_SEQ) {
        publish(0xEF340000u | ((idx & 0xffu) << 8) | 0x02u);
        e.observed = 0;
        e.fitness = 0;
        return e;
    }
    for (uint32_t s = 0; s < nseq; s++) {
        uint32_t seq_off = fb_read(desc + 5u + s * 2u);
        uint32_t seq_len = fb_read(desc + 6u + s * 2u);
        if (icap_write_seq(seq_off, seq_len) != 0) {
            publish(0xEF340000u | ((idx & 0xffu) << 8) | (0x10u + s));
            e.observed = 0;
            e.fitness = 0;
            return e;
        }
    }
    e.observed = eval_candidate_hw();
#endif
    e.fitness = score_mask(e.observed);
    return e;
}

#ifndef EHW34_HOST_STUB
static uint32_t candidate_count(void) {
    for (;;) {
        if (fb_read(0) == EHW34_MAGIC)
            break;
        publish(0xE8400000u);
    }
    uint32_t n = fb_read(1);
    if (n == 0 || n > EHW34_MAX_CAND) {
        publish(0xEF340100u | (n & 0xffu));
        return 0;
    }
    uint32_t desc_base = fb_read(2);
    uint32_t desc_words = fb_read(3);
    if (desc_base != EHW34_DESC_BASE || desc_words != EHW34_DESC_WORDS) {
        publish(0xEF340200u | ((desc_base & 0xffu) << 8) | (desc_words & 0xffu));
        return 0;
    }
    return n;
}

static int run_micro(eval_t *best, uint32_t *best_idx) {
    uint32_t n = candidate_count();
    if (n == 0)
        return -1;

    publish(0xE8410000u | (n & 0xffu));
    publish(0xE8420000u | (mmio_read(SR_BASE + SR_MARKER) & 0x00ffffffu));
    best->observed = 0;
    best->fitness = 0;
    *best_idx = 0;

    for (uint32_t i = 0; i < n; i++) {
        eval_t e = evaluate_candidate(i);
        publish(0xE9000000u | ((i & 0xffu) << 16) |
                ((uint32_t)e.fitness << 8) | (uint32_t)e.observed);
        if (i == 0 || e.fitness > best->fitness) {
            *best = e;
            *best_idx = i;
        }
    }
    publish(0xEA000000u | ((*best_idx & 0xffu) << 16) |
            ((uint32_t)best->fitness << 8) | (uint32_t)best->observed);
    return 0;
}
#endif

#ifdef EHW34_HOST_STUB
static void print_genome(FILE *fp, const uint8_t genome[SR_GENOME_LEN]) {
    for (int i = 0; i < SR_GENOME_LEN; i++)
        fprintf(fp, "%02x", genome[i]);
}

static int write_csv(const char *path) {
    FILE *fp = path ? fopen(path, "w") : stdout;
    if (!fp)
        return 2;
    fprintf(fp, "eval,label,genome,observed,fitness,best_index,best_fitness\n");

    eval_t best = {{0}, 0, 0};
    uint32_t best_idx = 0;
    for (uint32_t i = 0; i < EHW34_MAX_CAND; i++) {
        eval_t e = evaluate_candidate(i);
        if (i == 0 || e.fitness > best.fitness) {
            best = e;
            best_idx = i;
        }
        fprintf(fp, "%u,%s,", i, host_labels[i]);
        print_genome(fp, e.genome);
        fprintf(fp, ",%02x,%u,%u,%u\n", e.observed, e.fitness, best_idx, best.fitness);
    }
    if (path)
        fclose(fp);
    return best.fitness == 8 ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *csv = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check-target")) {
            eval_t e = evaluate_candidate(3);
            return e.observed == SR_TARGET_MASK && e.fitness == SR_FITNESS_MAX ? 0 : 1;
        } else if (!strcmp(argv[i], "--csv") && i + 1 < argc) {
            csv = argv[++i];
        }
    }
    return write_csv(csv);
}
#else
int main(void) {
    for (volatile uint32_t d = 0; d < 300000u; d++) { }
    publish(0xE8400001u);

    eval_t best = {{0}, 0, 0};
    uint32_t best_idx = 0;
    if (run_micro(&best, &best_idx) != 0)
        publish(0xEF340300u);

    for (;;) {
        publish(0xEC000000u | ((best_idx & 0xffu) << 16) |
                ((uint32_t)best.fitness << 8) | (uint32_t)best.observed);
    }
}
#endif
