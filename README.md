# TODO

+ Fix arrow keys disappearing things in bash
+ Implement colors (foreground, background, swap, 256 color)
+ Implement multiple cursor styles
+ Interpret ZWJ sequences

+ Clean up frame state properly
+ Add sixel/iTerm2 graphic support
+ Add minimize/maximize/close bar (override system style)
+ Add scrollback / mouse handling
+ Add scrollbar
+ Fix resizing bugs
+ Don't crash when applications try to write outside the window bounds
+ When a wcwidth==2 character is printed clear the character after it
+ Handle overflowing spritemaps
+ Use a different spritemap for each font size
+ Fix SSH
+ Fix arrow keys generally (they don't work in `less`, for example)
+ Add missing keys (fn, alt, pgup)
+ Parse str escape sequences (used to set the application title)
+ Handle bitmap fonts (restrict zoom)
+ Implement zoom (basically just a resize, but interacts with windows)
+ Add tabs and windows
+ Ask fontconfig for font stuff
+ Add command line arguments and configuration file
+ Add debug mode for visualizing operations like tscrollup
+ Refactor OpenGL code
+ Switch to Vulkan
+ Avoid using system wcwidth due to platform inconsistencies
+ Support Windows
+ Use dynamic allocation for more things
+ Text selection
+ Primary/clipboard handling (cross platform?)
+ Allow overriding the font for specific code points
+ Custom line drawing functionality
+ Add a client/server model to reduce startup time and system load
+ Avoid redrawing the screen when there hasn't been any change (vbo_dirty)
+ Add LuaJIT scripting
+ Unit testing
+ Use hash table for glyph lookup (ASCII can be O(1))

Script ideas:

+ File transfer
+ Screenshot tool
