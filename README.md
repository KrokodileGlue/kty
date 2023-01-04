# kty

This work is licensed under the terms of the GPLv2.

# TODO

+ Finish windows
+ Add scrollback
+ Harfbuzz - Support languages like Arabic / ligatures
+ Interpret ZWJ sequences
+ Add tabs
+ Abstract away graphics

  Each wterm should hold onto a `struct canvas` and the renderer uses
  the rendering technology agonistic "graphics" api to render
  primitives like textured quads. The renderer checks line->dirty and
  then uses font.h to convert each line into rendering primitives
  (glyphs) which can be passed to the graphics api.

  The OpenGL graphics API in graphics/ (which should use only platform
  agnostic code) will build a vertex buffer for the entire screen,
  upload any sprite maps that have changed, and render the entire
  screen in one go.

+ Abstract away display

  This will make it easy to switch from glfw to other displays (like a
  terminal; that would make kty a multiplexer).

+ Add sixel/iTerm2 graphic support
+ Add minimize/maximize/close bar (override system style)
+ Add mouse handling
+ Add scrollbar
+ Handle overflowing spritemaps
+ Fix arrow keys and home/end (or at least alt+home/alt+end)
+ Parse str escape sequences (used to set the application title)
+ Ask fontconfig for font stuff
+ Add command line arguments and libconfig
+ Avoid using system wcwidth due to platform inconsistencies
+ Support Windows (conio, PowerShell)
+ Use dynamic allocation for more things
+ Text selection
+ Primary/clipboard handling (cross platform?)
+ Allow overriding the font for specific code points
+ Custom line drawing functionality (bezier curves?)
+ Add a client/server model to reduce startup time and system load
+ Add LuaJIT scripting
+ Unit testing
+ Use hash table or LRU for glyph lookup (ASCII can be O(1))
+ Handle alternate charsets
+ Add custom box drawing functionality
+ Add url matching with PCRE
+ Add missing graphic modes (e.g. double underscore, dim, blinking cursor)
+ Dim emoji to match font brightness
+ Fix race condition causing glfwPollEvents to hang

+ Rewrite font / renderer

+ Use a different spritemap for each font size
+ Handle bitmap fonts (restrict zoom)

+ Do profiling after rendering rewrite

Script ideas:

+ File transfer
+ Screenshot tool
