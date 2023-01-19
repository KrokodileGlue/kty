#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "config.h"

struct cpu_cell {
        uint32_t c[MAX_CODE_POINTS_PER_CELL];
        unsigned num_code_point;
        int fg, bg;
        bool bold, italic;
};
