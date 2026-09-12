# emuGraphics.h notes

The longer comments from `emuGraphics.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `dial_nudge_by_angle()`

Rotary drag mode: nudge the dial by a change in mouse angle (degrees,
signed, shortest-path) around the dial centre — matches
calculate_mouse_angle()'s convention. Rotates at the same rate as the
mouse without pinning the indicator to the raw mouse angle.

## 2. `EMU_SOFTKEY_COUNT`

── LCD soft keys ─────────────────────────────────────────────────────────────
The six boxes the sampler draws along the bottom of its own display are its soft keys. They are
clickable, and each raises the same event as the F-key directly beneath it — which is why the LCD
is positioned from the F-key geometry rather than independently (see emuGraphics.c).
