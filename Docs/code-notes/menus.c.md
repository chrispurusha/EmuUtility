# menus.c notes

The longer comments from `menus.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

The generic nested context menu (open_context_menu(), handle_context_menu_click(),
update_context_menu_hover(), render_context_menu(), gContextMenu) now lives
in SynthLib (see contextMenu.c/h) — EmuUtility used to carry its own
single-level flat-grid duplicate of the same names here, which started
colliding with SynthLib's richer nested-flyout types once SynthLib picked
up the menu system. This file is the reserved home for EmuUtility-specific
menu-building helpers once something actually opens a menu; see menus.h.
