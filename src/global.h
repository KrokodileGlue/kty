#pragma once

#include <GL/glew.h> // IWYU pragma: keep
#include <GLFW/glfw3.h>

#include "render.h"
#include "util.h"
#include "font.h"
#include "window.h"

struct wterm;

struct global {
        struct window window;
        struct wterm *focus;    /* Currently focused term */
        struct color color256[256];
        /* TODO: Rename these. */
        struct font_renderer font;
        struct font_manager m;
        GLFWwindow *glfw_window;
        /* void (*window_title_callback)(char *); */
};

int global_init(struct global *k);
int global_render(struct global *k);
