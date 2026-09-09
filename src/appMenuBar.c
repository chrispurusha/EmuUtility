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

// Just two menus, each with a handful of items — small enough that (unlike
// G2-Edit's much larger File/Settings/Backup/Restore/Controls/Tools/View set)
// there's no separate menuActions.c; the action bodies live directly in each
// open_X_menu() below.

static void action_scan_devices(int index) {
    (void)index;
    midi_request_reconnect(); // the scan itself runs on the MIDI thread, which owns the connection state
    wake_glfw();
}

static void open_device_menu(tCoord anchor) {
    static tMenuItem items[] = {
        {"Scan Devices", (tRgb)RGB_GREY_3, action_scan_devices, 0, NULL, 0, 0.0},
        {NULL,           (tRgb)RGB_BLACK,  NULL,                0, NULL, 0, 0.0},
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

    // Labels are fixed strings with a checkmark prefix baked in (tMenuItem has no separate
    // "checked" flag) — point each entry's label at the checked or unchecked variant depending
    // on the current dial mode, rather than mutating the string in place. Same approach as
    // G2-Edit's open_controls_menu (src/appMenuBar.c there).
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


// ── Experimental menu ─────────────────────────────────────────────────────────
// Work that is being tried out rather than relied on, kept as its own menu so that what is
// finished and what is an experiment are not sitting side by side. Anything here may change or
// disappear, and graduates into one of the other menus once it has settled. Modelled on
// G2-Edit's, which is where the renderer choice landed first.


static void open_experimental_menu(tCoord anchor) {
    static tMenuItem items[4];
    int              i = 0;

    // WHICH RENDERER IS RUNNING - a readout, not a control, since 2026-09-09. macOS is Metal only
    // now (SYNTHLIB_NO_GL_BACKEND, set for every Apple target in renderBackendSelect.h), so there is
    // no second backend to switch to: OpenGL is what Windows and Linux will run, and on a Mac it
    // comes back only in a build made with SYNTHLIB_ALLOW_GL_ON_APPLE.
    //
    // What is running now, greyed so it reads as information rather than a control. Without it
    // there is no way to tell which backend drew the window you are looking at.
    static char      rendererLine[48];

    snprintf(rendererLine, sizeof(rendererLine), "Renderer: %s",
             gfx_backend_name(gfx_backend_current()));
    items[i++] = (tMenuItem){
        rendererLine, (tRgb)RGB_GREY_5, NULL, 0, NULL, 0, 0.0
    };
    items[i]   = (tMenuItem){
        NULL, (tRgb)RGB_BLACK, NULL, 0, NULL, 0, 0.0
    };

    open_context_menu(anchor, items, 0, 0.0);
}

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
    {"Device",       open_device_menu      },
    {"Controls",     open_controls_menu    },
    {"Experimental", open_experimental_menu},
    {"Help",         open_help_menu        },
    {NULL,           NULL                  },
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
