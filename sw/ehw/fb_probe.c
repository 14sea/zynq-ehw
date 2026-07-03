/* fb_probe.c — EHW-4.6b plumbing probe (Claude board-side tooling).
 *
 * Proves the PS->NEORV32 parameter channel live, no reboot per update:
 *   - NEORV32 loops reading param words 0/1 from the framebuf window at
 *     0xF5000000 (PS side: axil_framebuf @0x40000000, staged with U-Boot mw
 *     or scripts/ehw2-framebank-load.py).
 *   - mailbox alternates 0xFBxxxxxx = fb[0] low 24 | 0xFCxxxxxx = fb[1] low 24.
 * Expected demo: `mw 0x40000000 0x00123456; mw 0x40000004 0x00ABCDEF` on the PS
 * makes the mailbox show 0xFB123456 / 0xFCABCDEF within one carousel period.
 */
#include <neorv32.h>
#include <stdint.h>
#define MBOX   (*(volatile uint32_t *)0xF1000000U)
#define FB(w)  (*(volatile uint32_t *)(0xF5000000U + ((w) << 2)))

int main(void) {
    neorv32_rte_setup();
    while (1) {
        MBOX = 0xFB000000u | (FB(0) & 0x00FFFFFFu);
        for (volatile uint32_t d = 0; d < 8000000u; d++) { }
        MBOX = 0xFC000000u | (FB(1) & 0x00FFFFFFu);
        for (volatile uint32_t d = 0; d < 8000000u; d++) { }
    }
    return 0;
}
