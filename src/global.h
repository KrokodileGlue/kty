#ifndef GLOBAL_H
#define GLOBAL_H

#include <GL/glew.h> // IWYU pragma: keep
#include <GLFW/glfw3.h>

#include "render.h"
#include "util.h"
#include "font.h"

struct frame;

struct global {
        struct frame *frame[10];
        int nframe;
        struct frame *focus; /* Currently focused frame */
        struct color color256[256];
        /* TODO: Rename these. */
        struct font_renderer font;
        struct font_manager m;
        GLFWwindow *glfw_window;
        void (*window_title_callback)(char *);
};

int global_init(struct global *k, char **env, void (*)(char *));
int global_notify_title_change(struct frame *f);
int global_render(struct global *k);

#endif
