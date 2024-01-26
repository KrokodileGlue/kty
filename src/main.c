#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>

#include <unistd.h>
#include <sys/ioctl.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "global.h"
#include "term.h"
#include "utf8.h"
#include "t.h"
#include "util.h"
#include "window.h"
#include "platform.h"

GLFWwindow *window;
struct global *k;

void character_callback(GLFWwindow *window, uint32_t c)
{
        (void)window;
        uint8_t buf[4];
        unsigned len = 0;
        utf8encode(c, buf, &len);
        platform_write(k->focus->subprocess, (char *)buf, len);
}

static struct key {
        int keysym, mask;
        char *s;
        int appkey, appcursor;
} keys[] = {
        /* keysym           mask            string      appkey appcursor */
        { GLFW_KEY_KP_MULTIPLY,   0,              "\033Oj",       +2,    0},
        { GLFW_KEY_KP_ADD,        0,              "\033Ok",       +2,    0},
        { GLFW_KEY_KP_ENTER,      0,              "\033OM",       +2,    0},
        { GLFW_KEY_KP_ENTER,      0,              "\r",           -1,    0},
        { GLFW_KEY_KP_SUBTRACT,   0,              "\033Om",       +2,    0},
        { GLFW_KEY_KP_DECIMAL,    0,              "\033On",       +2,    0},
        { GLFW_KEY_KP_DIVIDE,     0,              "\033Oo",       +2,    0},
        { GLFW_KEY_KP_0,          0,              "\033Op",       +2,    0},
        { GLFW_KEY_KP_1,          0,              "\033Oq",       +2,    0},
        { GLFW_KEY_KP_2,          0,              "\033Or",       +2,    0},
        { GLFW_KEY_KP_3,          0,              "\033Os",       +2,    0},
        { GLFW_KEY_KP_4,          0,              "\033Ot",       +2,    0},
        { GLFW_KEY_KP_5,          0,              "\033Ou",       +2,    0},
        { GLFW_KEY_KP_6,          0,              "\033Ov",       +2,    0},
        { GLFW_KEY_KP_7,          0,              "\033Ow",       +2,    0},
        { GLFW_KEY_KP_8,          0,              "\033Ox",       +2,    0},
        { GLFW_KEY_KP_9,          0,              "\033Oy",       +2,    0},

        { GLFW_KEY_UP,          GLFW_MOD_SHIFT,                               "\033[1;2A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_ALT,                                 "\033[1;3A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_CONTROL,                             "\033[1;5A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7A", 0,  0  },
        { GLFW_KEY_UP,          GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8A", 0,  0  },
        { GLFW_KEY_UP,          0,                                            "\033[A",    0,  -1 },
        { GLFW_KEY_UP,          0,                                            "\033OA",    0,  +1 },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT,                               "\033[1;2B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_ALT,                                 "\033[1;3B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_CONTROL,                             "\033[1;5B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7B", 0,  0  },
        { GLFW_KEY_DOWN,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8B", 0,  0  },
        { GLFW_KEY_DOWN,        0,                                            "\033[B",    0,  -1 },
        { GLFW_KEY_DOWN,        0,                                            "\033OB",    0,  +1 },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT,                               "\033[1;2D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_ALT,                                 "\033[1;3D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_CONTROL,                             "\033[1;5D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7D", 0,  0  },
        { GLFW_KEY_LEFT,        GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8D", 0,  0  },
        { GLFW_KEY_LEFT,        0,                                            "\033[D",    0,  -1 },
        { GLFW_KEY_LEFT,        0,                                            "\033OD",    0,  +1 },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT,                               "\033[1;2C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_ALT,                                 "\033[1;3C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT|GLFW_MOD_ALT,                  "\033[1;4C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_CONTROL,                             "\033[1;5C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT|GLFW_MOD_CONTROL,              "\033[1;6C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_CONTROL|GLFW_MOD_ALT,                "\033[1;7C", 0,  0  },
        { GLFW_KEY_RIGHT,       GLFW_MOD_SHIFT|GLFW_MOD_CONTROL|GLFW_MOD_ALT, "\033[1;8C", 0,  0  },
        { GLFW_KEY_RIGHT,       0,                                            "\033[C",    0,  -1 },
        { GLFW_KEY_RIGHT,       0,                                            "\033OC",    0,  +1 },
        /* { GLFW_KEY_ISO_Left_Tab,GLFW_MOD_SHIFT,                               "\033[Z",    0,  0  }, */
        { GLFW_KEY_ENTER,       GLFW_MOD_ALT,                                 "\033\r",    0,  0  },
        { GLFW_KEY_ENTER,       0,                                            "\r",        0,  0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_SHIFT,                               "\033[4l",   -1, 0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_SHIFT,                               "\033[2;2~", +1, 0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_CONTROL,                             "\033[L",    -1, 0  },
        { GLFW_KEY_INSERT,      GLFW_MOD_CONTROL,                             "\033[2;5~", +1, 0  },
        { GLFW_KEY_INSERT,      0,                                            "\033[4h",   -1, 0  },
        { GLFW_KEY_INSERT,      0,                                            "\033[2~",   +1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_CONTROL,                             "\033[M",    -1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_CONTROL,                             "\033[3;5~", +1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_SHIFT,                               "\033[2K",   -1, 0  },
        { GLFW_KEY_DELETE,      GLFW_MOD_SHIFT,                               "\033[3;2~", +1, 0  },
        { GLFW_KEY_DELETE,      0,                                            "\033[P",    -1, 0  },
        { GLFW_KEY_DELETE,      0,                                            "\033[3~",   +1, 0  },
        { GLFW_KEY_BACKSPACE,   0,                                            "\177",      0,  0  },
        { GLFW_KEY_BACKSPACE,   GLFW_MOD_ALT,                                 "\033\177",  0,  0  },
        { GLFW_KEY_HOME,        GLFW_MOD_ALT,                                 "\033[1;3H", 0,  -1 },
        { GLFW_KEY_HOME,        GLFW_MOD_SHIFT,                               "\033[2J",   0,  -1 },
        { GLFW_KEY_HOME,        GLFW_MOD_SHIFT,                               "\033[1;2H", 0,  +1 },
        { GLFW_KEY_HOME,        0,                                            "\033[H",    0,  -1 },
        { GLFW_KEY_HOME,        0,                                            "\033[1~",   0,  +1 },
        { GLFW_KEY_END,         GLFW_MOD_ALT,                                 "\033[1;3F", 0,  -1 },
        { GLFW_KEY_END,         GLFW_MOD_CONTROL,                             "\033[J",    -1, 0  },
        { GLFW_KEY_END,         GLFW_MOD_CONTROL,                             "\033[1;5F", +1, 0  },
        { GLFW_KEY_END,         GLFW_MOD_SHIFT,                               "\033[K",    -1, 0  },
        { GLFW_KEY_END,         GLFW_MOD_SHIFT,                               "\033[1;2F", +1, 0  },
        { GLFW_KEY_END,         0,                                            "\033[4~",   0,  0  },
        { GLFW_KEY_PAGE_UP,     GLFW_MOD_CONTROL,                             "\033[5;5~", 0,  0  },
        { GLFW_KEY_PAGE_UP,     GLFW_MOD_SHIFT,                               "\033[5;2~", 0,  0  },
        { GLFW_KEY_PAGE_UP,     0,                                            "\033[5~",   0,  0  },
        { GLFW_KEY_PAGE_DOWN,   GLFW_MOD_CONTROL,                             "\033[6;5~", 0,  0  },
        { GLFW_KEY_PAGE_DOWN,   GLFW_MOD_SHIFT,                               "\033[6;2~", 0,  0  },
        { GLFW_KEY_PAGE_DOWN,   0,                                            "\033[6~",   0,  0  },

        { GLFW_KEY_TAB,         0,                                            "\t",        0,  0  },
        { GLFW_KEY_ESCAPE,      0,                                            "\e",      0,  0  },

        { GLFW_KEY_F1,                   0,       "\033OP" ,       0,    0},
        { GLFW_KEY_F1, /* F13 */  GLFW_MOD_SHIFT,      "\033[1;2P",     0,    0},
        { GLFW_KEY_F1, /* F25 */  GLFW_MOD_CONTROL,    "\033[1;5P",     0,    0},
        { GLFW_KEY_F1, /* F37 */  GLFW_MOD_SUPER,       "\033[1;6P",     0,    0},
        { GLFW_KEY_F1, /* F49 */  GLFW_MOD_ALT,       "\033[1;3P",     0,    0},
        /* { GLFW_KEY_F1, /\* F61 *\/  GLFW_MOD_MOD3,       "\033[1;4P",     0,    0}, */
        { GLFW_KEY_F2,                   0,       "\033OQ" ,       0,    0},
        { GLFW_KEY_F2, /* F14 */  GLFW_MOD_SHIFT,      "\033[1;2Q",     0,    0},
        { GLFW_KEY_F2, /* F26 */  GLFW_MOD_CONTROL,    "\033[1;5Q",     0,    0},
        { GLFW_KEY_F2, /* F38 */  GLFW_MOD_SUPER,       "\033[1;6Q",     0,    0},
        { GLFW_KEY_F2, /* F50 */  GLFW_MOD_ALT,       "\033[1;3Q",     0,    0},
        /* { GLFW_KEY_F2, /\* F62 *\/  GLFW_MOD_MOD3,       "\033[1;4Q",     0,    0}, */
        { GLFW_KEY_F3,                   0,       "\033OR" ,       0,    0},
        { GLFW_KEY_F3, /* F15 */  GLFW_MOD_SHIFT,      "\033[1;2R",     0,    0},
        { GLFW_KEY_F3, /* F27 */  GLFW_MOD_CONTROL,    "\033[1;5R",     0,    0},
        { GLFW_KEY_F3, /* F39 */  GLFW_MOD_SUPER,       "\033[1;6R",     0,    0},
        { GLFW_KEY_F3, /* F51 */  GLFW_MOD_ALT,       "\033[1;3R",     0,    0},
        /* { GLFW_KEY_F3, /\* F63 *\/  GLFW_MOD_MOD3,       "\033[1;4R",     0,    0}, */
        { GLFW_KEY_F4,                   0,       "\033OS" ,       0,    0},
        { GLFW_KEY_F4, /* F16 */  GLFW_MOD_SHIFT,      "\033[1;2S",     0,    0},
        { GLFW_KEY_F4, /* F28 */  GLFW_MOD_CONTROL,    "\033[1;5S",     0,    0},
        { GLFW_KEY_F4, /* F40 */  GLFW_MOD_SUPER,       "\033[1;6S",     0,    0},
        { GLFW_KEY_F4, /* F52 */  GLFW_MOD_ALT,       "\033[1;3S",     0,    0},
        { GLFW_KEY_F5,                   0,       "\033[15~",      0,    0},
        { GLFW_KEY_F5, /* F17 */  GLFW_MOD_SHIFT,      "\033[15;2~",    0,    0},
        { GLFW_KEY_F5, /* F29 */  GLFW_MOD_CONTROL,    "\033[15;5~",    0,    0},
        { GLFW_KEY_F5, /* F41 */  GLFW_MOD_SUPER,       "\033[15;6~",    0,    0},
        { GLFW_KEY_F5, /* F53 */  GLFW_MOD_ALT,       "\033[15;3~",    0,    0},
        { GLFW_KEY_F6,                   0,       "\033[17~",      0,    0},
        { GLFW_KEY_F6, /* F18 */  GLFW_MOD_SHIFT,      "\033[17;2~",    0,    0},
        { GLFW_KEY_F6, /* F30 */  GLFW_MOD_CONTROL,    "\033[17;5~",    0,    0},
        { GLFW_KEY_F6, /* F42 */  GLFW_MOD_SUPER,       "\033[17;6~",    0,    0},
        { GLFW_KEY_F6, /* F54 */  GLFW_MOD_ALT,       "\033[17;3~",    0,    0},
        { GLFW_KEY_F7,                   0,       "\033[18~",      0,    0},
        { GLFW_KEY_F7, /* F19 */  GLFW_MOD_SHIFT,      "\033[18;2~",    0,    0},
        { GLFW_KEY_F7, /* F31 */  GLFW_MOD_CONTROL,    "\033[18;5~",    0,    0},
        { GLFW_KEY_F7, /* F43 */  GLFW_MOD_SUPER,       "\033[18;6~",    0,    0},
        { GLFW_KEY_F7, /* F55 */  GLFW_MOD_ALT,       "\033[18;3~",    0,    0},
        { GLFW_KEY_F8,                   0,       "\033[19~",      0,    0},
        { GLFW_KEY_F8, /* F20 */  GLFW_MOD_SHIFT,      "\033[19;2~",    0,    0},
        { GLFW_KEY_F8, /* F32 */  GLFW_MOD_CONTROL,    "\033[19;5~",    0,    0},
        { GLFW_KEY_F8, /* F44 */  GLFW_MOD_SUPER,       "\033[19;6~",    0,    0},
        { GLFW_KEY_F8, /* F56 */  GLFW_MOD_ALT,       "\033[19;3~",    0,    0},
        { GLFW_KEY_F9,                   0,       "\033[20~",      0,    0},
        { GLFW_KEY_F9, /* F21 */  GLFW_MOD_SHIFT,      "\033[20;2~",    0,    0},
        { GLFW_KEY_F9, /* F33 */  GLFW_MOD_CONTROL,    "\033[20;5~",    0,    0},
        { GLFW_KEY_F9, /* F45 */  GLFW_MOD_SUPER,       "\033[20;6~",    0,    0},
        { GLFW_KEY_F9, /* F57 */  GLFW_MOD_ALT,       "\033[20;3~",    0,    0},
        { GLFW_KEY_F10,                  0,       "\033[21~",      0,    0},
        { GLFW_KEY_F10, /* F22 */ GLFW_MOD_SHIFT,      "\033[21;2~",    0,    0},
        { GLFW_KEY_F10, /* F34 */ GLFW_MOD_CONTROL,    "\033[21;5~",    0,    0},
        { GLFW_KEY_F10, /* F46 */ GLFW_MOD_SUPER,       "\033[21;6~",    0,    0},
        { GLFW_KEY_F10, /* F58 */ GLFW_MOD_ALT,       "\033[21;3~",    0,    0},
        { GLFW_KEY_F11,                  0,       "\033[23~",      0,    0},
        { GLFW_KEY_F11, /* F23 */ GLFW_MOD_SHIFT,      "\033[23;2~",    0,    0},
        { GLFW_KEY_F11, /* F35 */ GLFW_MOD_CONTROL,    "\033[23;5~",    0,    0},
        { GLFW_KEY_F11, /* F47 */ GLFW_MOD_SUPER,       "\033[23;6~",    0,    0},
        { GLFW_KEY_F11, /* F59 */ GLFW_MOD_ALT,       "\033[23;3~",    0,    0},
        { GLFW_KEY_F12,                  0,       "\033[24~",      0,    0},
        { GLFW_KEY_F12, /* F24 */ GLFW_MOD_SHIFT,      "\033[24;2~",    0,    0},
        { GLFW_KEY_F12, /* F36 */ GLFW_MOD_CONTROL,    "\033[24;5~",    0,    0},
        { GLFW_KEY_F12, /* F48 */ GLFW_MOD_SUPER,       "\033[24;6~",    0,    0},
        { GLFW_KEY_F12, /* F60 */ GLFW_MOD_ALT,       "\033[24;3~",    0,    0},
};

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
        (void)window, (void)mods, (void)scancode;

        if (action == GLFW_RELEASE) return;

        if (key == GLFW_KEY_INSERT && mods & GLFW_MOD_SHIFT) {
                const char *s = glfwGetClipboardString(window);
                platform_write(k->focus->subprocess, s, strlen(s));
                return;
        }

        if (key == GLFW_KEY_ENTER && mods & GLFW_MOD_CONTROL && mods & GLFW_MOD_SHIFT) {
                window_spawn(&k->window);
                return;
        }

        if (key == GLFW_KEY_SPACE && mods & GLFW_MOD_CONTROL) {
                k->window.direction = !k->window.direction;
                window_place(&k->window, k->window.x0, k->window.y0, k->window.x1, k->window.y1);
                return;
        }

        if (key == GLFW_KEY_EQUAL && mods & GLFW_MOD_CONTROL && mods & GLFW_MOD_SHIFT) {
                window_change_font_size(k->focus, 1);
                return;
        }

        if (key == GLFW_KEY_MINUS && mods & GLFW_MOD_CONTROL && mods & GLFW_MOD_SHIFT) {
                window_change_font_size(k->focus, -1);
                return;
        }

        if (key == GLFW_KEY_RIGHT_BRACKET && mods & GLFW_MOD_CONTROL) {
                k->focus = k->focus->next;
                if (!k->focus) k->focus = k->window.wterm;
                return;
        }

        struct wterm *f = k->focus;

        /*
         * The GLFW keycode things are hardcoded
         * (https://www.glfw.org/docs/3.3/group__keys.html), so it's
         * safe to use these conditionals.
         */

        if (key >= 'A' && key <= 'Z' && mods & GLFW_MOD_CONTROL) {
                platform_write(f->subprocess, (char []){ key - 'A' + 1 }, 1);
                return;
        }

        if (key >= 32 && key <= 126 && mods & GLFW_MOD_ALT) {
                platform_write(f->subprocess, (char []){ 0x1b, tolower(key) }, 2);
                return;
        }

        const char *s = NULL;

        for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) {
                if (key != keys[i].keysym) continue;
                if ((mods & keys[i].mask) != keys[i].mask) continue;

                if (!keys[i].appcursor) {
                        s = keys[i].s;
                        break;
                }

                if (f->term->mode & MODE_APPCURSOR ? keys[i].appcursor < 0 : keys[i].appcursor > 0)
                        continue;

                s = keys[i].s;
                break;
        }

        if (s) _printf("Key string \e[36m^[%s\e[0m\n", s + 1);

        if (s) platform_write(f->subprocess, s, strlen(s));
}

void window_size_callback(GLFWwindow *window, int width, int height)
{
        (void)window;
        k->font.width = width, k->font.height = height;
        window_place(&k->window, 0, 0, width, height);
}

int main(int argc, char **argv)
{
        (void)argc; /* TODO: Parse arguments. */

        if (!glfwInit()) return 1;

        /* TODO: Make the default window size configurable. */
        window = glfwCreateWindow(1280, 720, argv[0], 0, 0);

        if (!window) {
                glfwTerminate();
                return 1;
        }

        glfwMakeContextCurrent(window);

        glfwSetCharCallback(window, character_callback);
        glfwSetKeyCallback(window, key_callback);
        glfwSetWindowSizeCallback(window, window_size_callback);

        /* Initialize GLEW. */
        GLenum glew_status = glewInit();

        if (GLEW_OK != glew_status) {
                fprintf(stderr, "Error: %s\n", glewGetErrorString(glew_status));
                return 1;
        }

        if (!GLEW_VERSION_2_0) {
                fprintf(stderr, "No support for OpenGL 2.0 found\n");
                return 1;
        }

        k = calloc(1, sizeof *k);

        global_init(k);

        int width, height;
        glfwGetWindowSize(window, &width, &height);
        window_size_callback(window, width, height);

        window_spawn(&k->window);
        k->focus = k->window.wterm;

        global_render(k);

        glfwSwapBuffers(window);

        while (!glfwWindowShouldClose(window) && k->focus) {
                global_render(k);
                glfwSwapBuffers(window);
                glfwPollEvents();
        }

        glfwTerminate();

        return 0;
}
