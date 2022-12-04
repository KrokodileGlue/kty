# TODO

+ Add scrollback
+ Harfbuzz - Support languages like Arabic / ligatures / ZWJ sequences
+ Implement zoom

+ Add tabs and windows
+ Closing vim doesn't restore the previous stuff (scrolling the history back?)
+ Clean up frame state properly
+ Add sixel/iTerm2 graphic support
+ Add minimize/maximize/close bar (override system style)
+ Add mouse handling
+ Add scrollbar
+ Fix resizing bugs
+ Handle overflowing spritemaps
+ Use a different spritemap for each font size
+ Fix arrow keys and home/end (or at least alt+home/alt+end)
+ Parse str escape sequences (used to set the application title)
+ Handle bitmap fonts (restrict zoom)
+ Ask fontconfig for font stuff
+ Add command line arguments and configuration file
+ Refactor OpenGL code
+ Switch to Vulkan
+ Avoid using system wcwidth due to platform inconsistencies
+ Support Windows (conio, PowerShell)
+ Use dynamic allocation for more things
+ Text selection
+ Primary/clipboard handling (cross platform?)
+ Allow overriding the font for specific code points
+ Custom line drawing functionality
+ Add a client/server model to reduce startup time and system load
+ Add LuaJIT scripting
+ Unit testing
+ Use hash table or LRU for glyph lookup (ASCII can be O(1))
+ Write tiling frame manager
+ Handle DPI changes
+ Handle alternate charsets
+ Add custom box drawing functionality
+ Add url matching with PCRE
+ Add missing graphic modes (e.g. double underscore)

Script ideas:

+ File transfer
+ Screenshot tool
