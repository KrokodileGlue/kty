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

static void
show_glyph(struct glyph_manager *gm, uint32_t c, int size)
{
        struct glyph *glyph = glyph_manager_generate_glyph(gm,
                                                           (uint32_t []){ c }, 1,
                                                           0, 1,
                                                           size);
        if (!glyph) return;

        /* printf("glyph %d (%d): %d,%d\n", glyph->id, glyph->index, glyph->size.x, glyph->size.y); */

        struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(gm, glyph->glyph_sheet);

        /* printf("id: %d\n", sheet.id); */
        /* printf("width: %d\n", sheet.width); */
        /* printf("height: %d\n", sheet.height); */
        /* printf("data: %p\n", sheet.data); */
        /* printf("format: %s\n", (char *[]){ "alpha", "color" }[sheet.format]); */
}

int main(void)
{
        struct glyph_manager *gm = glyph_manager_create();

        if (glyph_manager_init(gm)) {
                perror("glyph_manager_init");
                return 1;
        }

        char *pattern[] = {
                "monospace",
                "monospace:bold",
                "monospace:italic",
                "emoji",
                "Noto Serif CJK JP",
        };

        for (unsigned i = 0; i < sizeof pattern / sizeof *pattern; i++)
                if (glyph_manager_add_font_from_name(gm, pattern[i], 12)) {
                        print("Couldn't load font %s\n", pattern[i]);
                        return 1;
                }

        for (int i = 33; i <= 126; i++)
                show_glyph(gm, i, 36);

        for (int i = 0; i < 30; i++)
                show_glyph(gm, 0x1f914 + i, 24);

        glyph_manager_show(gm);

        /* if (glyph_manager_destroy(gm)) { */
        /*         perror("glyph_manager_destroy"); */
        /*         return 1; */
        /* } */

        return 0;
}
