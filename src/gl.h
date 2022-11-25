#ifndef GL_H
#define GL_H

int print_gl_error_log(GLuint object);
GLint create_shader(const GLchar *source, GLenum type);
int bind_uniform_to_program(GLuint program, const char *name);
int bind_attribute_to_program(GLuint program, const char *name);

#endif
