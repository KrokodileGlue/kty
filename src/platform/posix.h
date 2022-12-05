#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern char **environ;

struct subprocess {
        int master;
};

static int spawn_shell(const char *shell)
{
        int master = posix_openpt(O_RDWR | O_NOCTTY);

        if (master == -1) {
                perror("posix_openpt");
                return -1;
        }

        if (grantpt(master) == -1) {
                perror("grantpt");
                return -1;
        }

        if (unlockpt(master) == -1) {
                perror("unlockpt");
                return -1;
        }

        const char *slave_name = ptsname(master);

        if (!slave_name) {
                perror("ptsname");
                return -1;
        }

        int slave = open(slave_name, O_RDWR | O_NOCTTY);

        if (slave == -1) {
                perror("open");
                return -1;
        }

        if (fork()) {
                close(slave);
        } else {
                close(master);
                setsid();

                /*
                 * Inform the shell that we're the controlling
                 * terminal.
                 */
                if (ioctl(slave, TIOCSCTTY, NULL) == -1) {
                        perror("ioctl");
                        return -1;
                }

                dup2(slave, 0);
                dup2(slave, 1);
                dup2(slave, 2);

                close(slave);

                execle(shell, shell, NULL, environ);

                /* Unreachable; `execle` never returns. */
        }

        return master;
}

struct subprocess *platform_spawn_shell(void)
{
        struct subprocess *subprocess = malloc(sizeof *subprocess);
        subprocess->master = spawn_shell(getenv("SHELL"));
        return subprocess;
}

void platform_close_shell(struct subprocess *p)
{
        /* TODO */
        (void)p;
}

int platform_read(struct subprocess *p, char *buf, int n)
{
        return read(p->master, buf, n);
}

int platform_write(struct subprocess *p, char *buf, int n)
{
        return write(p->master, buf, n);
}

FILE *platform_open_config(void)
{
        return NULL;
}

void platform_inform_subprocess_of_resize(struct subprocess *p, int col, int row)
{
        struct winsize ws = {
                .ws_col = col,
                .ws_row = row,
        };

        if (ioctl(p->master, TIOCSWINSZ, &ws) == -1)
                perror("ioctl");
}
