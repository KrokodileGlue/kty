#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <pthread.h>

extern char **environ;

struct subprocess {
        int master;

        /*
         * Each shell forks off into a different process and we spawn
         * a thread which just waits for it to say something. Then it
         * repeats whatever the shell said to the terminal by calling
         * the `write` callback.
         */
        pthread_t thread;

        /*
         * `write` is a callback which is called whenever the shell
         * wants to say something to the terminal. `fluff` is the
         * terminal being written to (well, the wterm).
         */
        void *fluff;
        int (*write)(void *, char *, int);
        void (*end)(void *);
};

static void *read_shell(void *arg)
{
        struct subprocess *p = (struct subprocess *)arg;

        char buf[BUFSIZ];

        while (1) {
                int ret = read(p->master, buf, sizeof buf);
                if (ret <= 0) break;
                int written = p->write(p->fluff, buf, ret);
                if (written < 0) break;
        }

        p->end(p->fluff);

        return NULL;
}

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

        setenv("TERM", "xterm-256color", 1);

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

int platform_write(struct subprocess *p, const char *buf, int n)
{
        return write(p->master, buf, n);
}

struct subprocess *platform_spawn_shell(void *fluff,
                                        int (*callback)(void *, char *, int),
                                        void (*end)(void *))
{
        struct subprocess *subprocess = malloc(sizeof *subprocess);
        subprocess->master = spawn_shell(getenv("SHELL"));
        subprocess->write = callback;
        subprocess->end = end;
        subprocess->fluff = fluff;
        pthread_create(&subprocess->thread, NULL, read_shell, subprocess);
        return subprocess;
}

void platform_close_shell(struct subprocess *p)
{
        /* TODO */
        (void)p;
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
