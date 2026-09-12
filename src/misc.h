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
// Notes: Docs/code-notes/misc.h.md - "// notes §k" refers there.

#ifndef __MISC_H__
#define __MISC_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// notes §1
void register_sleep_wake_notifications(void);
void setup_main_menu(void);

// Applies saved window size/position and dial mode — called once by setup_main_menu() right after
// prefs_init(). Implemented in persistence.c.
void load_saved_settings(void);

// Persists the Controls > Keyboard Plays Notes setting. Implemented in persistence.c alongside the
// window and dial-mode state.
void save_note_keyboard_setting(bool enabled);

// notes §2
const char * emu_temp_dir(void);

#ifdef __cplusplus
}
#endif

#endif // __MISC_H__
