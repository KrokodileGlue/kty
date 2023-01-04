#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "layout_engine.h"
#include "debug.h"

enum debug_level debug_level = LOG_EVERYTHING;

static void
show_glyphs(struct layout_engine *e, uint32_t *c, unsigned len_c, int size)
{
        /*
         * Use the layout engine to separate text into runs (so the
         * layout engine owns the font manager), run harfbuzz to get
         * the glyphs, turn the glyphs into sprites through the glyph
         * manager.
         */

        struct cpu_cell *cells = malloc(len_c * sizeof *cells);

        for (unsigned i = 0; i < len_c; i++)
                cells[i] = (struct cpu_cell){
                        .c = { c[i] },
                        .num_code_point = 1,
                        .fg = 1,
                        .bg = 1,
                        .bold = rand() % 2,
                        .italic = rand() % 2,
                };

        unsigned len;
        struct glyph **glyph = layout(e, cells, len_c, size, &len);

        if (!glyph) return;

        /* printf("glyph %d (%d): %d,%d\n", glyph->id, glyph->index, glyph->size.x, glyph->size.y); */

        /* for (unsigned i = 0; i < 6; i++) */
        /*         printf("%d,%d ", glyph->sprite_coordinates[i].x, glyph->sprite_coordinates[i].y); */
        /* puts(""); */

        /* struct glyph_sheet sheet = glyph_manager_get_glyph_sheet(gm, glyph->glyph_sheet); */

        /* printf("id: %d\n", sheet.id); */
        /* printf("width: %d\n", sheet.width); */
        /* printf("height: %d\n", sheet.height); */
        /* printf("data: %p\n", sheet.data); */
        /* printf("format: %s\n", (char *[]){ "alpha", "color" }[sheet.format]); */
}

int main(void)
{
        srand(0);

        struct layout_engine *e = layout_engine_create();

        if (layout_engine_init(e)) {
                perror("layout_engine_init");
                return 1;
        }

        char *pattern[] = {
                "Fira Code:regular",
                "Fira Code:bold",
                "Fira Code:italic",
                "Fira Code:bold:italic",
                "emoji",
                "Noto Serif CJK JP",
        };

        for (unsigned i = 0; i < sizeof pattern / sizeof *pattern; i++)
                if (layout_engine_add_font_from_name(e, pattern[i], 12)) {
                        print(LOG_CRITICAL, "Couldn't load font %s\n", pattern[i]);
                        return 1;
                }

        static uint32_t c[1000];
        unsigned num_c = 0;

        for (int i = 33; i <= 126; i++)
                c[num_c++] = i;

        for (int i = 0; i < 30; i++)
                c[num_c++] = 0x1f914 + i;

        c[num_c++] = ' ';
        c[num_c++] = '-';
        c[num_c++] = '>';
        c[num_c++] = ' ';

        show_glyphs(e, c, num_c, 24);

        layout_engine_show(e);

        /* glyph_manager_show(gm); */

        /* if (glyph_manager_destroy(gm)) { */
        /*         perror("glyph_manager_destroy"); */
        /*         return 1; */
        /* } */

        return 0;
}
