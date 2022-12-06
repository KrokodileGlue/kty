#include "window.h"
#include "term.h"
#include "t.h"
#include "global.h"
#include "platform.h"

extern struct global *k;

/* TODO: Move this into platform. */
void *read_shell(void *arg)
{
        struct wterm *wt = (struct wterm *)arg;

        while (1) {
                int ret = platform_read(wt->subprocess, wt->buf + wt->buflen, sizeof wt->buf / sizeof *wt->buf - wt->buflen);
                if (ret <= 0) break;
                wt->buflen += ret;
                int written = twrite(wt->term, wt->buf, wt->buflen);
                wt->buflen -= written;
                if (wt->buflen > 0)
                        memmove(wt->buf, wt->buf + written, wt->buflen);
        }

        return NULL;
}

void window_title_callback(char *title)
{
        (void)title;
        /* glfwSetWindowTitle(window, title); */
}

/*
 * Add a new wterm to the end of the linked list.
 */
static void append_wterm(struct window *w, struct wterm *wterm)
{
        struct wterm **head = &w->wterm;
        while (*head) head = &(*head)->next;
        *head = wterm;
        w->nterm++;
}

void window_spawn(struct window *w)
{
        struct wterm *wt = calloc(1, sizeof *wt); /* TODO: xmalloc */
        *wt = (struct wterm){
                .term = term_new(w->x1 - w->x0, w->y1 - w->y0),
                .font_size = 12, /* TODO: Configure default font size */
        };
        append_wterm(w, wt);

        int cw, ch;
        font_get_dimensions(&k->m, &cw, &ch, 12);
        term_set_font_size(wt->term, cw, ch);
        wt->subprocess = platform_spawn_shell();
        window_place(w, w->x0, w->y0, w->x1, w->y1);
        pthread_create(&wt->thread, NULL, read_shell, wt);
}

void window_split(struct window *w)
{
        if (!w->wterm) window_spawn(w);
}

void window_init(struct window *w)
{
        if (!w) return;
        w->split = 0.5;
        w->direction = WINDOW_HORIZONTAL;
}

void window_place(struct window *w, int x0, int y0, int x1, int y1)
{
        if (!w) return;

        w->x0 = x0;
        w->y0 = y0;
        w->x1 = x1;
        w->y1 = y1;

        if (!w->wterm) return;

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

                for (struct wterm *wt = w->wterm; wt; wt = wt->next) {
                        struct term *t = wt->term;
                        /* TODO: Move this somewhere sensible. */
                        glBindTexture(GL_TEXTURE_2D, t->tex_color_buffer);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                                     GL_RGB, GL_UNSIGNED_BYTE, NULL);
                        term_resize(t, width, height);
                        platform_inform_subprocess_of_resize(wt->subprocess, t->g->col, t->g->row);
                }
        }
}

void window_render(struct window *w, struct font_renderer *r)
{
        for (struct wterm *wt = w->wterm; wt; wt = wt->next)
                render_term(r, wt->term, wt->font_size);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0, 0, r->width, r->height);

        int i = 0;
        for (struct wterm *wt = w->wterm; wt; wt = wt->next, i++) {
                struct term *t = wt->term;

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
