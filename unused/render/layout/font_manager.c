// SPDX-License-Identifier: GPL-2.0-only

/*
 * font_manager.c
 * Copyright (C) 2023 Taylor West
 *
 * This file contains data structures and functions related to the
 * management of font data.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <math.h>
#include <ctype.h>

#include <fontconfig/fontconfig.h>
#include <freetype/freetype.h>
#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>

#include "font_manager.h"
#include "debug.h"

struct font_manager {
        FcConfig *fc_config;
        FT_Library ft_library;
        struct font *fonts;
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

        m->fc_config = FcInitLoadConfigAndFonts();

        if (!m->fc_config) {
                /* FcFini(); */
                return 1;
        }

        if (FT_Init_FreeType(&m->ft_library)) {
                FcConfigDestroy(m->fc_config);
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
                hb_font_destroy(font->hb_font);
                free(font->name);
                font = font->next;
        }

	FcConfigDestroy(m->fc_config);
        FT_Done_FreeType(m->ft_library);
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
font_manager_add_font_from_name(struct font_manager *m, const char *name, int font_size)
{
	FcPattern *fc_pattern = FcNameParse((FcChar8 *)name);

        /*
         * These functions configure the kind of query we'd like to
         * perform. They ask FontConfig to do a kind of loose matching
         * that allows us to use vague names like "monospace".
         */
	FcConfigSubstitute(m->fc_config, fc_pattern, FcMatchPattern);
	FcDefaultSubstitute(fc_pattern);

	FcResult result;
	FcPattern *fc_font = FcFontMatch(m->fc_config, fc_pattern, &result);

        if (!fc_font) {
                perror("FcFcfontMatch");
                return 1;
        }

	FcChar8 *fc_path;

        if (FcPatternGetString(fc_font, FC_FILE, 0, &fc_path) != FcResultMatch) {
                perror("FcPatternGetString");
                return 1;
        }

        print(LOG_INFORMATION, "\e[34m%s\e[m -> \e[34m%s\e[m\n", name, fc_path);

        FT_Face ft_face;

        if (FT_New_Face(m->ft_library, (char *)fc_path, 0, &ft_face)) {
                perror("FT_New_Face");
                return 1;
        }

        hb_face_t *hb_face = hb_ft_face_create(ft_face, 0);
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
                .path           = strdup((char *)fc_path),
                .ft_face        = ft_face,
                .hb_font        = hb_font,
                .has_color      = FT_HAS_COLOR(ft_face),
                .is_fixed_width = FT_IS_FIXED_WIDTH(ft_face),
                .bold           = ft_face->style_flags & FT_STYLE_FLAG_BOLD,
                .italic         = ft_face->style_flags & FT_STYLE_FLAG_ITALIC,
                .size           = font_size,
                .name           = strdup(ft_face->family_name),
                .next           = NULL,
        };

        FcPatternDestroy(fc_font);
        FcPatternDestroy(fc_pattern);

        /*
         * TODO: This is where the font size should be set, but
         * instead it's being set in `glyph_manager_generate_glyph`.
         */
        font_manager_describe_font(*head);

        return 0;
}

static bool
font_has_glyph_for_sequence(struct font *font,
                            uint32_t *utf32, unsigned len)
{
        (void)len;
        return hb_font_get_glyph(font->hb_font,
                                 utf32[0],
                                 0,
                                 &(hb_codepoint_t){0});
}

/*
 * Get the first font in the fallback hierarchy which contains the
 * code point `c`.
 *
 * This shouldn't be construed as a function which should be used
 * during rendering to fetch the font used to render a given code
 * point; you can't pick the correct glyph for a code point without
 * knowing the surrounding characters (ligatures, ZWJ sequences,
 * Arabic, emoji modifiers, cursive fonts). This is used as a jumping
 * off point for rendering longer sequences; specifically the glyph
 * manager uses this to identify which font should be used to render a
 * sequence which corresponds to a single glyph. In fact, this
 * function is used to segment a line of text into runs.
 */
struct font *
font_manager_get_font(struct font_manager *m,
                      uint32_t *seq,
                      unsigned len,
                      bool bold,
                      bool italic,
                      int font_size)
{
        /*
         * We might care about `font_size` for bitmap fonts but for
         * the scalable fonts I use it's not really an issue.
         *
         * Before you render a font returned by this function you
         * always need to set its scale manually.
         */
        (void)font_size;

        struct font *font = m->fonts, *fallback = NULL;

        while (font) {
                /*
                 * If the font doesn't contain a glyph for the code
                 * point continue.
                 */
                if (!font_has_glyph_for_sequence(font, seq, len)) {
                        font = font->next;
                        continue;
                }

                if (font->bold != bold || font->italic != italic) {
                        fallback = font;
                        font = font->next;
                        continue;
                }

                static char buf[1000];

                buf[0] = 0;

                for (unsigned i = 0; i < len; i++)
                        snprintf(buf + strlen(buf), sizeof buf -
                                 strlen(buf), "U+%"PRIX32" ", seq[i]);

                print(LOG_EVERYTHING, "Picking font %s for sequence %s\n", font->name, buf);

                return font;
        }

        /*
         * Last resort, we can't find a suitable font with the right
         * bold/italic properties, so use the first font in the last
         * that contains the code point regardless of its bold/italic
         * properties.
         */

        font = fallback;

        if (!font) {
                static char buf[1000];
                buf[0] = 0;
                for (unsigned i = 0; i < len; i++)
                        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "U+%"PRIX32" ", seq[i]);
                print(LOG_DETAIL, "\e[31mNo font for sequence %s\e[m\n", buf);
        }

        return font;
}

void
font_manager_describe_font(struct font *font)
{
        FT_Face ft_face = font->ft_face;

        long bbox_height = ceil((float)(ft_face->bbox.yMax -
                                        ft_face->bbox.yMin) / 64.0);

        print(LOG_DETAIL, "Describing font \e[31m%s\e[m (%s) at %d pt:\n", font->name, ft_face->style_name, font->size);
        print(LOG_DETAIL, "\tBBox height:\t%ld\n", bbox_height);
        print(LOG_DETAIL, "\tHas color:\t%s\n", font->has_color ? "Yes" : "No");
        print(LOG_DETAIL, "\tIs bold:\t%s\n", font->bold ? "Yes" : "No");
        print(LOG_DETAIL, "\tIs italic:\t%s\n", font->italic ? "Yes" : "No");
        print(LOG_DETAIL, "\tFixed width:\t%s\n", font->is_fixed_width ? "Yes" : "No");
        print(LOG_DETAIL, "\tFull path:\t%s\n", font->path);
        print(LOG_DETAIL, "\tStyle:\t\t%s\n", (char *)ft_face->style_name);
        print(LOG_DETAIL, "\tGlyphs:\t\t%ld\n", ft_face->num_glyphs);
}

int
font_manager_show(struct font_manager *m)
{
        print(LOG_EVERYTHING, "Font manager stats:\n");
        print(LOG_EVERYTHING, "\tFonts loaded:\t%d\n", m->num_font);

        struct font *font = m->fonts;

        while (font) {
                font_manager_describe_font(font);
                font = font->next;
        }

        return 0;
}
