#include <stdlib.h>
#include <math.h>
#include <stdio.h>

#include <freetype2/freetype/freetype.h>

#include <harfbuzz/hb.h>
#include <harfbuzz/hb-ft.h>

#include <cairo/cairo.h>
#include <cairo/cairo-ft.h>

#define FONT "FreeSerif.otf"
#define FONT_SIZE 64

#define MARGIN 0

int main(void)
{
        hb_buffer_t *buf = hb_buffer_create();

        char *string = "اربك تكست";
        hb_buffer_add_utf8(buf, string, -1, 0, -1);
        hb_buffer_set_direction(buf, HB_DIRECTION_RTL);
        hb_buffer_set_script(buf, HB_SCRIPT_ARABIC);
        hb_buffer_set_language(buf, hb_language_from_string("ar", -1));
        /* hb_buffer_set_cluster_level(buf, HB_BUFFER_CLUSTER_LEVEL_MONOTONE_CHARACTERS); */

        FT_Library ft_library;
        FT_Face ft_face;
        FT_Error ft_error;

        if ((ft_error = FT_Init_FreeType(&ft_library))) abort();
        if ((ft_error = FT_New_Face(ft_library, FONT, 0, &ft_face))) abort();
        if ((ft_error = FT_Set_Char_Size(ft_face, FONT_SIZE*64, FONT_SIZE*64, 0, 0))) abort();

        hb_font_t *font = hb_ft_font_create(ft_face, NULL);

        hb_feature_t userfeatures[4];

        userfeatures[0].tag = HB_TAG('d','l','i','g');
        userfeatures[0].value = 1;
        userfeatures[0].start = HB_FEATURE_GLOBAL_START;
        userfeatures[0].end = HB_FEATURE_GLOBAL_END;

        userfeatures[1].tag = HB_TAG('c','a','l','t');
        userfeatures[1].value = 1;
        userfeatures[1].start = HB_FEATURE_GLOBAL_START;
        userfeatures[1].end = HB_FEATURE_GLOBAL_END;

        userfeatures[2].tag = HB_TAG('c','l','i','g');
        userfeatures[2].value = 1;
        userfeatures[2].start = HB_FEATURE_GLOBAL_START;
        userfeatures[2].end = HB_FEATURE_GLOBAL_END;

        userfeatures[3].tag = HB_TAG('l','i','g','a');
        userfeatures[3].value = 1;
        userfeatures[3].start = HB_FEATURE_GLOBAL_START;
        userfeatures[3].end = HB_FEATURE_GLOBAL_END;

        hb_shape(font, buf, userfeatures, 4);

        unsigned glyph_count;
        hb_glyph_info_t *glyph_info = hb_buffer_get_glyph_infos(buf, &glyph_count);
        hb_glyph_position_t *glyph_pos = hb_buffer_get_glyph_positions(buf, &glyph_count);

        double width = 2 * MARGIN;
        double height = 2 * MARGIN;

        for (unsigned int i = 0; i < glyph_count; i++) {
                width  += glyph_pos[i].x_advance / 64.;
                height -= glyph_pos[i].y_advance / 64.;
        }

        if (HB_DIRECTION_IS_HORIZONTAL (hb_buffer_get_direction(buf))) height += FONT_SIZE;
        else width += FONT_SIZE;

        cairo_surface_t *cairo_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32,
                                                                    ceil(width),
                                                                    ceil(height));
        cairo_t *cr = cairo_create(cairo_surface);
        cairo_set_source_rgba(cr, 1., 1., 1., 1.);
        cairo_paint(cr);
        cairo_set_source_rgba(cr, 0., 0., 0., 1.);
        cairo_translate(cr, MARGIN, MARGIN);

        cairo_font_face_t *cairo_face = cairo_ft_font_face_create_for_ft_face(ft_face, 0);
        cairo_set_font_face(cr, cairo_face);
        cairo_set_font_size(cr, FONT_SIZE);

        if (HB_DIRECTION_IS_HORIZONTAL(hb_buffer_get_direction(buf))) {
                cairo_font_extents_t font_extents;
                cairo_font_extents(cr, &font_extents);
                double baseline = (FONT_SIZE - font_extents.height) * .5 + font_extents.ascent;
                cairo_translate(cr, 0, baseline);
        } else {
                cairo_translate(cr, FONT_SIZE * .5, 0);
        }

        cairo_glyph_t *cairo_glyphs = cairo_glyph_allocate(glyph_count);

        double x = 0, y = 0;

        for (unsigned int i = 0; i < glyph_count; i++) {
                cairo_glyphs[i].index = glyph_info[i].codepoint;
                cairo_glyphs[i].x = x + glyph_pos[i].x_offset / 64.;
                cairo_glyphs[i].y = -(y + glyph_pos[i].y_offset / 64.);
                x += glyph_pos[i].x_advance / 64.;
                y += glyph_pos[i].y_advance / 64.;
        }

        cairo_show_glyphs(cr, cairo_glyphs, glyph_count);
        cairo_glyph_free(cairo_glyphs);

        cairo_surface_write_to_png(cairo_surface, "feh.png");

        cairo_font_face_destroy(cairo_face);
        cairo_destroy(cr);
        cairo_surface_destroy(cairo_surface);

        hb_buffer_destroy(buf);
        hb_font_destroy(font);
}
