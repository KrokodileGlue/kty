#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "layout_engine.h"
#include "debug.h"

enum debug_level debug_level = LOG_EVERYTHING;

int main(void)
{
        srand(time(0));

        struct layout_engine *e = layout_engine_create();
        if (!e) return 1;

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

        /*
         * Maybe here would be where you would detect the primary font
         * and make sure it's monospace or whatever.
         *
         * Maybe something like:
         *
         * layout_engine_detect_primary_font(e);
         * layout_engine_check_primary_font_is_monospace(e);
         */

        struct cpu_cell *cells = malloc(1000 * sizeof *cells);
        unsigned n = 0;

        /* Add all printable ASCII characters. */
        for (int i = 33; i <= 126; i++)
                cells[n++] = (struct cpu_cell){
                        .c = { i },
                        .num_code_point = 1,
                };

        /* Add some basic emoji. */
        for (int i = 0; i < 30; i++)
                cells[n++] = (struct cpu_cell){
                        .c = { 0x1f914 + i },
                        .num_code_point = 1,
                };

        cells[n++] = (struct cpu_cell){
                .c = { 0x1f914 },
                .num_code_point = 1,
        };

        cells[n++] = (struct cpu_cell){
                .c = { 0x1F3F4, 0x200D, 0x2620, 0xFE0F },
                .num_code_point = 4,
        };

        cells[n++] = (struct cpu_cell){
                .c = { ' ' },
                .num_code_point = 1,
        };

        cells[n++] = (struct cpu_cell){
                .c = { 0x301 },
                .num_code_point = 1,
        };

        cells[n++] = (struct cpu_cell){
                .c = { ' ', 0x301 },
                .num_code_point = 1,
        };

        /*
         * Recommended Emoji ZWJ Sequence 1145
         * https://unicode.org/emoji/charts/emoji-zwj-sequences.html
         */
        cells[n++] = (struct cpu_cell){
                .c = { 0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FB },
                .num_code_point = 10,
        };

        cells[n++] = (struct cpu_cell){
                .c = { 0x1f914 },
                .num_code_point = 1,
        };

        cells[n++] = (struct cpu_cell){
                .c = { 'e', 0x301 },
                .num_code_point = 2,
        };

        struct glyph **glyphs = calloc(n, sizeof *glyphs);

        layout(e, cells, glyphs, n, 24);

        layout_engine_show(e);

        return 0;
}
