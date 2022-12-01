#include "gl.h"

#include <GL/glew.h>  /* GLuint, GLint, GL_FALSE, GL_INFO_LOG_LENGTH */
#include <stdio.h>    /* NULL */
#include <stdlib.h>   /* exit, free, malloc */

#include "util.h"     /* _printf */

int print_gl_error_log(GLuint object)
{
        GLint log_length;

        if (glIsShader(object)) {
                glGetShaderiv(object, GL_INFO_LOG_LENGTH, &log_length);
        } else if (glIsProgram(object)) {
                glGetProgramiv(object, GL_INFO_LOG_LENGTH, &log_length);
        } else {
                return 1;
        }

        char *log = malloc(log_length);
        if (!log) return 1;

        if (glIsShader(object)) {
                glGetShaderInfoLog(object, log_length, NULL, log);
        } else if (glIsProgram(object)) {
                glGetProgramInfoLog(object, log_length, NULL, log);
        }

        _printf("OpenGL error: %s", log);
        free(log);

        return 0;
}

GLint create_shader(const GLchar *source, GLenum type)
{
        GLuint res = glCreateShader(type);

        if (!res) {
                _printf("Can't create shader\n");
        }

        glShaderSource(res, 1, &source, NULL);

        glCompileShader(res);
        GLint compile_ok = GL_FALSE;
        glGetShaderiv(res, GL_COMPILE_STATUS, &compile_ok);
        if (compile_ok == GL_FALSE) {
                _printf("Couldn't compile shader\n");
                print_gl_error_log(res);
                glDeleteShader(res);
                return 0;
        }

        _printf("res: %d\n", res);
        return res;
}

int bind_uniform_to_program(GLuint program, const char *name)
{
        int coord = glGetUniformLocation(program, name);

        if (coord < 0) {
                _printf("Couldn't bind uniform %s\n", name);
                exit(1);
        }

        return coord;
}

int bind_attribute_to_program(GLuint program, const char *name)
{
        int coord = glGetAttribLocation(program, name);

        if (coord < 0) {
                _printf("Couldn't bind attribute %s\n", name);
                exit(1);
        }

        return coord;
}
