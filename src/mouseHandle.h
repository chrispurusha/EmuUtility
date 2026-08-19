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

#ifndef __MOUSE_HANDLE_H__
#define __MOUSE_HANDLE_H__

#include "types.h"
#include "clickRegion.h"

#ifdef __cplusplus
extern "C" {
#endif

void handle_mouse_button(void * win, int button, int action, int mods, double x, double y);

// Arms the dial-drag state (gDialDrag et al, private to mouseHandle.c) on press;
// registered by emuGraphics.cpp's render_dial_knob() as the dial's click region.
// Release is handled separately, earlier in handle_mouse_button() — see there.
void dial_press_click_handler(tCoord coord, eClickPhase phase, void * userData);
void handle_cursor_pos(void * win, double x, double y);
void handle_key(void * win, int key, int scancode, int action, int mods);

// Ends a dial drag whose mouse release never arrived. Call once per frame — it no-ops unless the
// drag flag is genuinely stuck. See its definition for why a stuck flag is worse than a stuck cursor.
void recover_lost_dial_drag(void * win);
void handle_scroll(void * win, double dx, double dy);

// Supplied for SynthLib's contextMenu.c to link against — current mouse
// position in the same logical (render-scaled) space menu coords are opened
// in. See contextMenu.h.
void get_global_gui_scaled_mouse_coord(tCoord * coord);

#ifdef __cplusplus
}
#endif

#endif // __MOUSE_HANDLE_H__
