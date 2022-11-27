#define _XOPEN_SOURCE 600

#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h>
#include <wchar.h>
#include <ctype.h>

#include <unistd.h>
#include <pthread.h> /* TODO: Windows support. */
#include <fcntl.h>
#include <sys/ioctl.h>

#include <GL/glew.h>
#include <GL/gl.h>

#include <GLFW/glfw3.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <freetype/tttables.h>

#include "gl.h"
#include "util.h"
#include "frame.h"
#include "t.h"
#include "esc.h"
#include "utf8.h"
#include "font.h"
#include "render.h"
#include "global.h"
#include "window.h"
