#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>
#include <wchar.h>
#include <ctype.h>

#include <unistd.h>
#include <pthread.h> /* TODO: Windows support. */
#include <fcntl.h>
#include <sys/ioctl.h>

#include <GL/glew.h>
#include <GL/gl.h>

#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>

#include "gl.h"
#include "util.h"
#include "frame.h"
#include "t.h"
#include "esc.h"
#include "utf8.h"
#include "font.h"
#include "render.h"
#include "global.h"

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
