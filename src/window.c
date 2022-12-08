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

void init_gl_resources(struct wterm *wt, int width, int height)
{
        glGenFramebuffers(1, &wt->framebuffer);

        glBindFramebuffer(GL_FRAMEBUFFER, wt->framebuffer);

        /* Set up the texture */
        glGenTextures(1, &wt->tex_color_buffer);
        glBindTexture(GL_TEXTURE_2D, wt->tex_color_buffer);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        /* TODO: Don't use the window width and height here. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glBindTexture(GL_TEXTURE_2D, 0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, wt->tex_color_buffer, 0);

        GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
                _printf("Framebuffer status: %u\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void window_set_wterm_dimensions(struct wterm *wt)
{
        struct window *w = wt->window;
        wt->width = w->direction == WINDOW_VERTICAL ? w->x1 - w->x0 : (w->x1 - w->x0) / w->nterm;
        wt->height = w->direction == WINDOW_VERTICAL ? (w->y1 - w->y0) / w->nterm : w->y1 - w->y0;
}

static void wterm_change_font_size(struct wterm *wt, int delta)
{
        int cw, ch;
        font_get_dimensions(&k->m, &cw, &ch, wt->font_size);

        wt->font_size += delta;
        wt->cw = cw;
        wt->ch = ch;

        term_resize(wt->term, wt->width / cw,
                    wt->height / (ch + LINE_SPACING));
}

void window_change_font_size(struct wterm *wt, int delta)
{
        wterm_change_font_size(wt, delta);
        platform_inform_subprocess_of_resize(wt->subprocess,
                                             wt->term->g->col,
                                             wt->term->g->row);
}

void window_spawn(struct window *w)
{
        struct wterm *wt = calloc(1, sizeof *wt); /* TODO: xmalloc */

        *wt = (struct wterm){
                .term = term_new(),
                .font_size = 12, /* TODO: Configure default font size */
                .window = w,
        };

        append_wterm(w, wt);

        window_set_wterm_dimensions(wt);
        wterm_change_font_size(wt, 0);
        init_gl_resources(wt, wt->width, wt->height);

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
                for (struct wterm *wt = w->wterm; wt; wt = wt->next) {
                        struct term *t = wt->term;

                        window_set_wterm_dimensions(wt);
                        term_resize(t, wt->width / wt->cw, wt->height / (wt->ch + LINE_SPACING));
                        platform_inform_subprocess_of_resize(wt->subprocess, t->g->col, t->g->row);

                        /* TODO: Move this somewhere sensible. */
                        glBindTexture(GL_TEXTURE_2D, wt->tex_color_buffer);
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, wt->width, wt->height, 0,
                                     GL_RGB, GL_UNSIGNED_BYTE, NULL);
                }
        }
}

void window_render(struct window *w, struct font_renderer *r)
{
        for (struct wterm *wt = w->wterm; wt; wt = wt->next)
                render_wterm(r, wt);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(0, 1, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0, 0, r->width, r->height);

        int i = 0;
        for (struct wterm *wt = w->wterm; wt; wt = wt->next, i++) {
                if (w->direction == WINDOW_HORIZONTAL) {
                        render_quad(r,
                                    w->x0 + i * wt->width + i,
                                    w->y0,
                                    w->x0 + (i + 1) * wt->width + i,
                                    w->y1,
                                    wt->tex_color_buffer);
                } else {
                        render_quad(r,
                                    w->x0,
                                    w->y0 + i * wt->height + i,
                                    w->x1,
                                    w->y0 + (i + 1) * wt->height + i,
                                    wt->tex_color_buffer);
                }
        }
}
