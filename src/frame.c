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

        return f;
}

void frame_title(struct frame *f, const char *title)
{
        free(f->title);
        f->title = malloc(strlen(title) + 1); /* TODO: xmalloc */
        strcpy(f->title, title);
        global_notify_title_change(f);
}
