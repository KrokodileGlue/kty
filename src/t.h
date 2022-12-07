#pragma once

struct term;

int twrite(struct term *t, const char *buf, int buflen);
void tresize(struct term *t, int col, int row);
