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

// Window/dial-mode settings persistence — goes through SynthLib's prefs.h (a plain "key=value"
// text file under a per-OS standard config directory) instead of NSUserDefaults, so none of this
// needs Objective-C/Cocoa any more. Same shape as G2-Edit's persistence.c, minus zoom/file-browser-
// directory, which this app doesn't have.

#include "misc.h"
#include "defs.h"
#include "types.h"
#include "globalVars.h"
#include "graphics.h"
#include "prefs.h"
#include "synthlibPersistence.h"

// Restores window size/position/dial-mode state saved from a previous run. Called once at
// startup from setup_main_menu() (misc.mm) — prefs_init() must run before this (also there), so
// the settings file is loaded before any of these get_* calls.
void load_saved_settings(void) {
    synthlib_load_window_and_dial_mode(TARGET_FRAME_BUFF_WIDTH, TARGET_FRAME_BUFF_HEIGHT);
}
