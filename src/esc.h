#pragma once

#include <stdbool.h>

#include "util.h"    /* _printf, ESC_ARG_SIZE */

struct csi {
        /* TODO: Don't hard code this buffer size. */
        char buf[2048];
        long arg[ESC_ARG_SIZE];
        unsigned len;
        int narg;
        bool priv;
        int mode[2];
};

struct stresc {
        /* TODO: Nor this one. */
        char buf[2048];
        unsigned len;
        unsigned char type;
        char *arg[ESC_ARG_SIZE];
        int narg;
};


void csiparse(struct csi *csi);
void resetcsi(struct csi *csi);
void strescparse(struct stresc *stresc);
