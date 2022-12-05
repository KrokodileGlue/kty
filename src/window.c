#include <pthread.h> /* TODO: Windows support. */
#include <sys/ioctl.h>
#include <unistd.h>

#include "window.h"
#include "term.h"
#include "t.h"
#include "global.h"
#include "platform.h"

void *read_shell(void *arg)
{
        struct term *f = (struct term *)arg;

        while (1) {
                int ret = platform_read(f->subprocess, f->buf + f->buflen, sizeof f->buf / sizeof *f->buf - f->buflen);
                if (ret <= 0) break;
                f->buflen += ret;
                int written = twrite(f, f->buf, f->buflen);
                f->buflen -= written;
                if (f->buflen > 0)
                        memmove(f->buf, f->buf + written, f->buflen);
        }

        return NULL;
}

void window_title_callback(char *title)
{
        (void)title;
        /* glfwSetWindowTitle(window, title); */
}

void window_spawn(struct window *w)
{
        w->term = realloc(w->term, (w->nterm + 1) * sizeof *w->term);
        w->term[w->nterm] = term_new(&w->global->font, w->x1 - w->x0, w->y1 - w->y0);
        w->nterm++;
        window_place(w, w->x0, w->y0, w->x1, w->y1);
        pthread_create(&w->term[w->nterm - 1]->thread, NULL, read_shell, w->term[w->nterm - 1]);
}

void window_split(struct window *w)
{
        if (!w->term) window_spawn(w);
}

void window_init(struct window *w, struct global *g)
{
        w->global = g;
        w->split = 0.5;
        w->direction = WINDOW_HORIZONTAL;
}

void window_place(struct window *w, int x0, int y0, int x1, int y1)
{
        w->x0 = x0;
        w->y0 = y0;
        w->x1 = x1;
        w->y1 = y1;

        if (!w->nterm) return;

        if (w->left) {
                if (w->direction == WINDOW_VERTICAL) {
                        window_place(w->left, x0, y0, x1, y0 + (y1 - y0) * w->split);
                        window_place(w->left, x0, y0 + (y1 - y0) * w->split, x1, y0 + (y1 - y0) * (1.0 - w->split));
                } else {
                        window_place(w->left, x0, y0, x0 + (x1 - x0) * w->split, y1);
                        window_place(w->left, x0 + (x1 - x0) * w->split, y0, x1 + (x1 - x0) * (1.0 - w->split), y1);
                }
        } else {
                int width = w->direction == WINDOW_VERTICAL ? x1 - x0 : (x1 - x0) / w->nterm;
                int height = w->direction == WINDOW_VERTICAL ? (y1 - y0) / w->nterm : y1 - y0;

                for (int i = 0; i < w->nterm; i++) {
                        struct term *f = w->term[i];

                        f->width = width, f->height = height;
                        f->top = 0, f->bot = height / (f->ch + LINE_SPACING) - 1;

                        /* Resize the framebuffer. */
                        glBindTexture(GL_TEXTURE_2D, f->tex_color_buffer);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                                     GL_RGB, GL_UNSIGNED_BYTE, NULL);

                        tresize(f, width / f->cw, height / (f->ch + LINE_SPACING));
                        platform_inform_subprocess_of_resize(f->subprocess, f->col, f->row);
                }
        }
}

void window_render(struct window *w, struct font_renderer *r)
{
        for (int i = 0; i < w->nterm; i++)
                render_term(r, w->term[i]);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0, 0, r->width, r->height);

        for (int i = 0; i < w->nterm; i++) {
                struct term *t = w->term[i];
                if (w->direction == WINDOW_HORIZONTAL) {
                        render_quad(r,
                                    w->x0 + i * t->width + i,
                                    w->y0,
                                    w->x0 + (i + 1) * t->width + i,
                                    w->y1,
                                    t->tex_color_buffer);
                } else {
                        render_quad(r,
                                    w->x0,
                                    w->y0 + i * t->height + i,
                                    w->x1,
                                    w->y0 + (i + 1) * t->height + i,
                                    t->tex_color_buffer);
                }
        }
}
