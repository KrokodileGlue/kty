// SPDX-License-Identifier: GPL-2.0-only

/*
 * glyph_manager.c
 * Copyright (C) 2022 Taylor West
 *
 * This file contains data structures and functions related to the
 * management of glyphs.
 */

#include <string.h>
#include <assert.h>
#include <sys/param.h>
#include <inttypes.h>
#include <math.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>
#include <harfbuzz/hb.h>

#include "debug.h"
#include "font_manager.h"
#include "glyph_manager.h"

struct glyph_manager {
        struct font_manager *fm;

        /* Cairo sprite map for each font */
        struct sprite_map {
                /*
                 * This surface is the CPU texture object from which
                 * we can extract information to send to the GPU.
                 */
                cairo_surface_t *cairo_surface;

                /*
                 * For regular alpha fonts this will have format
                 * `CAIRO_FORMAT_A8` and for color fonts (i.e. emoji)
                 * it will have `CAIRO_FORMAT_ARGB32`.
                 */
                cairo_format_t cairo_format;

                int width, height;
                struct ivec2 cursor;
                struct font *font;

                int max_height[2];
                unsigned num_glyph;
        } **sprite_map;

        unsigned capacity_sprite_map;
        unsigned num_sprite_map;

        struct glyph *glyph;

        unsigned capacity_glyph;
        unsigned num_glyph;
};

struct glyph_manager *
glyph_manager_create(void)
{
        return calloc(1, sizeof (struct glyph_manager));
}

/*
 * Initialized the glyph manager.
 *
 * n.b.: This also creates /and initializes/ a font manager, which
 * means that it creates and uses fontconfig objects and performs i/o
 * operations to parse config files. There should only be one glyph
 * manager in the application.
 */
int
glyph_manager_init(struct glyph_manager *m)
{
        memset(m, 0, sizeof *m);
        m->fm = font_manager_create();
        if (!m->fm) return 1;
        if (font_manager_init(m->fm)) return 1;
        return 0;
}

/*
 * Cleans up glyph manager resources.
 */
int
glyph_manager_destroy(struct glyph_manager *m)
{
        if (font_manager_destroy(m->fm)) {
                perror("font_manager_destroy");
                return 1;
        }

        return 0;
}

/*
 * Look up the glyph for a given `cpu_cell`.
 *
 * Returns NULL if no glyph could be found for the given cpu cell.
 */
static struct glyph *
look_up_glyph(struct glyph_manager *m, struct cpu_cell *c)
{
        /* TODO: Use a hash for this lookup. */
        for (unsigned i = 0; i < m->num_glyph; i++) {
                if (m->glyph[i].num_code_point != c->num_code_point)
                        continue;
                if (memcmp(m->glyph[i].c, c->c,
                           c->num_code_point * sizeof *c->c))
                        continue;
                return m->glyph + i;
        }

        return NULL;
}

/*
 * Creates an entry in the glyph table for the glyph appropriate for
 * the given cpu cell. Note that this requires more information than
 * just the code points; it also needs information about attributes
 * like bold/italic which are contained in the cpu cell.
 */
struct glyph *
glyph_manager_generate_glyph(struct glyph_manager *m,
                             struct cpu_cell *c,
                             int font_size)
{
        struct glyph *glyph = look_up_glyph(m, c);

        /* If the glyph already exists then we can just return it. */
        if (glyph) return glyph;

        /* The glyph table is full and has to be expanded. */
        if (m->num_glyph == m->capacity_glyph) {
                /* Why not make 10 the minimum number of glyphs? */
                m->capacity_glyph = MAX(10, 2 * m->capacity_glyph);
                m->glyph = realloc(m->glyph, m->capacity_glyph * sizeof *m->glyph);

                /* Zero out any unallocated glyphs. */
                memset(m->glyph + m->num_glyph, 0,
                       (m->capacity_glyph - m->num_glyph) * sizeof *m->glyph);
        }

        /*
         * Now we have to do the heavy lifting of actually generating
         * all of the glyph information by shaping with Harfbuzz and
         * extracting glyph information.
         */

        /*
         * Kind of hacky; right now we'll just assume that every
         * character in a ZWJ sequence has the same script and the
         * same font. That's why we can just look up the font for the
         * first character in the sequence and use it for shaping the
         * entire sequence.
         */
        struct font *font = font_manager_get_font(m->fm, c->c[0], font_size);

        assert(font);

        char *font_name = font_manager_get_font_name(font);
        hb_font_t *hb_font = font_manager_get_hb_font(font);

        assert(hb_font);

        print("Picking font %s for U+%"PRIX32" (%c)\n", font_name, c->c[0], c->c[0]);

        hb_buffer_t *buf = hb_buffer_create();

        hb_buffer_add_codepoints(buf, c->c, c->num_code_point, 0, -1);
        hb_buffer_set_direction(buf, HB_DIRECTION_LTR);
        hb_buffer_set_language(buf, hb_language_from_string("en", -1));

        hb_shape(hb_font, buf, NULL, 0);

        unsigned glyph_count;
        hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);

        assert(glyph_count == 1); /* A cell should only have one glyph */

        /*
         * At this point we have everything we need to construct the
         * gpu cell.
         */

        glyph = m->glyph + m->num_glyph;
        m->num_glyph++;

        hb_glyph_extents_t extents;

        hb_font_get_glyph_extents(hb_font,
                                  glyph_info->codepoint,
                                  &extents);

        struct ivec2 size = {
                .x = ceil((float)extents.width / 64.0),
                .y = ceil(-(float)extents.height / 64.0)
        };

        struct ivec2 bearing = {
                .x = ceil((float)extents.x_bearing / 64.0),
                .y = ceil((float)extents.y_bearing / 64.0)
        };

        print("size: %d,%d / bearing: %d,%d\n", size.x, size.y,
              bearing.x, bearing.y);

        struct vec2 vertices[6] = {
                { bearing.x,          bearing.y },
                { bearing.x + size.x, bearing.y },
                { bearing.x,          bearing.y + size.y },
                { bearing.x,          bearing.y + size.y },
                { bearing.x + size.x, bearing.y + size.y },
                { bearing.x + size.x, bearing.y },
        };

        assert(sizeof vertices == sizeof glyph->vertices);
        memcpy(glyph->vertices, vertices, sizeof vertices);

        return glyph;
}

int
glyph_manager_add_font_from_name(struct glyph_manager *m, const char *name)
{
        return font_manager_add_font_from_name(m->fm, name);
}
