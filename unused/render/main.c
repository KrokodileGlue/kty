#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "glyph_manager.h"
#include "debug.h"

/*
 * font_manager   - holds and loads font data
 *                  get_font_for_code_point()
 * shaper         - shapes lines
 *                  shape_line()
 * sprite_manager - maintains sprite maps for each font
 *                  generate_sprites_for_line()
 * renderer       - uploads sprite maps and glyph position data
 *                  render_line()
 */

int main(void)
{
        struct glyph_manager *gm = glyph_manager_create();

        if (glyph_manager_init(gm)) {
                perror("glyph_manager_init");
                return 1;
        }

        char *pattern[] = {
                "monospace",
                "emoji",
                "cjk",
        };

        for (unsigned i = 0; i < sizeof pattern / sizeof *pattern; i++)
                if (glyph_manager_add_font_from_name(gm, pattern[i])) {
                        print("Couldn't load font %s\n", pattern[i]);
                        return 1;
                }

        struct cpu_cell cell = {
                .c = { 'o' },
                .num_code_point = 1,
                .fg = 1,
                .bg = 0,
        };

        struct glyph *glyph = glyph_manager_generate_glyph(gm, &cell, 12);
        if (!glyph) return 1;

        puts("glyph:");

        for (unsigned i = 0; i < 6; i++)
                printf("%f %f\n", glyph->vertices[i].x, glyph->vertices[i].y);

        struct cpu_cell cell2 = {
                .c = { '|' },
                .num_code_point = 1,
                .fg = 1,
                .bg = 0,
        };

        glyph = glyph_manager_generate_glyph(gm, &cell2, 12);
        if (!glyph) return 1;

        puts("glyph:");

        for (unsigned i = 0; i < 6; i++)
                printf("%f %f\n", glyph->vertices[i].x, glyph->vertices[i].y);

        /* if (glyph_manager_destroy(gm)) { */
        /*         perror("glyph_manager_destroy"); */
        /*         return 1; */
        /* } */

        return 0;
}
