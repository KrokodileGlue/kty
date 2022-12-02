#define _XOPEN_SOURCE 600

#include "global.h"

#include <GL/glew.h>            /* GLfloat, glTexParameteri, GL_ARRAY_BU... */
#include <freetype/freetype.h>  /* FT_GlyphSlotRec_, FT_FaceRec_, FT_Gly... */
#include <freetype/ftimage.h>   /* FT_Bitmap */
#include <freetype/fttypes.h>   /* FT_MAKE_TAG */
#include <freetype/tttables.h>  /* FT_Load_Sfnt_Table */
#include <inttypes.h>           /* uint32_t */
#include <stdio.h>              /* fprintf, stderr, NULL */
#include <stdlib.h>             /* calloc, abs */
#include <string.h>             /* memcpy */
#include <wchar.h>              /* wcwidth */
#include "font.h"               /* font, cell, CELL_DUMMY, CELL_INVERSE */
#include "frame.h"              /* frame, window, frame_new, cursor, MOD... */
#include "render.h"             /* font_renderer */
#include "sprite.h"             /* sprite */
#include "util.h"               /* color, LINE_SPACING, NUM_CELL, FONT_... */
#include "utf8.h"
#include "render.h"

static struct color color256[256] = {
        { 0.003922, 0.094118, 0.156863 }, /*   0 - #011828 */
        { 0.968627, 0.462745, 0.556863 }, /*   1 - #f7768e */
        { 0.450980, 0.854902, 0.792157 }, /*   2 - #73daca */
        { 0.878431, 0.811765, 0.470588 }, /*   3 - #e0cf78 */
        { 0.415686, 0.572549, 0.968627 }, /*   4 - #6a92f7 */
        { 0.482353, 0.101961, 0.780392 }, /*   5 - #7b1ac7 */
        { 0.176471, 0.811765, 1.000000 }, /*   6 - #2dcfff */
        { 0.945098, 0.945098, 0.945098 }, /*   7 - #f1f1f1 */
        { 0.003922, 0.094118, 0.156863 }, /*   8 - #011828 */
        { 0.968627, 0.462745, 0.556863 }, /*   9 - #f7768e */
        { 0.450980, 0.854902, 0.792157 }, /*  10 - #73daca */
        { 0.878431, 0.811765, 0.533333 }, /*  11 - #e0cf88 */
        { 0.878431, 0.811765, 0.533333 }, /*  12 - #e0cf88 */
        { 0.482353, 0.101961, 0.780392 }, /*  13 - #7b1ac7 */
        { 0.176471, 0.811765, 1.000000 }, /*  14 - #2dcfff */
        { 0.176471, 0.811765, 1.000000 }, /*  15 - #2dcfff */
        { 0.000000, 0.000000, 0.000000 }, /*  16 - #000000 */
        { 0.000000, 0.000000, 0.166667 }, /*  17 - #00002a */
        { 0.000000, 0.000000, 0.333333 }, /*  18 - #000055 */
        { 0.000000, 0.000000, 0.500000 }, /*  19 - #00007f */
        { 0.000000, 0.000000, 0.666667 }, /*  20 - #0000aa */
        { 0.000000, 0.000000, 0.833333 }, /*  21 - #0000d4 */
        { 0.000000, 0.166667, 0.000000 }, /*  22 - #002a00 */
        { 0.000000, 0.166667, 0.166667 }, /*  23 - #002a2a */
        { 0.000000, 0.166667, 0.333333 }, /*  24 - #002a55 */
        { 0.000000, 0.166667, 0.500000 }, /*  25 - #002a7f */
        { 0.000000, 0.166667, 0.666667 }, /*  26 - #002aaa */
        { 0.000000, 0.166667, 0.833333 }, /*  27 - #002ad4 */
        { 0.000000, 0.333333, 0.000000 }, /*  28 - #005500 */
        { 0.000000, 0.333333, 0.166667 }, /*  29 - #00552a */
        { 0.000000, 0.333333, 0.333333 }, /*  30 - #005555 */
        { 0.000000, 0.333333, 0.500000 }, /*  31 - #00557f */
        { 0.000000, 0.333333, 0.666667 }, /*  32 - #0055aa */
        { 0.000000, 0.333333, 0.833333 }, /*  33 - #0055d4 */
        { 0.000000, 0.500000, 0.000000 }, /*  34 - #007f00 */
        { 0.000000, 0.500000, 0.166667 }, /*  35 - #007f2a */
        { 0.000000, 0.500000, 0.333333 }, /*  36 - #007f55 */
        { 0.000000, 0.500000, 0.500000 }, /*  37 - #007f7f */
        { 0.000000, 0.500000, 0.666667 }, /*  38 - #007faa */
        { 0.000000, 0.500000, 0.833333 }, /*  39 - #007fd4 */
        { 0.000000, 0.666667, 0.000000 }, /*  40 - #00aa00 */
        { 0.000000, 0.666667, 0.166667 }, /*  41 - #00aa2a */
        { 0.000000, 0.666667, 0.333333 }, /*  42 - #00aa55 */
        { 0.000000, 0.666667, 0.500000 }, /*  43 - #00aa7f */
        { 0.000000, 0.666667, 0.666667 }, /*  44 - #00aaaa */
        { 0.000000, 0.666667, 0.833333 }, /*  45 - #00aad4 */
        { 0.000000, 0.833333, 0.000000 }, /*  46 - #00d400 */
        { 0.000000, 0.833333, 0.166667 }, /*  47 - #00d42a */
        { 0.000000, 0.833333, 0.333333 }, /*  48 - #00d455 */
        { 0.000000, 0.833333, 0.500000 }, /*  49 - #00d47f */
        { 0.000000, 0.833333, 0.666667 }, /*  50 - #00d4aa */
        { 0.000000, 0.833333, 0.833333 }, /*  51 - #00d4d4 */
        { 0.166667, 0.000000, 0.000000 }, /*  52 - #2a0000 */
        { 0.166667, 0.000000, 0.166667 }, /*  53 - #2a002a */
        { 0.166667, 0.000000, 0.333333 }, /*  54 - #2a0055 */
        { 0.166667, 0.000000, 0.500000 }, /*  55 - #2a007f */
        { 0.166667, 0.000000, 0.666667 }, /*  56 - #2a00aa */
        { 0.166667, 0.000000, 0.833333 }, /*  57 - #2a00d4 */
        { 0.166667, 0.166667, 0.000000 }, /*  58 - #2a2a00 */
        { 0.166667, 0.166667, 0.166667 }, /*  59 - #2a2a2a */
        { 0.166667, 0.166667, 0.333333 }, /*  60 - #2a2a55 */
        { 0.166667, 0.166667, 0.500000 }, /*  61 - #2a2a7f */
        { 0.166667, 0.166667, 0.666667 }, /*  62 - #2a2aaa */
        { 0.166667, 0.166667, 0.833333 }, /*  63 - #2a2ad4 */
        { 0.166667, 0.333333, 0.000000 }, /*  64 - #2a5500 */
        { 0.166667, 0.333333, 0.166667 }, /*  65 - #2a552a */
        { 0.166667, 0.333333, 0.333333 }, /*  66 - #2a5555 */
        { 0.166667, 0.333333, 0.500000 }, /*  67 - #2a557f */
        { 0.166667, 0.333333, 0.666667 }, /*  68 - #2a55aa */
        { 0.166667, 0.333333, 0.833333 }, /*  69 - #2a55d4 */
        { 0.166667, 0.500000, 0.000000 }, /*  70 - #2a7f00 */
        { 0.166667, 0.500000, 0.166667 }, /*  71 - #2a7f2a */
        { 0.166667, 0.500000, 0.333333 }, /*  72 - #2a7f55 */
        { 0.166667, 0.500000, 0.500000 }, /*  73 - #2a7f7f */
        { 0.166667, 0.500000, 0.666667 }, /*  74 - #2a7faa */
        { 0.166667, 0.500000, 0.833333 }, /*  75 - #2a7fd4 */
        { 0.166667, 0.666667, 0.000000 }, /*  76 - #2aaa00 */
        { 0.166667, 0.666667, 0.166667 }, /*  77 - #2aaa2a */
        { 0.166667, 0.666667, 0.333333 }, /*  78 - #2aaa55 */
        { 0.166667, 0.666667, 0.500000 }, /*  79 - #2aaa7f */
        { 0.166667, 0.666667, 0.666667 }, /*  80 - #2aaaaa */
        { 0.166667, 0.666667, 0.833333 }, /*  81 - #2aaad4 */
        { 0.166667, 0.833333, 0.000000 }, /*  82 - #2ad400 */
        { 0.166667, 0.833333, 0.166667 }, /*  83 - #2ad42a */
        { 0.166667, 0.833333, 0.333333 }, /*  84 - #2ad455 */
        { 0.166667, 0.833333, 0.500000 }, /*  85 - #2ad47f */
        { 0.166667, 0.833333, 0.666667 }, /*  86 - #2ad4aa */
        { 0.166667, 0.833333, 0.833333 }, /*  87 - #2ad4d4 */
        { 0.333333, 0.000000, 0.000000 }, /*  88 - #550000 */
        { 0.333333, 0.000000, 0.166667 }, /*  89 - #55002a */
        { 0.333333, 0.000000, 0.333333 }, /*  90 - #550055 */
        { 0.333333, 0.000000, 0.500000 }, /*  91 - #55007f */
        { 0.333333, 0.000000, 0.666667 }, /*  92 - #5500aa */
        { 0.333333, 0.000000, 0.833333 }, /*  93 - #5500d4 */
        { 0.333333, 0.166667, 0.000000 }, /*  94 - #552a00 */
        { 0.333333, 0.166667, 0.166667 }, /*  95 - #552a2a */
        { 0.333333, 0.166667, 0.333333 }, /*  96 - #552a55 */
        { 0.333333, 0.166667, 0.500000 }, /*  97 - #552a7f */
        { 0.333333, 0.166667, 0.666667 }, /*  98 - #552aaa */
        { 0.333333, 0.166667, 0.833333 }, /*  99 - #552ad4 */
        { 0.333333, 0.333333, 0.000000 }, /* 100 - #555500 */
        { 0.333333, 0.333333, 0.166667 }, /* 101 - #55552a */
        { 0.333333, 0.333333, 0.333333 }, /* 102 - #555555 */
        { 0.333333, 0.333333, 0.500000 }, /* 103 - #55557f */
        { 0.333333, 0.333333, 0.666667 }, /* 104 - #5555aa */
        { 0.333333, 0.333333, 0.833333 }, /* 105 - #5555d4 */
        { 0.333333, 0.500000, 0.000000 }, /* 106 - #557f00 */
        { 0.333333, 0.500000, 0.166667 }, /* 107 - #557f2a */
        { 0.333333, 0.500000, 0.333333 }, /* 108 - #557f55 */
        { 0.333333, 0.500000, 0.500000 }, /* 109 - #557f7f */
        { 0.333333, 0.500000, 0.666667 }, /* 110 - #557faa */
        { 0.333333, 0.500000, 0.833333 }, /* 111 - #557fd4 */
        { 0.333333, 0.666667, 0.000000 }, /* 112 - #55aa00 */
        { 0.333333, 0.666667, 0.166667 }, /* 113 - #55aa2a */
        { 0.333333, 0.666667, 0.333333 }, /* 114 - #55aa55 */
        { 0.333333, 0.666667, 0.500000 }, /* 115 - #55aa7f */
        { 0.333333, 0.666667, 0.666667 }, /* 116 - #55aaaa */
        { 0.333333, 0.666667, 0.833333 }, /* 117 - #55aad4 */
        { 0.333333, 0.833333, 0.000000 }, /* 118 - #55d400 */
        { 0.333333, 0.833333, 0.166667 }, /* 119 - #55d42a */
        { 0.333333, 0.833333, 0.333333 }, /* 120 - #55d455 */
        { 0.333333, 0.833333, 0.500000 }, /* 121 - #55d47f */
        { 0.333333, 0.833333, 0.666667 }, /* 122 - #55d4aa */
        { 0.333333, 0.833333, 0.833333 }, /* 123 - #55d4d4 */
        { 0.500000, 0.000000, 0.000000 }, /* 124 - #7f0000 */
        { 0.500000, 0.000000, 0.166667 }, /* 125 - #7f002a */
        { 0.500000, 0.000000, 0.333333 }, /* 126 - #7f0055 */
        { 0.500000, 0.000000, 0.500000 }, /* 127 - #7f007f */
        { 0.500000, 0.000000, 0.666667 }, /* 128 - #7f00aa */
        { 0.500000, 0.000000, 0.833333 }, /* 129 - #7f00d4 */
        { 0.500000, 0.166667, 0.000000 }, /* 130 - #7f2a00 */
        { 0.500000, 0.166667, 0.166667 }, /* 131 - #7f2a2a */
        { 0.500000, 0.166667, 0.333333 }, /* 132 - #7f2a55 */
        { 0.500000, 0.166667, 0.500000 }, /* 133 - #7f2a7f */
        { 0.500000, 0.166667, 0.666667 }, /* 134 - #7f2aaa */
        { 0.500000, 0.166667, 0.833333 }, /* 135 - #7f2ad4 */
        { 0.500000, 0.333333, 0.000000 }, /* 136 - #7f5500 */
        { 0.500000, 0.333333, 0.166667 }, /* 137 - #7f552a */
        { 0.500000, 0.333333, 0.333333 }, /* 138 - #7f5555 */
        { 0.500000, 0.333333, 0.500000 }, /* 139 - #7f557f */
        { 0.500000, 0.333333, 0.666667 }, /* 140 - #7f55aa */
        { 0.500000, 0.333333, 0.833333 }, /* 141 - #7f55d4 */
        { 0.500000, 0.500000, 0.000000 }, /* 142 - #7f7f00 */
        { 0.500000, 0.500000, 0.166667 }, /* 143 - #7f7f2a */
        { 0.500000, 0.500000, 0.333333 }, /* 144 - #7f7f55 */
        { 0.500000, 0.500000, 0.500000 }, /* 145 - #7f7f7f */
        { 0.500000, 0.500000, 0.666667 }, /* 146 - #7f7faa */
        { 0.500000, 0.500000, 0.833333 }, /* 147 - #7f7fd4 */
        { 0.500000, 0.666667, 0.000000 }, /* 148 - #7faa00 */
        { 0.500000, 0.666667, 0.166667 }, /* 149 - #7faa2a */
        { 0.500000, 0.666667, 0.333333 }, /* 150 - #7faa55 */
        { 0.500000, 0.666667, 0.500000 }, /* 151 - #7faa7f */
        { 0.500000, 0.666667, 0.666667 }, /* 152 - #7faaaa */
        { 0.500000, 0.666667, 0.833333 }, /* 153 - #7faad4 */
        { 0.500000, 0.833333, 0.000000 }, /* 154 - #7fd400 */
        { 0.500000, 0.833333, 0.166667 }, /* 155 - #7fd42a */
        { 0.500000, 0.833333, 0.333333 }, /* 156 - #7fd455 */
        { 0.500000, 0.833333, 0.500000 }, /* 157 - #7fd47f */
        { 0.500000, 0.833333, 0.666667 }, /* 158 - #7fd4aa */
        { 0.500000, 0.833333, 0.833333 }, /* 159 - #7fd4d4 */
        { 0.666667, 0.000000, 0.000000 }, /* 160 - #aa0000 */
        { 0.666667, 0.000000, 0.166667 }, /* 161 - #aa002a */
        { 0.666667, 0.000000, 0.333333 }, /* 162 - #aa0055 */
        { 0.666667, 0.000000, 0.500000 }, /* 163 - #aa007f */
        { 0.666667, 0.000000, 0.666667 }, /* 164 - #aa00aa */
        { 0.666667, 0.000000, 0.833333 }, /* 165 - #aa00d4 */
        { 0.666667, 0.166667, 0.000000 }, /* 166 - #aa2a00 */
        { 0.666667, 0.166667, 0.166667 }, /* 167 - #aa2a2a */
        { 0.666667, 0.166667, 0.333333 }, /* 168 - #aa2a55 */
        { 0.666667, 0.166667, 0.500000 }, /* 169 - #aa2a7f */
        { 0.666667, 0.166667, 0.666667 }, /* 170 - #aa2aaa */
        { 0.666667, 0.166667, 0.833333 }, /* 171 - #aa2ad4 */
        { 0.666667, 0.333333, 0.000000 }, /* 172 - #aa5500 */
        { 0.666667, 0.333333, 0.166667 }, /* 173 - #aa552a */
        { 0.666667, 0.333333, 0.333333 }, /* 174 - #aa5555 */
        { 0.666667, 0.333333, 0.500000 }, /* 175 - #aa557f */
        { 0.666667, 0.333333, 0.666667 }, /* 176 - #aa55aa */
        { 0.666667, 0.333333, 0.833333 }, /* 177 - #aa55d4 */
        { 0.666667, 0.500000, 0.000000 }, /* 178 - #aa7f00 */
        { 0.666667, 0.500000, 0.166667 }, /* 179 - #aa7f2a */
        { 0.666667, 0.500000, 0.333333 }, /* 180 - #aa7f55 */
        { 0.666667, 0.500000, 0.500000 }, /* 181 - #aa7f7f */
        { 0.666667, 0.500000, 0.666667 }, /* 182 - #aa7faa */
        { 0.666667, 0.500000, 0.833333 }, /* 183 - #aa7fd4 */
        { 0.666667, 0.666667, 0.000000 }, /* 184 - #aaaa00 */
        { 0.666667, 0.666667, 0.166667 }, /* 185 - #aaaa2a */
        { 0.666667, 0.666667, 0.333333 }, /* 186 - #aaaa55 */
        { 0.666667, 0.666667, 0.500000 }, /* 187 - #aaaa7f */
        { 0.666667, 0.666667, 0.666667 }, /* 188 - #aaaaaa */
        { 0.666667, 0.666667, 0.833333 }, /* 189 - #aaaad4 */
        { 0.666667, 0.833333, 0.000000 }, /* 190 - #aad400 */
        { 0.666667, 0.833333, 0.166667 }, /* 191 - #aad42a */
        { 0.666667, 0.833333, 0.333333 }, /* 192 - #aad455 */
        { 0.666667, 0.833333, 0.500000 }, /* 193 - #aad47f */
        { 0.666667, 0.833333, 0.666667 }, /* 194 - #aad4aa */
        { 0.666667, 0.833333, 0.833333 }, /* 195 - #aad4d4 */
        { 0.833333, 0.000000, 0.000000 }, /* 196 - #d40000 */
        { 0.833333, 0.000000, 0.166667 }, /* 197 - #d4002a */
        { 0.833333, 0.000000, 0.333333 }, /* 198 - #d40055 */
        { 0.833333, 0.000000, 0.500000 }, /* 199 - #d4007f */
        { 0.833333, 0.000000, 0.666667 }, /* 200 - #d400aa */
        { 0.833333, 0.000000, 0.833333 }, /* 201 - #d400d4 */
        { 0.833333, 0.166667, 0.000000 }, /* 202 - #d42a00 */
        { 0.833333, 0.166667, 0.166667 }, /* 203 - #d42a2a */
        { 0.833333, 0.166667, 0.333333 }, /* 204 - #d42a55 */
        { 0.833333, 0.166667, 0.500000 }, /* 205 - #d42a7f */
        { 0.833333, 0.166667, 0.666667 }, /* 206 - #d42aaa */
        { 0.833333, 0.166667, 0.833333 }, /* 207 - #d42ad4 */
        { 0.833333, 0.333333, 0.000000 }, /* 208 - #d45500 */
        { 0.833333, 0.333333, 0.166667 }, /* 209 - #d4552a */
        { 0.833333, 0.333333, 0.333333 }, /* 210 - #d45555 */
        { 0.833333, 0.333333, 0.500000 }, /* 211 - #d4557f */
        { 0.833333, 0.333333, 0.666667 }, /* 212 - #d455aa */
        { 0.833333, 0.333333, 0.833333 }, /* 213 - #d455d4 */
        { 0.833333, 0.500000, 0.000000 }, /* 214 - #d47f00 */
        { 0.833333, 0.500000, 0.166667 }, /* 215 - #d47f2a */
        { 0.833333, 0.500000, 0.333333 }, /* 216 - #d47f55 */
        { 0.833333, 0.500000, 0.500000 }, /* 217 - #d47f7f */
        { 0.833333, 0.500000, 0.666667 }, /* 218 - #d47faa */
        { 0.833333, 0.500000, 0.833333 }, /* 219 - #d47fd4 */
        { 0.833333, 0.666667, 0.000000 }, /* 220 - #d4aa00 */
        { 0.833333, 0.666667, 0.166667 }, /* 221 - #d4aa2a */
        { 0.833333, 0.666667, 0.333333 }, /* 222 - #d4aa55 */
        { 0.833333, 0.666667, 0.500000 }, /* 223 - #d4aa7f */
        { 0.833333, 0.666667, 0.666667 }, /* 224 - #d4aaaa */
        { 0.833333, 0.666667, 0.833333 }, /* 225 - #d4aad4 */
        { 0.833333, 0.833333, 0.000000 }, /* 226 - #d4d400 */
        { 0.833333, 0.833333, 0.166667 }, /* 227 - #d4d42a */
        { 0.833333, 0.833333, 0.333333 }, /* 228 - #d4d455 */
        { 0.833333, 0.833333, 0.500000 }, /* 229 - #d4d47f */
        { 0.833333, 0.833333, 0.666667 }, /* 230 - #d4d4aa */
        { 0.833333, 0.833333, 0.833333 }, /* 231 - #d4d4d4 */
        { 0.040000, 0.040000, 0.040000 }, /* 232 - #0a0a0a */
        { 0.080000, 0.080000, 0.080000 }, /* 233 - #141414 */
        { 0.120000, 0.120000, 0.120000 }, /* 234 - #1e1e1e */
        { 0.160000, 0.160000, 0.160000 }, /* 235 - #282828 */
        { 0.200000, 0.200000, 0.200000 }, /* 236 - #333333 */
        { 0.240000, 0.240000, 0.240000 }, /* 237 - #3d3d3d */
        { 0.280000, 0.280000, 0.280000 }, /* 238 - #474747 */
        { 0.320000, 0.320000, 0.320000 }, /* 239 - #515151 */
        { 0.360000, 0.360000, 0.360000 }, /* 240 - #5b5b5b */
        { 0.400000, 0.400000, 0.400000 }, /* 241 - #666666 */
        { 0.440000, 0.440000, 0.440000 }, /* 242 - #707070 */
        { 0.480000, 0.480000, 0.480000 }, /* 243 - #7a7a7a */
        { 0.520000, 0.520000, 0.520000 }, /* 244 - #848484 */
        { 0.560000, 0.560000, 0.560000 }, /* 245 - #8e8e8e */
        { 0.600000, 0.600000, 0.600000 }, /* 246 - #999999 */
        { 0.640000, 0.640000, 0.640000 }, /* 247 - #a3a3a3 */
        { 0.680000, 0.680000, 0.680000 }, /* 248 - #adadad */
        { 0.720000, 0.720000, 0.720000 }, /* 249 - #b7b7b7 */
        { 0.760000, 0.760000, 0.760000 }, /* 250 - #c1c1c1 */
        { 0.800000, 0.800000, 0.800000 }, /* 251 - #cccccc */
        { 0.840000, 0.840000, 0.840000 }, /* 252 - #d6d6d6 */
        { 0.880000, 0.880000, 0.880000 }, /* 253 - #e0e0e0 */
        { 0.920000, 0.920000, 0.920000 }, /* 254 - #eaeaea */
        { 0.960000, 0.960000, 0.960000 }, /* 255 - #f4f4f4 */
};

int global_init(struct global *k, char **env, void (*window_title_callback)(char *))
{
        int cw, ch;
        font_manager_init(&k->m, &cw, &ch);
        memcpy(&k->color256, color256, sizeof k->color256);
        render_init(&k->font, &k->m, k->color256);

        struct frame *f = frame_new(env, &k->font);

        f->focused = 1;
        f->cw = cw;
        f->ch = ch;

        k->frame[k->nframe++] = f;

        k->window_title_callback = window_title_callback;
        k->focus = f;

        return 0;
}

int global_notify_title_change(struct frame *f)
{
        struct global *k = f->k;
        if (f == k->focus)
                k->window_title_callback(f->title);
        return 0;
}

int global_render(struct global *f)
{
        /* TODO */
        struct font_renderer *r = &f->font;
        render_frame(&f->font, f->frame[0]);
        render_quad(r, f->frame[0]->tex_color_buffer);
        return 0;
}
