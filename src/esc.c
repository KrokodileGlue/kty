#include "esc.h"

#include <limits.h>  /* LONG_MAX, LONG_MIN */
#include <stdint.h>  /* uint32_t */
#include <stdio.h>   /* NULL */
#include <stdlib.h>  /* strtol */
#include <string.h>  /* memset */
#include <unistd.h>

#include "frame.h"   /* frame, frame::(anonymous), cursor, ESC_ALTCHARSET */
#include "render.h"  /* font_renderer */
#include "t.h"       /* tclearregion, tmoveto, tscrolldown, tsetscroll */
#include "util.h"    /* _printf, ESC_ARG_SIZE */

void csiparse(struct frame *f)
{
        f->csi.narg = 0;
        f->csi.buf[f->csi.len] = 0;

        char *p = f->csi.buf;

        if (*p == '?') {
                f->csi.priv = 1;
                p++;
        }

        while (p < f->csi.buf + f->csi.len) {
                char *np = NULL;
                long v = strtol(p, &np, 10);
                if (np == p) break;
                if (v == LONG_MAX || v == LONG_MIN) v = -1;
                f->csi.arg[f->csi.narg++] = v;
                p = np;

                /*
                 * TODO: Investigate why some applications seem to use : instad
                 * of ; for some sequences.
                 */
                if ((*p != ';' && *p != ':') || f->csi.narg == ESC_ARG_SIZE)
                        break;
                p++;
        }

        f->csi.mode[0] = *p++;
        f->csi.mode[1] = (p < f->csi.buf + f->csi.len) ? *p : 0;

#ifdef DEBUG
        static char buf[2048] = { 0 };
        snprintf(buf, sizeof buf, "%.*s%s -> ", f->csi.len, f->csi.buf, f->csi.priv ? " (priv)" : "");
        for (int i = 0; i < f->csi.narg; i++)
                snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "%ld ", f->csi.arg[i]);
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), "mode %c mode %d\n", f->csi.mode[0], f->csi.mode[1]);
        _printf("%s", buf);
#endif
}

void resetcsi(struct frame *f)
{
        memset(&f->csi, 0, sizeof f->csi);
}

void resetesc(struct frame *f)
{
        f->esc = 0;
}
