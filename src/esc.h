#pragma once

#include <stdbool.h>

#include "util.h"    /* ESC_ARG_SIZE */

struct csi {
        /* TODO: Don't hard code this buffer size. */
        char buf[2048];
        unsigned len;

        long arg[ESC_ARG_SIZE];
        int narg;

        bool priv;

        char mode[2];
};

struct stresc {
        /* TODO: Nor this one. */
        char buf[2048];
        unsigned len;

        char *arg[ESC_ARG_SIZE];
        int narg;

        char type;
};

void csiparse(struct csi *csi);
void strescparse(struct stresc *stresc);
