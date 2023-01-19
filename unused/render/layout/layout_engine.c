// SPDX-License-Identifier: GPL-2.0-only

/*
 * layout_engine.c
 * Copyright (C) 2023 Taylor West
 */

#include <stdlib.h>
#include <assert.h>

#include "debug.h"
#include "layout_engine.h"
#include "font_manager.h"
#include "glyph_manager.h"

struct layout_engine {
        struct font_manager *fm;
        struct glyph_manager *gm;
};

/*
 * n.b.: This also creates /and initializes/ a font manager, which
 * means that it creates and uses fontconfig objects and performs i/o
 * operations to parse config files. There should only be one layout
 * engine in the application.
 */
struct layout_engine *
layout_engine_create(void)
{
        struct layout_engine *e = calloc(1, sizeof *e);

        if (!e) return NULL;

        e->fm = font_manager_create();

        if (font_manager_init(e->fm)) {
                free(e);
                return NULL;
        }

        e->gm = glyph_manager_create();

        if (glyph_manager_init(e->gm)) {
                font_manager_destroy(e->fm);
                free(e);
                return NULL;
        }

        return e;
}

int
layout_engine_add_font_from_name(struct layout_engine *e,
                                 const char *name,
                                 int font_size)
{
        return font_manager_add_font_from_name(e->fm, name, font_size);
}

/*
 * Shapes a run of cells `a` and appends the resulting glyphs to
 * `glyphs`, where `*len` is the current length of the array. The
 * length is updated.
 */
static void
do_run(struct layout_engine *e, struct font *font,
       struct glyph **glyphs, unsigned *len,
       struct cpu_cell *a, unsigned num,
       int pt_size)
{
        /*
         * Here we allocate the maximum number of code points we could
         * possibly need. My current thinking is that each cell in the
         * screen will simply have a hardcoded array of the same size,
         * but that's obviously wasteful with memory since the vast
         * majority of characters won't be long ZWJ emoji sequences or
         * have dozens of combining marks. It should be okay
         * though. This code should handle it correctly in either
         * case.
         */
        uint32_t utf32[num * MAX_CODE_POINTS_PER_CELL];

        /*
         * This is what bugs me the most about this system; it
         * requires a data transformation from the text to a flat
         * array of code points.
         */
        unsigned index = 0;
        for (unsigned i = 0; i < num; i++)
                for (unsigned j = 0; j < a[i].num_code_point; j++)
                        utf32[index++] = a[i].c[j];

        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_add_utf32(buf, utf32, index, 0, -1);
        hb_buffer_guess_segment_properties(buf);
        /* hb_buffer_set_direction(buf, HB_DIRECTION_LTR); */
        /* hb_buffer_set_language(buf, hb_language_from_string("en", -1)); */

        hb_feature_t userfeatures[] = {
                {
                        .tag = HB_TAG('c','a','l','t'),
                        .value = 1,
                        .start = HB_FEATURE_GLOBAL_START,
                        .end = HB_FEATURE_GLOBAL_END,
                },
        };

        hb_font_set_scale(font->hb_font, pt_size * 64, pt_size * 64);
        hb_shape(font->hb_font, buf, userfeatures, sizeof userfeatures / sizeof *userfeatures);

        unsigned glyph_count;
        hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);

        for (unsigned i = 0; i < glyph_count; i++)
                glyphs[(*len)++] = glyph_manager_generate_glyph(e->gm, font, glyph_info[i].codepoint, pt_size);
}


#define FONT_FOR_CELL(x)                                                \
        font_manager_get_font(e->fm, (x)->c, (x)->num_code_point,       \
                              (x)->bold, (x)->italic, pt_size);

/*
 * Converts an array of `struct cpu_cell` into an array of `struct
 * glyph`.
 *
 * This function is the main entry point into the layout engine.
 *
 * Each CPU cell contains enough information about font attributes
 * along with the actual text content (regular language text along
 * with combining marks, ZWJ sequence, emoji modifiers, etc.) to shape
 * the entire sequence like in a rich text environment. The cells are
 * shaped is if they were written in sequence as one line of text.
 */
int
layout(struct layout_engine *e, struct cpu_cell *cells,
       struct glyph **glyphs, unsigned num_cells, int pt_size)
{
        struct cpu_cell *a = cells, *b = cells;
        struct font *font_b = NULL, *font_a = FONT_FOR_CELL(a);

        unsigned index = 0;

        /*
         * Parse the input cells with two pointers `a` and `b`. The
         * beginning of a run is marked by `a` and we walk `b` forward
         * until the two cells require different fonts to represent.
         */

        while (b < cells + num_cells) {
                font_b = FONT_FOR_CELL(b);

                if (!font_a) {
                        font_a = font_b;
                        a = ++b;
                        continue;
                }

                if (font_a == font_b) {
                        b++;
                        continue;
                }

                do_run(e, font_a, glyphs, &index, a, b - a, pt_size);

                a = b;
                font_a = font_b;
        }

        do_run(e, font_a, glyphs, &index, a, b - a, pt_size);

        assert(index == num_cells);

        return 0;
}

#undef FONT_FOR_CELL

int
layout_engine_show(struct layout_engine *e)
{
        return glyph_manager_show(e->gm);
}

int
layout_engine_destroy(struct layout_engine *e)
{
        return glyph_manager_destroy(e->gm)
                || font_manager_destroy(e->fm);
}
