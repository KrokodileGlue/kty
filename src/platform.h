#pragma once

#include <stdio.h>

struct subprocess;

struct subprocess *platform_spawn_shell(void *fluff,
                                        int (*callback)(void *, char *, int),
                                        void (*end)(void *));
int platform_write(struct subprocess *p, const char *buf, int n);
void platform_close_shell(void);
void platform_inform_subprocess_of_resize(struct subprocess *p, int col, int row);
FILE *platform_open_config(void);
