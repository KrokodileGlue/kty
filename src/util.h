#pragma once

/* No one will ever need more than 16 fonts. */
#define MAX_FONTS 16
#define WINDOW_WIDTH 600
#define WINDOW_HEIGHT 700

/* TODO: This is one of the most concerning. */
#define NUM_CELL 32000

/* TODO: Make this dynamic. */
#define MAX_SPRITES_IN_FONT 1000
#define LINE_SPACING 4

/* How long could an escape sequence possibly be. */
#define ESC_ARG_SIZE 512

#define VT_IDENTITY "\033[?6c"

struct color {
        float r, g, b;
};

void _printf(const char *func, const char *fmt, ...);

#ifdef DEBUG
#define _printf(...) _printf(__func__, __VA_ARGS__)
#else
#define _printf(...) do {} while (0)
#endif
