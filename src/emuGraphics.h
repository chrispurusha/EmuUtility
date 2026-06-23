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

#define LCD_BORDER    12.0   // green surround around the LCD texture

// Render the 240×64 LCD bitmap, scaled to fit the given rectangle.
void render_lcd(tRectangle area);

// Large rotary dial (right of LCD).
void render_dial_knob(void);
bool dial_hit_test(tCoord coord);
void dial_nudge(int delta);   // +ve = clockwise / increment

// Render the button panel below the LCD.
void render_button_panel(tRectangle area);

// Initialise the LCD OpenGL texture (call once after GL context is ready).
void init_lcd_texture(void);

// Hit-test a screen coordinate against the button layout.
// Returns a pointer to the matched tButton, or NULL.
tButton * button_at(tCoord coord);

// Total panel height needed for buttons below the LCD.
double button_panel_height(double areaWidth);

#ifdef __cplusplus
}
#endif

#endif // __EMU_GRAPHICS_H__
