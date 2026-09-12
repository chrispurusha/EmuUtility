# main.c notes

The longer comments from `main.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. in `main()`

BEFORE init_graphics(), and the order is load-bearing. The window is built differently for
each render backend — OpenGL needs a GL context created alongside it, Metal needs none — so
synthlib_window_create() reads the saved choice before it makes the window. Without this the
read returns the default and the setting is SILENTLY IGNORED: no error, nothing in the log,
both values simply give OpenGL. prefs_init() also runs from setup_main_menu() below, where it
always did; it clears and re-reads, so calling it twice is harmless.
