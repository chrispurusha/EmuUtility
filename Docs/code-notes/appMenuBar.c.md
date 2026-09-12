# appMenuBar.c notes

The longer comments from `appMenuBar.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `rescan_devices()`

Just two menus, each with a handful of items — small enough that (unlike
G2-Edit's much larger File/Settings/Backup/Restore/Controls/Tools/View set)
there's no separate menuActions.c; the action bodies live directly in each
open_X_menu() below.

## 2. in `open_controls_menu()`

Labels are fixed strings with a checkmark prefix baked in (tMenuItem has no separate
"checked" flag) — point each entry's label at the checked or unchecked variant depending
on the current dial mode, rather than mutating the string in place. Same approach as
G2-Edit's open_controls_menu (src/appMenuBar.c there).

## 3. `action_about()`

NO EXPERIMENTAL MENU ANY MORE (2026-09-09). It held one thing - the OpenGL/Metal choice - and
macOS is Metal only now, so the switch went and the greyed "Renderer: <name>" readout beneath it
was the only item left. A whole top-level menu for one line of information is not worth the width,
and the About box prints the renderer anyway (see synthlib_about_text()).

If something genuinely experimental turns up again, G2-Edit still has the pattern to copy.
