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

#ifdef __cplusplus
extern "C" {
#endif

// Disable warnings from external library headers etc.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"

#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>

#pragma clang diagnostic pop
#include "defs.h"
#include "synthlibDefs.h"
#include "types.h"
#include "globalVars.h"
#include "emuGraphics.h"
#include "peptalk.h"
#include "menus.h"
#include "utils.h"
#include "utilsGraphics.h"
#include "mouseHandle.h"

// Convert GLFW window-space (x,y) to logical canvas coordinates.
static tCoord window_to_logical(void * win, double x, double y) {
    int    winW  = 0;
    int    winH  = 0;

    glfwGetWindowSize(win, &winW, &winH);

    tCoord coord = {
        .x = (winW > 0) ? (x / winW) * (get_render_width() / gGlobalGuiScale) : x,
        .y = (winH > 0) ? (y / winH) * (get_render_height() / gGlobalGuiScale) : y,
    };
    return coord;
}

// Scale a window-space delta to logical-space delta
static double delta_to_logical(void * win, double winDelta, bool isX) {
    int winW = 0;
    int winH = 0;

    glfwGetWindowSize(win, &winW, &winH);

    if (isX) {
        return (winW > 0) ? (winDelta / winW) * (get_render_width() / gGlobalGuiScale) : winDelta;
    } else {
        return (winH > 0) ? (winDelta / winH) * (get_render_height() / gGlobalGuiScale) : winDelta;
    }
}

#define GLFW_CURSOR             0x00033001
#define GLFW_CURSOR_NORMAL      0x00034001
#define GLFW_CURSOR_DISABLED    0x00034003

static bool   gDialDrag      = false;
static double gDialPrevX     = 0.0; // previous cursor x — used for horizontal delta
static double gDialPrevY     = 0.0; // previous cursor y — used for vertical delta
static double gDialAccum     = 0.0;
static double gDialPrevAngle = 0.0; // previous mouse angle around dial centre — used for rotary mode
static int    gDialSkipCount = 0;   // skip first N cursor_pos events after CURSOR_DISABLED — covers stale events + transition event

void handle_mouse_button(void * win, int button, int action, int mods, double x, double y) {
    (void)mods;

    if (button != 0) {   // left button only
        return;
    }
    tCoord coord   = window_to_logical(win, x, y);

    bool   pressed = (action == 1);      // GLFW_PRESS == 1

    LOG_DEBUG("mouse %s win(%.0f,%.0f) logical(%.0f,%.0f)\n",
              pressed ? "press" : "release", x, y, coord.x, coord.y);

    // A release that ends an active dial drag takes priority over other click
    // routing below (context menu, buttons) — the drag consumes the release
    // regardless of what's now under the cursor. Matches G2-Edit/mouseHandle.c.
    //
    // No explicit glfwSetCursorPos() here (there used to be one, restoring
    // gDialStartX/Y) — GLFW's cocoa backend already restores the cursor to
    // wherever it was when CURSOR_DISABLED was entered as soon as we switch
    // back to NORMAL (see updateCursorMode() in cocoa_window.m). An extra
    // explicit warp on top of that was redundant, and — per the equivalent
    // fix in SynthEdit's mouseHandle.c — two independent warps in a row can
    // land a pixel or two off from each other. Harmless there in practice
    // since there's only the one dial on screen, but no reason to keep it.
    if (!pressed && gDialDrag) {
        gDialDrag       = false;
        gDialSkipCount  = 0;

        if (gDialMode != eDialModeRotary) {
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        gDialDragActive = false;
        gNeedLcdFull    = true; // final full sync supersedes the throttled polling done during the drag
        gReDraw         = true;
        return;
    }

    if (close_context_menu_if_outside(coord)) {
        return;
    }

    if (handle_context_menu_click(coord)) {
        return;
    }

    if (pressed && dial_hit_test(coord)) {
        gDialDrag       = true;
        gDialAccum      = 0.0;
        gDialPrevX      = x;
        gDialPrevY      = y;
        gDialDragActive = true;
        gLastLcdPollMs  = get_time_ms(); // first poll can fire as soon as the interval elapses

        if (gDialMode == eDialModeRotary) {
            gDialPrevAngle = calculate_mouse_angle(coord, emu_dial_rect());
        } else {
            gDialSkipCount = 3;
            glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        }
        return;
    }
    tButton * btn = button_at(coord);

    if (btn != NULL) {
        LOG_DEBUG("hit button key=%d label=%s\n", (int)btn->key, btn->label);
        btn->pressed  = pressed;
        peptalk_send_button_event(btn->key, pressed);
        // Always request a full dump after any button press. Deltas are only
        // safe when we know the hardware's base state hasn't drifted — button
        // presses can change the display in ways that compound delta errors.
        gNeedLcdFull  = true;
        gNeedLcdDelta = false;
        gReDraw       = true;
    } else {
        LOG_DEBUG("no button at logical(%.0f,%.0f)\n", coord.x, coord.y);
    }
}

void handle_cursor_pos(void * win, double x, double y) {
    if (!gDialDrag) {
        return;
    }

    if (gDialMode == eDialModeRotary) {
        // Relative tracking — rotating the mouse around the dial centre turns
        // the encoder at the same angular rate, without pinning the indicator
        // to the raw mouse angle (there's no fixed "12 o'clock = value X" on a
        // real endless encoder, so snapping to the click position would jump).
        tCoord coord = window_to_logical(win, x, y);
        double angle = calculate_mouse_angle(coord, emu_dial_rect());
        double delta = angle - gDialPrevAngle;

        // Shortest signed rotation, handling the 0°/360° wrap
        if (delta > 180.0) {
            delta -= 360.0;
        } else if (delta < -180.0) {
            delta += 360.0;
        }
        gDialPrevAngle = angle;
        dial_nudge_by_angle(delta);
        return;
    }

    if (gDialSkipCount > 0) {
        gDialPrevX = x;
        gDialPrevY = y;
        gDialSkipCount--;
        return;
    }

    if (gDialMode == eDialModeHorizontal) {
        gDialAccum += delta_to_logical(win, x - gDialPrevX, true) * 0.25;
        gDialPrevX  = x;
    } else {
        // Drag up = positive delta (increment)
        gDialAccum += delta_to_logical(win, gDialPrevY - y, false) * 0.25;
        gDialPrevY  = y;
    }
    int steps = (int)gDialAccum;

    if (steps != 0) {
        gDialAccum -= steps;
        dial_nudge(steps);
    }
}

void handle_key(void * win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    (void)mods;

    if (action == 0) {   // GLFW_RELEASE
        return;
    }
    // Basic keyboard → PEPTALK button mapping
    tButtonKey bk    = (tButtonKey)0;
    bool       found = true;

    switch (key) {
        case 256: bk   = pkExit;
            break;                        // GLFW_KEY_ESCAPE
        case 257: bk   = pkEnter;
            break;                        // GLFW_KEY_ENTER
        case 265: bk   = pkUp;
            break;                        // GLFW_KEY_UP
        case 264: bk   = pkDown;
            break;                        // GLFW_KEY_DOWN
        case 263: bk   = pkLeft;
            break;                        // GLFW_KEY_LEFT
        case 262: bk   = pkRight;
            break;                        // GLFW_KEY_RIGHT
        case 280: bk   = pkPrev;
            break;                        // GLFW_KEY_PAGE_UP
        case 281: bk   = pkNext;
            break;                        // GLFW_KEY_PAGE_DOWN
        case 290: bk   = pkF1;
            break;                        // GLFW_KEY_F1
        case 291: bk   = pkF2;
            break;
        case 292: bk   = pkF3;
            break;
        case 293: bk   = pkF4;
            break;
        case 294: bk   = pkF5;
            break;
        case 295: bk   = pkF6;
            break;
        case 45:  bk   = pkDec;
            break;                        // GLFW_KEY_MINUS
        case 61:  bk   = pkInc;
            break;                        // GLFW_KEY_EQUAL
        default: found = false;
            break;
    }

    if (found) {
        peptalk_send_button_event(bk, true);
        peptalk_send_button_event(bk, false);
        gNeedLcdFull  = true;
        gNeedLcdDelta = false;
        gReDraw       = true;
    }
}

void handle_scroll(void * win, double dx, double dy) {
    (void)win;
    (void)dx;

    if (dy == 0.0) {
        return;
    }
    int delta = (int)(dy * 3.0);
    peptalk_send_rotary_event(delta);
    gNeedLcdDelta = true;
    gReDraw       = true;
}

#ifdef __cplusplus
}
#endif
