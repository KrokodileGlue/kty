#include "esc.h"

#include <limits.h>  /* LONG_MAX, LONG_MIN */
#include <stdlib.h>  /* strtol */

#include <stdio.h>   /* snprintf */
#include <string.h>  /* strlen */

void csiparse(struct csi *csi)
{
        csi->narg = 0;
        csi->buf[csi->len] = 0;

        char *p = csi->buf;

        if (*p == '?') {
                csi->priv = 1;
                p++;
        }

        while (p < csi->buf + csi->len) {
                char *np = 0;
                long v = strtol(p, &np, 10);
                if (np == p) break;
                if (v == LONG_MAX || v == LONG_MIN) v = -1;
                csi->arg[csi->narg++] = v;
                p = np;

                /*
                 * TODO: Investigate why some applications seem to use
                 * : instead of ; for some sequences.
                 */
                if ((*p != ';' && *p != ':') || csi->narg == ESC_ARG_SIZE)
                        break;
                p++;
        }

        csi->mode[0] = *p++;
        csi->mode[1] = (p < csi->buf + csi->len) ? *p : 0;

#ifdef DEBUG
        static char buf[2048] = { 0 };
        snprintf(buf, sizeof buf, "%.*s%s -> ", csi->len, csi->buf, csi->priv ? " (priv)" : "");
        for (int i = 0; i < csi->narg; i++)
                snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%ld ", csi->arg[i]);
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "mode %c mode %d\n", csi->mode[0], csi->mode[1]);
        _printf("%s", buf);
#endif
}

void resetcsi(struct csi *csi)
{
        *csi = (struct csi){ 0 };
}

void strescparse(struct stresc *stresc)
{
        stresc->narg = 0;
        stresc->buf[stresc->len] = 0;

        char *p = stresc->buf;

        if (!*p) return;

        while (stresc->narg < ESC_ARG_SIZE) {
                stresc->arg[stresc->narg++] = p;
                int c;
                while ((c = *p) && c != ';') ++p;
                if (!c) return;
                *p++ = 0;
        }
}
