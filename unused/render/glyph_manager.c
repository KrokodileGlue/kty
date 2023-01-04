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
#include "utf8.h"

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
                cairo_font_face_t *cairo_face;
                cairo_t *cr;

                int width, height;
                struct ivec2 cursor;
                struct font *font;

                bool is_full;

                int next_line;
                unsigned num_glyph;

                int id;
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

        for (unsigned i = 0; i < m->num_sprite_map; i++)
                cairo_surface_destroy(m->sprite_map[i]->cairo_surface);

        return 0;
}

/* static void */
/* initialize_sprite_map(struct glyph_manager *m, */
/*                       struct sprite_map *map, */
/*                       struct font *font, */
/*                       int font_size) */
/* { */
/*         map->font = font; */

/* } */

static struct sprite_map *
new_sprite_map(struct glyph_manager *m,
               struct font *font,
               int id)
{
        struct sprite_map *map = calloc(1, sizeof *map);

        /*
         * TODO: Set the width and height smartly. Perhaps the
         * texture size can be configured for the glyph
         * manager so that the renderer can query OpenGL for
         * the maximum texture size and set that on the glyph
         * manager.
         */
        int width = 256, height = 256;

        int pt_size = font_manager_get_font_pt_size(font);

        cairo_format_t format = font_manager_is_font_color(font) ?
                CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_A8;

        cairo_surface_t *cairo_surface = cairo_image_surface_create(format,
                                                                    width,
                                                                    height);

        cairo_t *cr = cairo_create(cairo_surface);

        FT_Face ft_face = font_manager_get_font_ft_face(font);
        cairo_font_face_t *cairo_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
        cairo_set_font_face(cr, cairo_face);
        cairo_set_font_size(cr, pt_size);

        *map = (struct sprite_map){
                .id            = id,
                .width         = width,
                .height        = height,
                .font          = font,
                .cairo_format  = format,
                .cr            = cr,
                .cairo_surface = cairo_surface,
                .cairo_face    = cairo_face,
                .next_line     = pt_size,
                .cursor        = { .y = pt_size },
        };

        print("Sprite map %d:\n", map->id);
        print("\twidth: %d\n", map->width);
        print("\theight: %d\n", map->height);
        print("\tformat: %s\n", map->cairo_format == CAIRO_FORMAT_ARGB32
              ? "CAIRO_FORMAT_ARGB32" : "CAIRO_FORMAT_A8");

        return map;
}

static struct sprite_map *
add_sprite_map(struct glyph_manager *m,
               struct font *font)
{
        if (!m->sprite_map) {
                m->capacity_sprite_map = 1;
                m->sprite_map = calloc(1, sizeof *m->sprite_map);
                int id = m->num_sprite_map++;
                return m->sprite_map[id] = new_sprite_map(m, font, id);
        }

        if (m->num_sprite_map == m->capacity_sprite_map) {
                m->capacity_sprite_map *= 2;
                m->sprite_map = realloc(m->sprite_map, m->capacity_sprite_map * sizeof *m->sprite_map);
                int id = m->num_sprite_map++;
                return m->sprite_map[id] = new_sprite_map(m, font, id);
        }

        int id = m->num_sprite_map++;
        return m->sprite_map[id] = new_sprite_map(m, font, id);
}

static struct sprite_map *
get_first_unfilled_sprite_map(struct glyph_manager *m,
                              struct font *font)
{
        for (unsigned i = 0; i < m->num_sprite_map; i++)
                if (m->sprite_map[i]->font == font
                    && !m->sprite_map[i]->is_full)
                        return m->sprite_map[i];

        return add_sprite_map(m, font);
}

static void
add_sprite_to_font(struct glyph_manager *m,
                   struct font *font,
                   struct glyph *glyph)
{
        /*
         * First look for an unfilled sprite sheet and try to insert
         * the sprite into it. If that fails, mark that sheet as full
         * and allocate a new sprite sheet for the font and add it to
         * that. That should never fail.
         */

        struct sprite_map *map = get_first_unfilled_sprite_map(m, font);
        glyph->glyph_sheet = map->id;

        cairo_glyph_t *cairo_glyphs = cairo_glyph_allocate(1);

        cairo_glyphs[0].index = glyph->id;

        if (map->cursor.x + glyph->size.x > map->width
            || map->cursor.y + glyph->size.y > map->height) {
                map->is_full = true;
                map = new_sprite_map(m, font, m->num_sprite_map);
        }

        cairo_glyphs[0].x = map->cursor.x;
        cairo_glyphs[0].y = map->cursor.y;

        map->cursor.x += glyph->size.x + 1;

        map->next_line = MAX(map->next_line, map->cursor.y + glyph->size.y);

        if (map->cursor.x + glyph->size.x > map->width) {
                map->cursor.y = map->next_line + 1;
                map->cursor.x = 0;
        }

        /* TODO: Wrap cursor to next line etc. */

        cairo_show_glyphs(map->cr, cairo_glyphs, 1);

        char buf[100];
        snprintf(buf, sizeof buf, "output-%s-%d-%d.png",
                 font_manager_get_font_name(font),
                 font_manager_get_font_pt_size(font),
                 map->id);
        cairo_surface_write_to_png(map->cairo_surface, buf);
}

/*
 * Look up the glyph for the given code points.
 *
 * Returns NULL if no glyph could be found for the given text.
 */
static struct glyph *
look_up_glyph(struct glyph_manager *m,
              uint32_t *text,
              unsigned len,
              bool bold,
              bool italic)
{
        /* TODO: Use a hash for this lookup. */
        for (unsigned i = 0; i < m->num_glyph; i++) {
                if (m->glyph[i].num_code_point != len)
                        continue;
                if (memcmp(m->glyph[i].c, text, len * sizeof *text))
                        continue;
                if (m->glyph[i].bold != bold) continue;
                if (m->glyph[i].italic != italic) continue;
                return m->glyph + i;
        }

        return NULL;
}

static struct glyph *
new_glyph(struct glyph_manager *m)
{
        /* The glyph table is full and has to be expanded. */
        if (m->num_glyph == m->capacity_glyph) {
                /* Why not make 10 the minimum number of glyphs? */
                m->capacity_glyph = MAX(10, 2 * m->capacity_glyph);
                m->glyph = realloc(m->glyph, m->capacity_glyph * sizeof *m->glyph);

                /* Zero out any unallocated glyphs. */
                memset(m->glyph + m->num_glyph, 0,
                       (m->capacity_glyph - m->num_glyph) * sizeof *m->glyph);
        }

        return m->glyph + m->num_glyph++;
}

/*
 * Creates an entry in the glyph table for the glyph appropriate for
 * the given text. Note that this requires more information than just
 * the code points; it also needs information about attributes like
 * bold/italic which are contained in the cpu cell.
 */
struct glyph *
glyph_manager_generate_glyph(struct glyph_manager *m,
                             uint32_t *text,
                             unsigned len,
                             bool bold,
                             bool italic,
                             int font_size)
{
        struct glyph *glyph = look_up_glyph(m, text, len, bold, italic);
        if (glyph) return glyph; /* If the glyph already exists then we can just return it. */
        glyph = new_glyph(m);

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
        struct font *font = font_manager_get_font(m->fm, text[0], font_size);

        assert(font);

        char *font_name = font_manager_get_font_name(font);
        hb_font_t *hb_font = font_manager_get_hb_font(font);

        assert(hb_font);

        uint8_t tmpbuf[10];
        unsigned tmpbuflen;
        utf8encode(text[0], tmpbuf, &tmpbuflen);
        print("Picking font %s for U+%"PRIX32" (%.*s)\n", font_name, text[0], tmpbuflen, (char *)tmpbuf);

        hb_buffer_t *buf = hb_buffer_create();

        hb_buffer_add_utf32(buf, text, len, 0, -1);
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

        print("\t-> size: %d,%d / bearing: %d,%d\n", size.x, size.y,
              bearing.x, bearing.y);

        struct ivec2 vertices[6] = {
                { bearing.x,          bearing.y },
                { bearing.x + size.x, bearing.y },
                { bearing.x,          bearing.y + size.y },
                { bearing.x,          bearing.y + size.y },
                { bearing.x + size.x, bearing.y + size.y },
                { bearing.x + size.x, bearing.y },
        };

        *glyph = (struct glyph){
                .id = glyph_info->codepoint,
                .index = glyph - m->glyph,
                /* int glyph_sheet; */
                .bold = bold,
                .italic = italic,
                .font = font,
                /* uint32_t c[MAX_CODE_POINTS_PER_CELL]; */
                .num_code_point = len,
                .size = size,
                /* struct ivec2 vertices[6]; */
                /* struct vec2 sprite_coordinates[6]; */
        };

        assert(sizeof vertices == sizeof glyph->vertices);
        assert(len * sizeof *text <= sizeof glyph->c);

        memcpy(glyph->vertices, vertices, sizeof vertices);
        memcpy(glyph->c, text, sizeof glyph->c);

        add_sprite_to_font(m, font, glyph);

        return glyph;
}

int
glyph_manager_add_font_from_name(struct glyph_manager *m,
                                 const char *name,
                                 int font_size)
{
        return font_manager_add_font_from_name(m->fm, name, font_size);
}

/*
 * Retrieves useful information about a specific glyph sheet from the
 * glyph sheet id.
 */
struct glyph_sheet
glyph_manager_get_glyph_sheet(struct glyph_manager *m,
                              int glyph_sheet)
{
        struct sprite_map *map = m->sprite_map[glyph_sheet];
        return (struct glyph_sheet){
                .id = glyph_sheet,
                .width = map->width,
                .height = map->height,
                .data = cairo_image_surface_get_data(map->cairo_surface),
                .format = cairo_image_surface_get_format(map->cairo_surface) == CAIRO_FORMAT_A8 ? GLYPH_SHEET_ALPHA : GLYPH_SHEET_COLOR,
        };
}

int
glyph_manager_show(struct glyph_manager *m)
{
        print("Glyph manager stats:\n");
        print("\tGlyphs loaded:\t%d (capacity %d)\n", m->num_glyph, m->capacity_glyph);
        print("\tSprite maps:\t%d (capacity %d)\n", m->num_sprite_map, m->capacity_sprite_map);

        for (unsigned i = 0; i < m->num_glyph; i++) {
                struct glyph *glyph = m->glyph + i;
                print("\tGlyph %d in %s (%d pt) (bold=%s, italic=%s)\n",
                      glyph->id,
                      font_manager_get_font_name(glyph->font),
                      font_manager_get_font_pt_size(glyph->font),
                      glyph->bold ? "Yes" : "No",
                      glyph->italic ? "Yes" : "No"
                      );
        }

        /* font_manager_show(m->fm); */
        return 0;
}
