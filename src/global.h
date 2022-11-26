#ifndef GLOBAL_H
#define GLOBAL_H

struct global {
        struct frame **frame;
        int nframe;
        struct color color256[256];
};

#endif
