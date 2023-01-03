// SPDX-License-Identifier: GPL-2.0-only

/*
 * font_manager.c
 * Copyright (C) 2022 Taylor West
 *
 * This file contains data structures and functions related to the
 * management of font data.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include <fontconfig/fontconfig.h>
#include <harfbuzz/hb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#include "font_manager.h"
#include "debug.h"

struct font_manager {
        FcConfig *config;

        struct font {
                FcPattern *fc_pattern;
                FcPattern *fc_font;
                FcChar8 *fc_path;
                hb_font_t *hb_font;
                int size;
                char *name;
                struct font *next;
        } *fonts;

        int num_font;
};

struct font_manager *
font_manager_create(void)
{
        return calloc(1, sizeof (struct font_manager));
}

/*
 * Initialize the font manager by creating and initializing resources.
 *
 * The font manager controls the FreeType instance, which isn't thread
 * safe, so this is the only one.
 */
int
font_manager_init(struct font_manager *m)
{
	if (FcInit() != FcTrue) return 1;

        m->config = FcInitLoadConfigAndFonts();

        if (!m->config) {
                /* FcFini(); */
                return 1;
        }

        return 0;
}

int
font_manager_destroy(struct font_manager *m)
{
        struct font *font = m->fonts;

        /* Free the assets associated with each font. */
        while (font) {
                FcPatternDestroy(font->fc_font);
                FcPatternDestroy(font->fc_pattern);
                hb_font_destroy(font->hb_font);
                free(font->name);
                font = font->next;
        }

	FcConfigDestroy(m->config);
	/* FcFini(); */

        return 0;
}

/*
 * Add a font which best fits the name given by `name`.
 *
 * Some notes on how fonts of different sizes are handled:
 *
 * The font "face" is the real bulk of font data that's independent of
 * font size. The harfbuzz documentation notes:
 *
 * "Font objects also have the advantage of being considerably
 * lighter-weight than face objects (remember that a face contains the
 * contents of a binary font file mapped into memory)."
 * [[https://harfbuzz.github.io/fonts-and-faces.html]]
 *
 * Our strategy is to create a new font object for every font
 * size. This ends up being cleaner than the alternative anyway,
 * because we don't really want to mingle different font sizes
 * together inside a single font because then when we look up glyphs
 * there's not an unambigious relationship between the input code
 * points and the output glyph; we have to look up the glyph and use
 * the font size to figure out which one to use. It's simpler to just
 * consider e.g. Source Code Pro at 12 pt and Source Code Pro at 18 pt
 * to be completely different fonts.
 *
 * When a font is first created it needs to have some initial size
 * just so we can create the font objects. When we request glyphs of a
 * specific size then we might create a new font with the same face
 * and set the size to whatever we want.
 */
int
font_manager_add_font_from_name(struct font_manager *m, const char *name)
{
	FcPattern *fc_pattern = FcNameParse((FcChar8 *)name);

        /*
         * These functions configure the kind of query we'd like to
         * perform. They ask FontConfig to do a kind of loose matching
         * that allows us to use vague names like "monospace".
         */
	FcConfigSubstitute(m->config, fc_pattern, FcMatchPattern);
	FcDefaultSubstitute(fc_pattern);

	FcResult result;
	FcPattern *fc_font = FcFontMatch(m->config, fc_pattern, &result);

        if (!fc_font) {
                perror("FcFcfontMatch");
                return 1;
        }

	FcChar8 *fc_path;

        if (FcPatternGetString(fc_font, FC_FILE, 0, &fc_path) != FcResultMatch) {
                perror("FcPatternGetString");
                return 1;
        }

        print("\e[34m%s\e[m -> \e[34m%s\e[m\n", name, fc_path);

        hb_blob_t *blob = hb_blob_create_from_file((char *)fc_path);
        hb_face_t *hb_face = hb_face_create(blob, 0);
        hb_font_t *hb_font = hb_font_create(hb_face);

        /* Walk to the end of the linked list. */
        struct font **head = &m->fonts;
        while (*head) head = &(*head)->next;
        *head = calloc(1, sizeof **head);
        m->num_font++;

        /*
         * Note: I like this way of initializing objects through
         * pointers with a compound literal, but a side effect is that
         * the entire structure is allocated on the stack. If `struct
         * font` ever gets big this could lead to stack smashing.
         */
        **head = (struct font){
                .fc_pattern = fc_pattern,
                .fc_font = fc_font,
                .fc_path = fc_path,
                .hb_font = hb_font,
                .name = strdup(name),
                .next = NULL,
        };

        return 0;
}

/*
 * Get the first font in the fallback hierarchy which contains the
 * code point `c`.
 */
struct font *
font_manager_get_font(struct font_manager *m,
                      uint32_t c,
                      int font_size)
{
        struct font *font = m->fonts;

        /*
         * The plan for supporting ZWJ sequences is to store the list
         * of code points in each cell. We're going to assume that a
         * ZWJ sequence never joins two code points from different
         * scripts -- that way when we're dividing a line into runs we
         * can just check the font of the first character in a cell's
         * sequence.
         */
        while (font) {
                /*
                 * If the font doesn't contain a glyph for the code
                 * point continue.
                 */
                if (!hb_font_get_glyph(font->hb_font, c, 0,
                                       &(hb_codepoint_t){0})) {
                        font = font->next;
                        continue;
                }

                if (font->size != font_size) {
                        struct font **head = &m->fonts;
                        while (*head) head = &(*head)->next;
                        *head = calloc(1, sizeof **head);
                        m->num_font++;

                        **head = (struct font){
                                .fc_pattern = font->fc_pattern,
                                .fc_font    = font->fc_font,
                                .fc_path    = font->fc_path,
                                .hb_font    = hb_font_create_sub_font(font->hb_font),
                                .size       = font_size,
                                .name       = strdup(font->name),
                                .next       = NULL,
                        };

                        hb_font_set_scale(font->hb_font,
                                          font_size * 64,
                                          font_size * 64);

                        font = *head;
                }

                return font;
        }

        return NULL;
}

hb_font_t *
font_manager_get_hb_font(struct font *font)
{
        return font->hb_font;
}

char *
font_manager_get_font_name(struct font *font)
{
        return font->name;
}

#if 0
int
font_manager_get_sizes(struct font_manager *m, int *cw, int *ch)
{
        struct font *font = font_manager_get_font(m, 'x', 12);

        hb_codepoint_t glyph;
        hb_font_get_glyph(font->hb_font, 'x', 0, &glyph);

        if (glyph) {
                *cw = hb_font_get_glyph_h_advance(font->hb_font, glyph) / 64;
                *ch = -hb_font_get_glyph_v_advance(font->hb_font, glyph) / 64;
        }

        return 0;
}
#endif
