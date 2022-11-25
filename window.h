#ifndef WINDOW_H
#define WINDOW_H

void *read_shell(void *arg);
void character_callback(GLFWwindow *window, uint32_t c);
void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);
void window_size_callback(GLFWwindow *window, int width, int height);

#endif
