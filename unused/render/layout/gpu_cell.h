// SPDX-License-Identifier: GPL-2.0-only

/*
 * gpu_cell.h
 * Copyright (C) 2023 Taylor West
 */

#pragma once

#include "vec.h"

struct gpu_cell {
        struct vec2 vertices[6];
        struct vec2 texture_coordinates[6];
        int tex;
        struct vec3 fg, bg;
};
