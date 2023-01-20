// SPDX-License-Identifier: GPL-2.0-only

/*
 * gpu_cell.h
 * Copyright (C) 2023 Taylor West
 */

#pragma once

#include "vec.h"

struct gpu_cell {
        struct ivec2 position;
        struct ivec2 size;
        struct ivec2 texture_coordinates[2];
        int tex;
        struct vec3 fg, bg;
};
