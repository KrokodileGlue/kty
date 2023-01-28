// SPDX-License-Identifier: GPL-2.0-only

/*
 * layout_engine.c
 * Copyright (C) 2023 Taylor West
 */

#include <stdlib.h>
#include <assert.h>
#include <harfbuzz/hb-ft.h>

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
       struct cpu_cell *a, unsigned num_cells,
       struct glyph *glyphs, unsigned *num_glyphs,
       int pt_size)
{
        hb_buffer_t *buf = hb_buffer_create();
        hb_buffer_set_content_type(buf, HB_BUFFER_CONTENT_TYPE_UNICODE);

        for (unsigned i = 0; i < num_cells; i++)
                for (unsigned j = 0; j < a[i].num_code_point; j++)
                        hb_buffer_add(buf, a[i].c[j], 0);

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

        /* TODO: Use the glyph_info positioning. Only relevant for weird languages like Arabic. */
        for (unsigned i = 0; i < glyph_count; i++)
                glyphs[(*num_glyphs)++] = glyph_manager_generate_glyph(e->gm, font, glyph_info[i].codepoint, pt_size);
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
layout(struct layout_engine *e,
       struct cpu_cell *cells, unsigned num_cells,
       struct glyph *glyphs, unsigned *num_glyphs,
       int pt_size)
{
        struct cpu_cell *a = cells, *b = cells;
        struct font *font_b = NULL, *font_a = FONT_FOR_CELL(a);

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

                do_run(e, font_a, a, b - a, glyphs, num_glyphs, pt_size);

                a = b;
                font_a = font_b;
        }

        do_run(e, font_a, a, b - a, glyphs, num_glyphs, pt_size);

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

struct glyph_manager *
layout_engine_get_glyph_manager(struct layout_engine *e)
{
        return e->gm;
}

#define FONT_UNITS_TO_PIXELS(face,x)            \
        (int)(((float)pt_size * ((float)(x) / (float)face->units_per_EM)))

int
layout_engine_get_basic_font_info(struct layout_engine *e,
                                  struct basic_font_info *info,
                                  int pt_size)
{
        struct font *font = font_manager_get_font(e->fm, (uint32_t []){'x'}, 1, 0, 0, pt_size);
        FT_Face face = font->ft_face;
        FT_Set_Pixel_Sizes(face, 0, pt_size);

        if (FT_Load_Glyph(face, 'x', FT_LOAD_RENDER)) {
                print(LOG_CRITICAL, "Couldn't load `x'\n");
                exit(1);
        }

        FT_GlyphSlot slot = face->glyph;

        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL)) {
                print(LOG_CRITICAL, "Couldn't render `x'\n");
                exit(1);
        }

        *info = (struct basic_font_info){
                .cw = slot->metrics.horiAdvance / 64,
                .ch = slot->metrics.vertAdvance / 64,
                .ascender = FONT_UNITS_TO_PIXELS(face, face->descender),
        };

        return 0;
}
