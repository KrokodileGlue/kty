#include "font.h"
#include <freetype/tttables.h>
#include "utf8.h"

int is_color_font(FT_Face face)
{
        static const uint32_t tag = FT_MAKE_TAG('C', 'B', 'D', 'T');
        unsigned long length = 0;
        FT_Load_Sfnt_Table(face, tag, 0, NULL, &length);
        return !!length;
}

int font_manager_init(struct font_manager *m, int *cw, int *ch)
{
        /* TODO: Get fonts from command line options. */

        if (FT_Init_FreeType(&m->ft))
                return 1;

        static struct {
                char *path;
                int type;
        } path[] = {
                //{ "Minecraft.ttf", FONT_REGULAR },
                //{ "PixelOperatorMono8.ttf", FONT_REGULAR },
                { "SourceCodePro-Regular.otf", FONT_REGULAR },
                { "SourceCodePro-Bold.otf", FONT_BOLD },
                { "SourceCodePro-It.otf", FONT_ITALIC },
                { "SourceCodePro-BoldIt.otf", FONT_BOLD | FONT_ITALIC },
                { "NotoColorEmoji.ttf", FONT_REGULAR },
                { "NotoSansCJK-Regular.ttc", FONT_REGULAR },
                { "TibMachUni-1.901b.ttf", FONT_REGULAR },
                { "DejaVuSansMono.ttf", FONT_REGULAR },
        };

        for (unsigned i = 0; i < sizeof path / sizeof *path; i++) {
                FT_Face face;

                if (FT_New_Face(m->ft, path[i].path, 0, &face)) {
                        fprintf(stderr, "Couldn't open font ‘%s’\n", path[i].path);
                        return 1;
                }

                /*
                 * Hacky; this assumes that the first font in the list is the
                 * user's primary font and that the font is monospace.
                 */
                if (!i) {
                        FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);

                        /*
                         * Whatever font is being used for an ASCII character
                         * like `x` is prooooobably the right font to base the
                         * grid size on.
                         */
                        FT_Load_Char(face, 'x', FT_LOAD_COMPUTE_METRICS);
                        FT_GlyphSlot slot = face->glyph;

                        /*
                         * We need to keep the width and height independent of
                         * font used to render non-ascii characters because we
                         * have to be able to treat the terminal like a grid.
                         */
                        *cw = slot->metrics.horiAdvance / 64.0;
                        *ch = slot->metrics.vertAdvance / 64.0;
                        _printf("%d,%d\n", *cw, *ch);
                }

                _printf("Loading font %s\n", path[i].path);

                m->fonts[m->num_fonts++] = (struct font){
                        .path = path[i].path,
                        .face = face,
                        .is_color_font = is_color_font(face),
                        .render_mode = FT_RENDER_MODE_NORMAL,
                        .load_flags = 0,
                        /*
                         * TODO: Don't just hardcode this. The size obviously
                         * needs to fit inside of a single GPU texture...
                         */
                        .sprite_buffer = calloc(1, 2048 * 2048 * 4),
                        .type = path[i].type,
                };
        }

        return 0;
}

struct sprite *get_sprite(struct font_manager *r, uint32_t c, int mode)
{
        for (int i = 0; i < r->num_cell; i++) {
                if (r->cell[i].c == c &&
                        (r->cell[i].mode & (FONT_BOLD | FONT_ITALIC))
                        == (mode & (FONT_BOLD | FONT_ITALIC)))
                        return r->cell + i;
        }

        struct font *font = NULL;
        int load_flags = FT_LOAD_COLOR;
        int font_index = -1;

        for (int i = 0; i < r->num_fonts; i++) {
                struct font *fo = r->fonts + i;
                font_index = i;

                if ((mode & (CELL_BOLD | CELL_ITALIC)) != fo->type)
                        continue;

                FT_Face face = fo->face;
                FT_Set_Pixel_Sizes(face, 0, FONT_SIZE);

                uint32_t cell_index = FT_Get_Char_Index(face, c);
                if (!cell_index) continue;

                if (fo->is_color_font) {
                        if (!face->num_fixed_sizes) return NULL;

                        int best_match = 0;
                        int diff = abs(FONT_SIZE - face->available_sizes[0].width);

                        for (int i = 1; i < face->num_fixed_sizes; i++) {
                                int ndiff = abs(FONT_SIZE - face->available_sizes[i].width);
                                if (ndiff < diff) {
                                        best_match = i;
                                        diff = ndiff;
                                }
                        }

                        FT_Select_Size(face, best_match);

                        if (FT_Load_Glyph(face, cell_index, load_flags)) continue;
                        if (FT_Render_Glyph(face->glyph, fo->render_mode)) continue;
                } else {
                        if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
                }

                font = fo;
                break;
        }

        if (!font && mode) return get_sprite(r, c, 0);
        if (!font && c != 0x25a1) return get_sprite(r, 0x25a1, 0);
        if (!font) return NULL;

        FT_GlyphSlot slot = font->face->glyph;

#ifdef DEBUG
        unsigned char buf[5] = { 0 };
        utf8encode(c, buf, &(unsigned){0});
        _printf("Adding sprite U+%x (%s) to %s\n", c, buf, font->path);
#endif

        if (!slot->bitmap.buffer) {
                _printf("-> null buffer\n");

                /*
                 * TODO: This is a weird edge case that can probably be handled
                 * more cleanly. Basically the buffer will be NULL when a
                 * character like the common space (0x20) gets rendered by
                 * FreeType. The obvious solution is just oh let's return NULL
                 * because it doesn't need a sprite. That doesn't work because
                 * we might need the character advance or whatever, so we have
                 * to return SOMETHING. Also, new characters are allocated in
                 * the bitmap based on where the previous character was placed,
                 * so if we add a new sprite to this list with random texture
                 * coordinates then the spritemap allocation will go haywire.
                 * That's why we're returning a character with the same texture
                 * coordinates as the previous character; they're not actually
                 * used for rendering but they are used for spritemap
                 * allocation.
                 */
                struct sprite s = font->cell[font->num_cell - 1];
                font->cell[font->num_cell] = r->cell[r->num_cell] = (struct sprite){
                        .c = c,
                        .metrics = slot->metrics,
                        .bitmap_top = slot->bitmap_top,
                        .tex_coords = {s.tex_coords[0], s.tex_coords[1], s.tex_coords[2], s.tex_coords[3]},
                        .font = font_index,
                        .height = 0,
                        .mode = mode & (FONT_BOLD | FONT_ITALIC),
                };
                font->num_cell++;

                return &r->cell[r->num_cell++];
        }

        /* TODO: Fix this haha. */
        int cw = slot->bitmap.width, ch = slot->bitmap.rows;

        int x = 0, y = 0;

        if (font->num_cell) {
                x = font->cell[font->num_cell - 1].tex_coords[2] * 2048 + 1;
                y = font->cell[font->num_cell - 1].tex_coords[1] * 2048;
        }

        int m = 0;
        for (int i = 0; i < font->num_cell; i++)
                m = font->cell[i].height > m ? font->cell[i].height : m;

        _printf("\tStored at spritemap coordinates %d,%d\n", x, y);

        if (font->is_color_font && x + cw * 4 >= 2048 * 4) {
                x = 0;
                y += m;
        }
        if (!font->is_color_font && x >= 2048) {
                x = 0;
                y += m;
        }

        /* Write the sprite into the spritemap. */
        for (unsigned i = 0; i < slot->bitmap.rows; i++) {
                if (font->is_color_font) {
                        memcpy(font->sprite_buffer + (y + i) * 2048 * 4 + x * 4,
                               slot->bitmap.buffer + i * slot->bitmap.width  * 4,
                               slot->bitmap.width * 4);
                } else {
                        memcpy(font->sprite_buffer + (y + i) * 2048 + x,
                               slot->bitmap.buffer + i * slot->bitmap.width,
                               slot->bitmap.width);
                }
        }

        float fx0 = (float)x / 2048.0;
        float fx1 = (float)(x + slot->bitmap.width) / 2048.0;
        float fy0 = (float)y / 2048.0;
        float fy1 = (float)(y + slot->bitmap.rows) / 2048.0;

        font->cell[font->num_cell] = r->cell[r->num_cell] = (struct sprite){
                .c = c,
                .metrics = slot->metrics,
                .bitmap_top = slot->bitmap_top,
                .tex_coords = { fx0, fy0, fx1, fy1 },
                .font = font_index,
                .height = ch,
                .mode = mode & (FONT_BOLD | FONT_ITALIC),
        };

        font->spritemap_dirty = 1;
        font->num_cell++;

        return &r->cell[r->num_cell++];
}
