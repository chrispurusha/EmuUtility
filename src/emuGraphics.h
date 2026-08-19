/*
 * The EmuUtility application.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __EMU_GRAPHICS_H__
#define __EMU_GRAPHICS_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LCD_BORDER    6.0    // green surround around the LCD texture

// Render the 240×64 LCD bitmap, scaled to fit the given rectangle.
void render_lcd();

// Large rotary dial (right of LCD).
void render_dial_knob(void);
void dial_nudge(int delta);   // +ve = clockwise / increment
tRectangle emu_dial_rect(void);

// Rotary drag mode: nudge the dial by a change in mouse angle (degrees,
// signed, shortest-path) around the dial centre — matches
// calculate_mouse_angle()'s convention. Rotates at the same rate as the
// mouse without pinning the indicator to the raw mouse angle.
void dial_nudge_by_angle(double deltaDegrees);

// Render the button panel below the LCD.
void render_button_panel();

// Initialise the LCD OpenGL texture (call once after GL context is ready).
void init_lcd_texture(void);

// Total panel height needed for buttons below the LCD.
double button_panel_height(double areaWidth);

// ── LCD soft keys ─────────────────────────────────────────────────────────────
// The six boxes the sampler draws along the bottom of its own display are its soft keys. They are
// clickable, and each raises the same event as the F-key directly beneath it — which is why the LCD
// is positioned from the F-key geometry rather than independently (see emuGraphics.c).

#define EMU_SOFTKEY_COUNT    6

// Where the LCD bitmap and each soft-key box are drawn, in render coordinates.
tRectangle emu_lcd_rect(void);
tRectangle emu_softkey_rect(int index);

// The display ABOVE the soft-key row — the part with no key under it. Clicking there issues Exit,
// which is the front panel's own way of backing out of wherever you are.
tRectangle emu_lcd_body_rect(void);

// Which front-panel button soft key `index` corresponds to (0 -> F1 ... 5 -> F6).
tButtonKey emu_softkey_button(int index);

// Raise a front-panel button press or release: lights the on-screen button, sends the PEPTALK
// event, and asks for a fresh full LCD dump. Shared by the button click handler, the soft-key
// boxes, and the backdoor's BUTTON command.
void emu_button_press(tButtonKey key, bool pressed);

// ── Testing helpers (backdoor command channel, graphics.c) ───────────────────

// Walk every button in layout order; NULL once past the end.
const tButton * emu_button_at_index(int index);

// Resolve a button by its on-screen label (case-insensitive, exact) or by its raw PEPTALK key code.
bool emu_button_lookup(const char * name, tButtonKey * keyOut);

#ifdef __cplusplus
}
#endif

#endif // __EMU_GRAPHICS_H__
