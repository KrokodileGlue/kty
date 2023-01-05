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

        struct cpu_cell *cells = malloc((len_c + 15) * sizeof *cells);

        for (unsigned i = 0; i < len_c; i++)
                cells[i] = (struct cpu_cell){
                        .c = { c[i] },
                        .num_code_point = 1,
                        .bold = i % 2,
                };

        cells[len_c++] = (struct cpu_cell){
                .c = { 0x1f914 },
                .num_code_point = 1,
        };

        cells[len_c++] = (struct cpu_cell){
                .c = { 0x1F3F4, 0x200D, 0x2620, 0xFE0F },
                .num_code_point = 4,
        };

        cells[len_c++] = (struct cpu_cell){
                .c = { 0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FB },
                .num_code_point = 10,
        };

        cells[len_c++] = (struct cpu_cell){
                .c = { 0x1f914 },
                .num_code_point = 1,
        };

        cells[len_c++] = (struct cpu_cell){
                .c = { ' ' },
                .num_code_point = 1,
        };

        cells[len_c++] = (struct cpu_cell){
                .c = { 'e', 0x301 },
                .num_code_point = 2,
        };

        cells[len_c++] = (struct cpu_cell){
                .c = { 'e', 0x301 },
                .num_code_point = 2,
        };

        cells[len_c++] = (struct cpu_cell){
                .c = { ' ' },
                .num_code_point = 1,
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
        srand(time(0));

        struct layout_engine *e = layout_engine_create();

        if (layout_engine_init(e)) {
                perror("layout_engine_init");
                return 1;
        }

        char *pattern[] = {
                "monospace:regular",
                "monospace:bold",
                "monospace:italic",
                "monospace:bold:italic",
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

        show_glyphs(e, c, num_c, 36);

        layout_engine_show(e);

        /* glyph_manager_show(gm); */

        /* if (glyph_manager_destroy(gm)) { */
        /*         perror("glyph_manager_destroy"); */
        /*         return 1; */
        /* } */

        return 0;
}
