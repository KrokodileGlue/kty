#include "esc.h"

#include <limits.h>  /* LONG_MAX, LONG_MIN */
#include <stdint.h>  /* uint32_t */
#include <stdio.h>   /* NULL */
#include <stdlib.h>  /* strtol */
#include <string.h>  /* memset */
#include <unistd.h>

#include "term.h"    /* term, term::(anonymous), cursor, ESC_ALTCHARSET */
#include "render.h"  /* font_renderer */
#include "t.h"       /* tclearregion, tmoveto, tscrolldown, tsetscroll */
#include "util.h"    /* _printf, ESC_ARG_SIZE */

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
                char *np = NULL;
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
        memset(csi, 0, sizeof *csi);
}
