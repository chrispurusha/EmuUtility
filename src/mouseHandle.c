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
// Notes: Docs/code-notes/mouseHandle.c.md - "// notes §k" refers there.

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
#include "synthlibPopups.h"

// notes §1

// Supplied for SynthLib's contextMenu.c to link against — see mouseHandle.h.
void get_global_gui_scaled_mouse_coord(tCoord * coord) {
    synthlib_mouse_coord(coord);   // see inputState.h
}

// Scale a window-space delta to logical-space delta
// delta_to_logical() removed: the cursor handler receives logical coordinates now, so a delta of
// two of them is already logical. See gDialPrevX/gDialPrevY.

#define GLFW_CURSOR             0x00033001
#define GLFW_CURSOR_NORMAL      0x00034001
#define GLFW_CURSOR_DISABLED    0x00034003

static bool   gDialDrag      = false;
// notes §2
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
    {
        tCoord start = {0};

        get_global_gui_scaled_mouse_coord(&start);   // logical, to match the handler
        gDialPrevX = start.x;
        gDialPrevY = start.y;
    }
    gDialDragActive = true;
    gLastLcdPollMs  = get_time_ms(); // first poll can fire as soon as the interval elapses

    if (synthlib_dial_mode() == eDialModeRotary) {
        gDialPrevAngle = calculate_mouse_angle(coord, emu_dial_rect());
    } else {
        gDialSkipCount = 3;
        glfwSetInputMode((GLFWwindow *)synthlib_window(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    }
}

// notes §3
static void end_dial_drag(void * win) {
    // notes §4
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

// notes §5
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

// The coordinate arrives already scaled and the button already decoded — SynthLib's shim does both,
// and updates the modifier state before this runs, which the hand-written shim here never did. See
// tSynthLibInputHandlers in synthlibWindow.h.
void handle_mouse_button(tCoord coord, tMouseButton button, int mods) {
    (void)mods;

    if ((button != mouseButtonLeftDown) && (button != mouseButtonLeftUp)) {
        return;   // left button only
    }
    bool pressed = (button == mouseButtonLeftDown);

    LOG_DEBUG("mouse %s logical(%.0f,%.0f)\n", pressed ? "press" : "release", coord.x, coord.y);

    // notes §6
    if (!pressed && gDialDrag) {
        end_dial_drag(synthlib_window());
        return;
    }

    // notes §7
    if (synthlib_popups_modal_active()) {
        if (!pressed) {
            // The popup swallows this release, so a capture left armed by the press that opened it
            // must not survive into the next gesture — same reasoning as the context-menu branch.
            cancel_click_region_capture();
        }

        if (synthlib_popups_dispatch_click(coord, pressed ? mouseButtonLeftDown : mouseButtonLeftUp)) {
            synthlib_request_redraw();
            return;
        }
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

        // notes §8
        if (!pressed && within_rectangle(coord, app_menu_bar_rect())) {
            return;
        }
        handle_context_menu_click(coord); // closes the menu whether the click landed on an item or outside it
        return;
    }

    // notes §9
    if (dispatch_click_region(coord, pressed ? eClickPress : eClickRelease)) {
        return;
    }
    LOG_DEBUG("no click region at logical(%.0f,%.0f)\n", coord.x, coord.y);
}

void handle_cursor_pos(tCoord coord) {
    if (!gDialDrag) {
        return;
    }

    if (synthlib_dial_mode() == eDialModeRotary) {
        // notes §10
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
        gDialPrevX = coord.x;
        gDialPrevY = coord.y;
        gDialSkipCount--;
        return;
    }

    if (synthlib_dial_mode() == eDialModeHorizontal) {
        gDialAccum += (coord.x - gDialPrevX) * 0.25;
        gDialPrevX  = coord.x;
    } else {
        // Drag up = positive delta (increment)
        gDialAccum += (gDialPrevY - coord.y) * 0.25;
        gDialPrevY  = coord.y;
    }
    int steps = (int)gDialAccum;

    if (steps != 0) {
        gDialAccum -= steps;
        dial_nudge(steps);
    }
}

void handle_key(int key, int scancode, int action, int mods) {
    (void)scancode;

    // See handle_mouse_button(): a modal popup owns the keyboard while it is up, which is what
    // gives the alert dialog its Escape and Return. Without this, a note-entry key would play a
    // note through a modal dialog.
    if (synthlib_popups_modal_active()) {
        if (synthlib_popups_dispatch_key(key, mods, action)) {
            synthlib_request_redraw();
        }
        return;
    }

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

void handle_scroll(double dx, double dy) {
    (void)dx;

    // A MODAL POPUP OWNS THE WHEEL, and here that is worth more than tidiness: a scroll in this app
    // is not a UI gesture, it is midi_post_rotary_event() — it turns the sampler's data encoder.
    // Without this, spinning the wheel over a modal dialog edited the instrument underneath it.
    if (synthlib_popups_modal_active()) {
        if (synthlib_popups_dispatch_scroll(dy)) {
            synthlib_request_redraw();
        }
        return;   // consumed or not, it is not the encoder's while a dialog is up
    }

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

// notes §11
void handle_character(unsigned int codepoint) {
    if (synthlib_popups_dispatch_char(codepoint)) {
        synthlib_request_redraw();
    }
}

#ifdef __cplusplus
}
#endif
