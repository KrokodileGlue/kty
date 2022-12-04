#include <pthread.h> /* TODO: Windows support. */
#include <sys/ioctl.h>
#include <unistd.h>

#include "window.h"
#include "term.h"
#include "t.h"
#include "global.h"

void *read_shell(void *arg)
{
        struct term *f = (struct term *)arg;

        while (1) {
                int ret = read(f->master, f->buf + f->buflen, sizeof f->buf / sizeof *f->buf - f->buflen);
                if (ret <= 0) break;
                f->buflen += ret;
                int written = twrite(f, f->buf, f->buflen);
                f->buflen -= written;
                if (f->buflen > 0)
                        memmove(f->buf, f->buf + written, f->buflen);
        }

        f->shell_done = true;

        return NULL;
}

void window_title_callback(char *title)
{
        (void)title;
        /* glfwSetWindowTitle(window, title); */
}

void window_split(struct window *w)
{
        if (!w->term) {
                w->term = term_new(w->env, &w->global->font);
                w->nterm++;
                pthread_create(&w->term->thread, NULL, read_shell, w->term);
        }
}

/* void window_spawn(struct window *w) */
/* { */
/*         w->nterm++; */
/*         w->term = realloc() */
/* } */

void window_init(struct window *w, struct global *g, char **env)
{
        w->global = g;
        w->split = 0.5;
        w->env = env;
        window_split(w);
}

void window_place(struct window *w, int x0, int y0, int x1, int y1)
{
        w->x0 = x0;
        w->y0 = y0;
        w->x1 = x1;
        w->y1 = y1;

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
                        struct term *f = w->term + i;

                        f->top = 0, f->bot = height / (f->ch + LINE_SPACING) - 1;

                        /* Resize the framebuffer. */
                        glBindTexture(GL_TEXTURE_2D, f->tex_color_buffer);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                                     GL_RGB, GL_UNSIGNED_BYTE, NULL);

                        tresize(f, width / f->cw, height / (f->ch + LINE_SPACING));

                        struct winsize ws = {
                                .ws_col = f->col,
                                .ws_row = f->row,
                        };

                        if (ioctl(f->master, TIOCSWINSZ, &ws) == -1)
                                perror("ioctl");
                }
        }
}
