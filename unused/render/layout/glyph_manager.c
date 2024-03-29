// SPDX-License-Identifier: GPL-2.0-only

/*
 * glyph_manager.c
 * Copyright (C) 2023 Taylor West
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
#include "glyph_manager.h"
#include "font_manager.h"

struct glyph_manager *
glyph_manager_create(void)
{
        return calloc(1, sizeof (struct glyph_manager));
}

/*
 * Initializes the glyph manager.
 */
int
glyph_manager_init(struct glyph_manager *m)
{
        memset(m, 0, sizeof *m);
        return 0;
}

/*
 * Cleans up glyph manager resources.
 */
int
glyph_manager_destroy(struct glyph_manager *m)
{
        for (unsigned i = 0; i < m->num_sprite_map; i++)
                cairo_surface_destroy(m->sprite_map[i]->cairo_surface);

        return 0;
}

static struct sprite_map *
new_sprite_map(struct font *font, int id)
{
        struct sprite_map *map = calloc(1, sizeof *map);

        /*
         * TODO: Set the width and height smartly. Perhaps the
         * texture size can be configured for the glyph
         * manager so that the renderer can query OpenGL for
         * the maximum texture size and set that on the glyph
         * manager.
         */
        int width = 512, height = 512;

        int pt_size = font->size;

        cairo_format_t format = font->has_color ?
                CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_A8;

        cairo_surface_t *cairo_surface = cairo_image_surface_create(format,
                                                                    width,
                                                                    height);

        cairo_t *cr = cairo_create(cairo_surface);

        FT_Face ft_face = font->ft_face;
        cairo_font_face_t *cairo_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
        cairo_set_font_face(cr, cairo_face);
        /* cairo_set_font_size(cr, pt_size); */

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
                .cursor = {
                        TEXTURE_ATLAS_GLYPH_PADDING,
                        TEXTURE_ATLAS_GLYPH_PADDING
                },
        };

        print(LOG_DETAIL, "Sprite map %d:\n", map->id);
        print(LOG_DETAIL, "\twidth: %d\n", map->width);
        print(LOG_DETAIL, "\theight: %d\n", map->height);
        print(LOG_DETAIL, "\tformat: %s\n", map->cairo_format == CAIRO_FORMAT_ARGB32
              ? "CAIRO_FORMAT_ARGB32" : "CAIRO_FORMAT_A8");

        return map;
}

static struct sprite_map *
add_sprite_map(struct glyph_manager *m,
               struct font *font)
{
        if (!m->sprite_map) {
                assert(!m->num_sprite_map);
                m->capacity_sprite_map = 2;
                m->sprite_map = calloc(m->capacity_sprite_map, sizeof *m->sprite_map);
                int id = m->num_sprite_map++;
                return m->sprite_map[id] = new_sprite_map(font, id);
        }

        if (m->num_sprite_map == m->capacity_sprite_map) {
                m->capacity_sprite_map *= 2;
                m->sprite_map = realloc(m->sprite_map, m->capacity_sprite_map * sizeof *m->sprite_map);
                int id = m->num_sprite_map++;
                return m->sprite_map[id] = new_sprite_map(font, id);
        }

        int id = m->num_sprite_map++;
        return m->sprite_map[id] = new_sprite_map(font, id);
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
                   struct glyph *glyph,
                   int pt_size)
{
        /*
         * First look for an unfilled sprite sheet and try to insert
         * the sprite into it. If that fails, mark that sheet as full
         * and allocate a new sprite sheet for the font and add it to
         * that. That should never fail.
         */

        struct sprite_map *map = get_first_unfilled_sprite_map(m, font);
        glyph->glyph_sheet = map->id;

        cairo_set_font_size(map->cr, pt_size);

        cairo_glyph_t *cairo_glyphs = calloc(1, sizeof *cairo_glyphs);

        *cairo_glyphs = (cairo_glyph_t){
                .index = glyph->id,
                .x = map->cursor.x,
                .y = map->cursor.y,
        };

        cairo_text_extents_t extents;
        cairo_glyph_extents(map->cr, cairo_glyphs, 1, &extents);

        if (map->cursor.x + glyph->size.x >= map->width - TEXTURE_ATLAS_GLYPH_PADDING) {
                map->cursor.y = map->next_line + TEXTURE_ATLAS_GLYPH_PADDING;
                map->cursor.x = TEXTURE_ATLAS_GLYPH_PADDING;
        }

        if (map->cursor.y + glyph->size.y > map->height) {
                map->is_full = true;
                return add_sprite_to_font(m, font, glyph, pt_size);
        }

        cairo_glyphs->x -= extents.x_bearing;
        cairo_glyphs->y -= extents.y_bearing;

        struct ivec2 sprite_coordinates[2] = {
                { map->cursor.x                 , map->cursor.y                 },
                { map->cursor.x + glyph->size.x , map->cursor.y + glyph->size.y },
        };

        assert(sizeof sprite_coordinates == sizeof glyph->sprite_coordinates);
        memcpy(glyph->sprite_coordinates, sprite_coordinates, sizeof glyph->sprite_coordinates);

        glyph->glyph_sheet = map->id;

        /* Update the cursor for the next sprite. */
        map->cursor.x += extents.width + TEXTURE_ATLAS_GLYPH_PADDING;
        map->next_line = MAX(map->next_line, map->cursor.y + extents.height);

        if (map->cursor.x + glyph->size.x >= map->width - TEXTURE_ATLAS_GLYPH_PADDING) {
                map->cursor.y = map->next_line + TEXTURE_ATLAS_GLYPH_PADDING;
                map->cursor.x = TEXTURE_ATLAS_GLYPH_PADDING;
        }

        cairo_show_glyphs(map->cr, cairo_glyphs, 1);

        /* TODO: Get rid of this obviously. */
#if 1
        char buf[100];
        snprintf(buf, sizeof buf, "%s-%d-%d.png",
                 font->name,
                 font->size,
                 map->id);
        cairo_surface_write_to_png(map->cairo_surface, buf);

        static int global = 0;
        if (++global == 9) {
                cairo_surface_t *surface =
                        cairo_surface_create_for_rectangle(map->cairo_surface,
                                                           sprite_coordinates[0].x,
                                                           sprite_coordinates[0].y,
                                                           sprite_coordinates[1].x - sprite_coordinates[0].x,
                                                           sprite_coordinates[1].y - sprite_coordinates[0].y);
                cairo_surface_write_to_png(surface, "a.png");
        }
#endif
}

/*
 * Get a glyph from a given glyph id in the given font.
 */
static struct glyph *
look_up_glyph(struct glyph_manager *m,
              struct font *font,
              int codepoint,
              int pt_size)
{
        /*
         * TODO: Use a hash for this lookup.
         */
        for (unsigned i = 0; i < m->num_glyph; i++) {
                if (m->glyph[i].pt_size != pt_size) continue;
                if (m->glyph[i].font != font) continue;
                if (m->glyph[i].id != codepoint) continue;
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
struct glyph
glyph_manager_generate_glyph(struct glyph_manager *m,
                             struct font *font,
                             int codepoint,
                             int font_size)
{
        hb_font_t *hb_font = font->hb_font;
        hb_font_set_scale(font->hb_font, font_size * 64, font_size * 64);
        assert(hb_font);

        struct glyph *glyph = look_up_glyph(m, font, codepoint, font_size);
        if (glyph) return *glyph; /* If the glyph already exists then we can just return it. */
        glyph = new_glyph(m);

        hb_glyph_extents_t extents;
        hb_font_get_glyph_extents(hb_font, codepoint, &extents);

        struct ivec2 size = {
                .x = ceil((float)extents.width / 64.0) + 2,
                .y = ceil(-(float)extents.height / 64.0) + 2,
        };

        FT_Face face = font->ft_face;

        /*
         * TODO: Without this the first loaded glyph doesn't have the
         * right bitmap font metrics.
         */
        FT_Set_Pixel_Sizes(face, 0, font_size);

        if (FT_Load_Glyph(face, codepoint, FT_LOAD_RENDER)) {
                print(LOG_CRITICAL, "Couldn't load expected character with glyph codepoint 0x%x\n", codepoint);
                exit(1);
        }

        FT_GlyphSlot slot = face->glyph;

        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL)) {
                print(LOG_CRITICAL, "Couldn't render expected character with glyph codepoint 0x%x\n", codepoint);
                exit(1);
        }

#define FONT_UNITS_TO_PIXELS(face,x)                                    \
        (int)(((float)font_size * ((float)(x) / (float)face->units_per_EM)))

        *glyph = (struct glyph){
                .id = codepoint,
                .index = glyph - m->glyph,
                /* int glyph_sheet; */
                .pt_size = font_size,
                .bold = font->bold,
                .italic = font->italic,
                .font = font,
                .ascender = face->descender ? 0 : -font_size,
                .size = size,
                .bitmap_left = slot->bitmap_left,
                .bitmap_top = slot->bitmap_top,
        };

        add_sprite_to_font(m, font, glyph, font_size);

        return *glyph;
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
        print(LOG_DETAIL, "Glyph manager stats:\n");
        print(LOG_DETAIL, "\tGlyphs loaded:\t%d (capacity %d)\n", m->num_glyph, m->capacity_glyph);
        print(LOG_DETAIL, "\tSprite maps:\t%d (capacity %d)\n", m->num_sprite_map, m->capacity_sprite_map);

        for (unsigned i = 0; i < m->num_glyph; i++) {
                struct glyph *glyph = m->glyph + i;
                print(LOG_EVERYTHING, "\t%u - Glyph %d in %s (%d pt) (bold=%s, italic=%s)\n",
                      i,
                      glyph->id,
                      glyph->font->name,
                      glyph->font->size,
                      glyph->bold ? "Yes" : "No",
                      glyph->italic ? "Yes" : "No"
                      );
        }

        return 0;
}
