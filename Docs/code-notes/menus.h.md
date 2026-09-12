# menus.h notes

The longer comments from `menus.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

App-specific menu-building helpers (open_X_context_menu()) belong here once
EmuUtility actually raises a context menu — see G2-Edit's menus.c for the
pattern: build a static tMenuItem[] table ending in a {NULL, ...} sentinel
(SynthLib walks items until it finds label == NULL, there's no count), then
call open_context_menu(). Nothing does that yet, so there's nothing to
declare here beyond what contextMenu.h already provides.
