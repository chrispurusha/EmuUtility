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
#include "inputState.h"
#include "synthlibDefs.h"
#include "types.h"
#include "globalVars.h"
#include "emuGraphics.h"
#include "midiComms.h"
#include "menus.h"
#include "utils.h"
#include "utilsGraphics.h"
#include "mouseHandle.h"
#include "appMenuBar.h"
#include "clickRegion.h"
#include "noteEntry.h"

// Convert GLFW window-space (x,y) to logical canvas coordinates.
// window_to_logical() moved into SynthLib (declared in inputState.h, implemented in
// inputStateGlfw.c): it was character-identical here and in the other editor, and inlined in
// the third — where it had lost the divide-by-zero guard both copies here kept. SynthLib owns
// the window, so the shared one takes no window argument.

// Supplied for SynthLib's contextMenu.c to link against — see mouseHandle.h.
void get_global_gui_scaled_mouse_coord(tCoord * coord) {
    synthlib_mouse_coord(coord);   // see inputState.h
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

void dial_press_click_handler(tCoord coord, eClickPhase phase, void * userData) {
    (void)userData;

    if (phase != eClickPress) {
        return; // release is handled earlier in handle_mouse_button() while gDialDrag is active
    }
    gDialDrag       = true;
    gDialAccum      = 0.0;
    glfwGetCursorPos((GLFWwindow *)synthlib_window(), &gDialPrevX, &gDialPrevY);
    gDialDragActive = true;
    gLastLcdPollMs  = get_time_ms(); // first poll can fire as soon as the interval elapses

    if (synthlib_dial_mode() == eDialModeRotary) {
        gDialPrevAngle = calculate_mouse_angle(coord, emu_dial_rect());
    } else {
        gDialSkipCount = 3;
        glfwSetInputMode((GLFWwindow *)synthlib_window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

// Ends a dial drag. The real mouse release calls this, and so does recover_lost_dial_drag() when the
// release never arrived — one place, so the two cannot drift apart.
//
// No explicit glfwSetCursorPos() here: GLFW's cocoa backend already restores the cursor to wherever
// it was when CURSOR_DISABLED was entered, as soon as we switch back to NORMAL, and two independent
// warps in a row can land a pixel or two apart (same fix as SynthEdit's mouseHandle.c).
static void end_dial_drag(void * win) {
    // The dial's own press was dispatched through the click registry, so it captured (see
    // eClickPhase in clickRegion.h). This path consumes the release without reaching
    // dispatch_click_region(), so drop that capture explicitly — otherwise it stays armed and the
    // next release to land on empty space would be delivered to the dial handler.
    cancel_click_region_capture();
    gDialDrag       = false;
    gDialSkipCount  = 0;

    if (synthlib_dial_mode() != eDialModeRotary) {
        glfwSetInputMode((GLFWwindow *)win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    gDialDragActive = false;
    midi_post_lcd_refresh(true);   // final full sync supersedes the throttled polling during the drag
    synthlib_request_redraw();
}

// A drag whose release never arrived — button came up outside the window, focus lost mid-gesture —
// used to leave gDialDragActive set FOREVER. That is worse than a stuck cursor: the MIDI thread
// gates BOTH of its corrective refreshes on the drag being over (the settle, and the idle resync),
// so a stuck flag disables both and the display stays wrong indefinitely, while a delta is requested
// every 120 ms for a drag that ended long ago.
//
// Called every frame from the render loop; a no-op unless the flag really is stuck. G2-Edit does the
// same with recover_lost_cursor() — this app had no equivalent.
void recover_lost_dial_drag(void * win) {
    if (!gDialDrag) {
        return;
    }

    if (glfwGetMouseButton((GLFWwindow *)win, 0) == 1) {   // left button genuinely still held
        return;
    }
    LOG_DEBUG("Recovering a dial drag whose release never arrived\n");
    end_dial_drag(win);
}

void handle_mouse_button(void * win, int button, int action, int mods, double x, double y) {
    (void)mods;

    if (button != 0) {   // left button only
        return;
    }
    tCoord coord   = synthlib_window_to_logical(x, y);

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
        end_dial_drag(win);
        return;
    }

    // Checked ahead of everything else on mouse-down — mirrors G2-Edit/mouseHandle.c's ordering,
    // since the bar itself needs first refusal on a click before it's treated as a dial/button hit
    // or as closing whatever context menu (bar dropdown or otherwise) is currently open.
    if (pressed && handle_menu_bar_click(gAppMenuBar, app_menu_bar_rect(), coord)) {
        return;
    }

    if (gContextMenu.active) {
        // Same reasoning as the dial-drag path above: an open menu swallows the release, so any
        // capture left over from the press that opened it must not survive into the next gesture.
        if (!pressed) {
            cancel_click_region_capture();
        }

        // Same click's mouse-down just opened/switched/closed this dropdown via
        // handle_menu_bar_click() above — landing back on the bar itself on mouse-up is not a
        // dropdown-item selection, so leave the state exactly as mouse-down left it. Must be
        // checked before handle_context_menu_click(): that call closes the menu itself whenever
        // coord doesn't land on any open item, which a bar click never does.
        if (!pressed && within_rectangle(coord, app_menu_bar_rect())) {
            return;
        }
        handle_context_menu_click(coord); // closes the menu whether the click landed on an item or outside it
        return;
    }

    // The dial and every button register their rect at render time (see
    // emuGraphics.cpp) — that's the entire clickable surface below the menu
    // bar/context menu already handled above, so dispatch alone is
    // authoritative here; no legacy per-widget hit-test fallback needed.
    if (dispatch_click_region(coord, pressed ? eClickPress : eClickRelease)) {
        return;
    }
    LOG_DEBUG("no click region at logical(%.0f,%.0f)\n", coord.x, coord.y);
}

void handle_cursor_pos(void * win, double x, double y) {
    if (!gDialDrag) {
        return;
    }

    if (synthlib_dial_mode() == eDialModeRotary) {
        // Relative tracking — rotating the mouse around the dial centre turns
        // the encoder at the same angular rate, without pinning the indicator
        // to the raw mouse angle (there's no fixed "12 o'clock = value X" on a
        // real endless encoder, so snapping to the click position would jump).
        tCoord coord = synthlib_window_to_logical(x, y);
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

    if (synthlib_dial_mode() == eDialModeHorizontal) {
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

    // Note entry comes first, and gets releases as well as presses — a note has to be let go of.
    // It returns true only for the keys it owns, so a key that plays a note never also reaches the
    // front-panel mapping below.
    if (handle_note_entry_key(key, mods, action)) {
        synthlib_request_redraw();
        return;
    }

    if (action == GLFW_RELEASE) {
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
        // Activity first, then the command, then the refresh — a keypress is a button press by
        // another route and follows the same order and the same reasoning; see emu_button_press().
        midi_note_ui_activity();
        midi_post_button_event(bk, true);
        midi_post_button_event(bk, false);
        midi_post_lcd_refresh(true);    // the screen certainly moved, so fetch it rather than poll
        synthlib_request_redraw();
    }
}

void handle_scroll(void * win, double dx, double dy) {
    (void)win;
    (void)dx;

    if (dy == 0.0) {
        return;
    }
    int delta = (int)(dy * 3.0);

    midi_note_ui_activity();   // scroll-wheel turns settle like any other input — and stand the poll
                               // down before the event goes out, as emu_button_press() explains
    midi_post_rotary_event(delta);
    midi_post_lcd_refresh(true);
    synthlib_request_redraw();
}

#ifdef __cplusplus
}
#endif
