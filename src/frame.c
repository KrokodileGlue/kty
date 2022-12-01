#define _XOPEN_SOURCE 600

#include "frame.h"

#include <fcntl.h>                     /* open, O_NOCTTY, O_RDWR */
#include <stdio.h>                     /* perror */
#include <stdlib.h>                    /* NULL, calloc, free, grantpt */
#include <string.h>                    /* strcpy, strlen */
#include <sys/ioctl.h>                 /* ioctl, TIOCSCTTY */
#include <unistd.h>                    /* close, dup2, execle, fork, setsid */

#include "global.h"                    /* global_notify_title_change */

extern struct global *k;

struct frame *frame_new(char **env, struct font_renderer *r)
{
        /* Set up the PTY. */
        int master = posix_openpt(O_RDWR | O_NOCTTY);

        if (master == -1) {
                perror("posix_openpt");
                return NULL;
        }

        if (grantpt(master) == -1) {
                perror("grantpt");
                return NULL;
        }

        if (unlockpt(master) == -1) {
                perror("unlockpt");
                return NULL;
        }

        const char *slave_name = ptsname(master);

        if (!slave_name) {
                perror("ptsname");
                return NULL;
        }

        int slave = open(slave_name, O_RDWR | O_NOCTTY);

        if (slave == -1) {
                perror("open");
                return NULL;
        }

        if (fork()) {
                close(slave);
        } else {
                close(master);

                setsid();
                if (ioctl(slave, TIOCSCTTY, NULL) == -1) {
                        perror("ioctl");
                        return NULL;
                }

                dup2(slave, 0);
                dup2(slave, 1);
                dup2(slave, 2);
                close(slave);

                execle("/bin/bash", "/bin/bash", NULL, env);
        }

        /* The shell is running, now set up the window/graphics. */
        struct frame *f = calloc(1, sizeof *f);

        if (!f) return NULL;

        f->master = master;
        f->mode = MODE_CURSOR_VISIBLE;
        f->c.fg = f->c.bg = -1;
        f->k = k;
        f->font = r;

        glGenFramebuffers(1, &f->framebuffer);

        glBindFramebuffer(GL_FRAMEBUFFER, f->framebuffer);

        /* Set up the texture */
        glGenTextures(1, &f->tex_color_buffer);
        glBindTexture(GL_TEXTURE_2D, f->tex_color_buffer);

        /* TODO: Don't use the window width and height here. */
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, r->width, r->height, 0,
                GL_RGB, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, f->tex_color_buffer, 0);

        GLuint status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
                _printf("Framebuffer status: %u\n", status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return f;
}

void frame_title(struct frame *f, const char *title)
{
        free(f->title);
        f->title = malloc(strlen(title) + 1); /* TODO: xmalloc */
        strcpy(f->title, title);
        global_notify_title_change(f);
}
