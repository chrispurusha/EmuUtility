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
// Notes: Docs/code-notes/appMenuBar.c.md - "// notes §k" refers there.

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "defs.h"
#include "synthlibDefs.h"
#include "types.h"
#include "utilsGraphics.h"
#include "contextMenu.h"
#include "synthlibVersion.h"
#include "alertDialog.h"
#include "renderBackend.h"
#include "prefs.h"
#include "globalVars.h"
#include "midiComms.h"
#include "misc.h"
#include "graphics.h"
#include "appMenuBar.h"
#include "noteEntry.h"
#include "synthlibPersistence.h"
#include "midiPortDialog.h"

// notes §1

// The scan itself runs on the MIDI thread, which owns the connection state. The same request serves
// the dialogue's Scan button and a changed choice: either way the thing to do is look again.
static void rescan_devices(void) {
    midi_request_reconnect();
    wake_glfw();
}

// SCAN MOVED INTO HERE (2026-09-11). The Device menu's only item was "Scan Devices"; the dialogue
// has it as a button, beside the input and output lists it now scans within.
static void action_midi_ports(int index) {
    (void)index;
    midi_port_dialog_open(&(tMidiPortDialogHost){
        .title   = "MIDI Ports",
        .changed = rescan_devices,
        .scan    = rescan_devices,
        .status  = midi_port_status,
    });
}

static void open_device_menu(tCoord anchor) {
    static tMenuItem items[] = {
        {"MIDI Ports...", (tRgb)RGB_GREY_3, action_midi_ports, 0, NULL, 0, 0.0},
        {NULL,            (tRgb)RGB_BLACK,  NULL,              0, NULL, 0, 0.0},
    };

    open_context_menu(anchor, items, 0, 0.0);
}

static void action_dial_mode_rotary(int index) {
    (void)index;
    synthlib_set_dial_mode(eDialModeRotary);
    synthlib_save_dial_mode(synthlib_dial_mode());
}

static void action_dial_mode_vertical(int index) {
    (void)index;
    synthlib_set_dial_mode(eDialModeVertical);
    synthlib_save_dial_mode(synthlib_dial_mode());
}

static void action_dial_mode_horizontal(int index) {
    (void)index;
    synthlib_set_dial_mode(eDialModeHorizontal);
    synthlib_save_dial_mode(synthlib_dial_mode());
}

static void action_toggle_note_keyboard(int index) {
    (void)index;
    bool enabled = !note_entry_enabled();

    note_entry_set_enabled(enabled);
    save_note_keyboard_setting(enabled);   // same treatment as the dial mode above
}

static void open_controls_menu(tCoord anchor) {
    static tMenuItem items[]      = {
        {"* Rotary",             (tRgb)RGB_GREY_3, action_dial_mode_rotary,     0, NULL, 0, 0.0},
        {"* Vertical",           (tRgb)RGB_GREY_3, action_dial_mode_vertical,   0, NULL, 0, 0.0},
        {"* Horizontal",         (tRgb)RGB_GREY_3, action_dial_mode_horizontal, 0, NULL, 0, 0.0},
        {"Keyboard Plays Notes", (tRgb)RGB_GREY_3, action_toggle_note_keyboard, 0, NULL, 0, 0.0},
        {NULL,                   (tRgb)RGB_BLACK,  NULL,                        0, NULL, 0, 0.0},
    };

    // notes §2
    static char *    checked[3]   = {"* Rotary", "* Vertical", "* Horizontal"};
    static char *    unchecked[3] = {"Rotary", "Vertical", "Horizontal"};
    int              i;

    for (i = 0; i < 3; i++) {
        items[i].label = ((int)synthlib_dial_mode() == i) ? checked[i] : unchecked[i];
    }

    // Same checkmark-in-the-label trick as the dial modes above (tMenuItem has no checked flag).
    items[3].label = note_entry_enabled() ? "* Keyboard Plays Notes" : "Keyboard Plays Notes";

    open_context_menu(anchor, items, 0, 0.0);
}


// notes §3

// ── Help menu ─────────────────────────────────────────────────────────────────
// WHICH BUILD IS THIS. Version, compile time and the render backend in force. The backend is a
// preference now, so "it looks wrong" and "it looks wrong on Metal" are different reports.
static void action_about(int index) {
    (void)index;
    show_alert("About", synthlib_about_text("EmuUtility"));
}

static void open_help_menu(tCoord anchor) {
    static tMenuItem items[2] = {0};

    items[0] = (tMenuItem){
        "About EmuUtility...", (tRgb)RGB_GREY_3, action_about, 0, NULL, 0, 0.0
    };
    items[1] = (tMenuItem){
        NULL, (tRgb)RGB_BLACK, NULL, 0, NULL, 0, 0.0
    };
    open_context_menu(anchor, items, 0, 0.0);
}

tMenuBarItem gAppMenuBar[] = {
    {"Device",   open_device_menu  },
    {"Controls", open_controls_menu},
    {"Help",     open_help_menu    },
    {NULL,       NULL              },
};

tRectangle app_menu_bar_rect(void) {
    return (tRectangle){
        {
            0.0, 0.0
        }, {
            (get_render_width() / gGlobalGuiScale), MENU_BAR_HEIGHT
        }
    };
}

#ifdef __cplusplus
}
#endif
