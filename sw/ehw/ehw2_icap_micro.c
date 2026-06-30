/* EHW-2 micro firmware: authentic per-eval ICAPE2 LUT-INIT evolution.
 *
 * The PS stages a small bank of candidate frame-write sequences into the framebuf
 * once, then grants ICAP ownership (PCAP_PR=0). NEORV32 evaluates each candidate by:
 *   1. streaming that candidate's frame sequence to rtl/xbus_icap.v,
 *   2. sweeping the editable LUT target's 8 input rows,
 *   3. scoring the observed truth table against 3-input majority (0xe8).
 *
 * Host stub mode models the same candidate bank without touching ICAP, so the
 * selection and mailbox/CSV contract can be gated before board work.
 */
#include <stdint.h>

#ifdef EHW2_HOST_STUB
#include <stdio.h>
#include <string.h>
#endif

#ifndef EHW2_HOST_STUB
#include <neorv32.h>
#endif

#define MBOX_BASE 0xF1000000u
#define ICAP_BASE 0xF3000000u
#define LUT_BASE  0xF4000000u
#define FB_BASE   0xF5000000u

#define ICAP_FILL   0x00u
#define ICAP_WRESET 0x04u
#define ICAP_WBURST 0x08u
#define ICAP_SWMODE 0x0cu
#define ICAP_STATUS 0x1cu

#define EHW2_MAGIC       0x45485732u /* "EHW2" */
#define EHW2_DESC_BASE   4u
#define EHW2_DESC_WORDS  4u
#define EHW2_MAX_CAND    4u
#define EHW2_TARGET_INIT 0xE8u
#define EHW2_SW_MODE     1u

typedef struct {
    uint8_t init;
    uint8_t observed;
    uint8_t fitness;
} eval_t;

#ifdef EHW2_HOST_STUB
static const uint8_t host_inits[EHW2_MAX_CAND] = { 0x00u, 0x80u, 0xA8u, 0xE8u };
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

static uint8_t majority3_mask(void) {
    uint8_t mask = 0;
    for (int row = 0; row < 8; row++) {
        int a = (row >> 0) & 1;
        int b = (row >> 1) & 1;
        int c = (row >> 2) & 1;
        if ((a + b + c) >= 2)
            mask |= (uint8_t)(1u << row);
    }
    return mask;
}

#ifdef EHW2_HOST_STUB
static uint8_t eval_candidate_init(uint8_t init) {
    return init;
}
#else
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

    mmio_write(ICAP_BASE + ICAP_SWMODE, EHW2_SW_MODE);
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
        mmio_write(LUT_BASE, row);
        for (volatile int d = 0; d < 8; d++) { }
        if (mmio_read(LUT_BASE) & 1u)
            observed |= (uint8_t)(1u << row);
    }
    return observed;
}
#endif

static uint8_t score_mask(uint8_t observed) {
    return (uint8_t)(8u - popcount8((uint8_t)(observed ^ EHW2_TARGET_INIT)));
}

static eval_t evaluate_candidate(uint32_t idx) {
    eval_t e;
#ifdef EHW2_HOST_STUB
    e.init = host_inits[idx];
    e.observed = eval_candidate_init(e.init);
#else
    uint32_t desc = EHW2_DESC_BASE + idx * EHW2_DESC_WORDS;
    uint32_t seq_off = fb_read(desc + 0u);
    uint32_t seq_len = fb_read(desc + 1u);
    e.init = (uint8_t)(fb_read(desc + 2u) & 0xffu);
    if (icap_write_seq(seq_off, seq_len) != 0) {
        publish(0xEF000000u | ((idx & 0xffu) << 8) | 0x01u);
        e.observed = 0;
        e.fitness = 0;
        return e;
    }
    e.observed = eval_candidate_hw();
#endif
    e.fitness = score_mask(e.observed);
    return e;
}

#ifndef EHW2_HOST_STUB
static uint32_t candidate_count(void) {
    for (;;) {
        if (fb_read(0) == EHW2_MAGIC)
            break;
        publish(0xE8000000u);
    }
    uint32_t n = fb_read(1);
    if (n == 0 || n > EHW2_MAX_CAND) {
        publish(0xEF000100u | (n & 0xffu));
        return 0;
    }
    return n;
}

static int run_micro(eval_t *best, uint32_t *best_idx) {
    uint32_t n = candidate_count();
    if (n == 0)
        return -1;

    publish(0xE8100000u | (n & 0xffu));
    best->init = 0;
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

#ifdef EHW2_HOST_STUB
static int write_csv(const char *path) {
    FILE *fp = path ? fopen(path, "w") : stdout;
    if (!fp)
        return 2;
    fprintf(fp, "eval,candidate_init,observed,fitness,best_index,best_fitness\n");

    eval_t best = {0, 0, 0};
    uint32_t best_idx = 0;
    for (uint32_t i = 0; i < EHW2_MAX_CAND; i++) {
        eval_t e = evaluate_candidate(i);
        if (i == 0 || e.fitness > best.fitness) {
            best = e;
            best_idx = i;
        }
        fprintf(fp, "%u,%02x,%02x,%u,%u,%u\n",
                i, e.init, e.observed, e.fitness, best_idx, best.fitness);
    }
    if (path)
        fclose(fp);
    return best.fitness == 8 ? 0 : 1;
}

int main(int argc, char **argv) {
    const char *csv = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--check-target")) {
            return majority3_mask() == EHW2_TARGET_INIT ? 0 : 1;
        } else if (!strcmp(argv[i], "--csv") && i + 1 < argc) {
            csv = argv[++i];
        }
    }
    return write_csv(csv);
}
#else
int main(void) {
    for (volatile uint32_t d = 0; d < 300000u; d++) { }
    publish(0xE8000001u);

    eval_t best;
    uint32_t best_idx;
    if (run_micro(&best, &best_idx) != 0)
        publish(0xEF000200u);

    for (;;) {
        publish(0xEB000000u | ((best_idx & 0xffu) << 16) |
                ((uint32_t)best.fitness << 8) | (uint32_t)best.observed);
    }
}
#endif
