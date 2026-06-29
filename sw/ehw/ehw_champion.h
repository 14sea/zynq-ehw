#ifndef EHW_CHAMPION_H
#define EHW_CHAMPION_H

#include <stdint.h>
#include "ehw_kernel.h"

// Default EHW-0.0/0.1 champion found by the xorshift GA:
//   python3 sim/oracle_evolve.py --rng xorshift --population 32 --generations 20
static const int8_t EHW_CHAMPION_GENOME[EHW_GENOME_LEN] = {
    3, -1, -3, -2,
    13, 13, 21, 18,
    -7, -3, -7, 0,
    4, 0, 2, -35,
    -2, 27, 3, 0,
    14, -14, 5, 13,
};

#endif
