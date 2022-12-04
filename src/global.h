#ifndef GLOBAL_H
#define GLOBAL_H

#include <GL/glew.h> // IWYU pragma: keep
#include <GLFW/glfw3.h>

#include "render.h"
#include "util.h"
#include "font.h"

struct term;

struct global {
        struct term *term;
        struct term *focus; /* Currently focused term */
        struct color color256[256];
        /* TODO: Rename these. */
        struct font_renderer font;
        struct font_manager m;
        GLFWwindow *glfw_window;
        void (*window_title_callback)(char *);
};

int global_init(struct global *k, char **env, void (*)(char *));
int global_notify_title_change(struct term *f);
int global_render(struct global *k);

#endif
