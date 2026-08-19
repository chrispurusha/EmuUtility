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

#ifndef __MISC_H__
#define __MISC_H__

#ifdef __cplusplus
extern "C" {
#endif

// register_sleep_wake_notifications() and setup_main_menu() are implemented in misc.mm — the only
// two things left in this codebase that genuinely need Objective-C/Cocoa. Everything else
// declared below is plain C: the Device/Controls menus live in appMenuBar.c, settings persistence
// (backed by SynthLib's cross-platform prefs.h rather than NSUserDefaults) lives in persistence.c.
void register_sleep_wake_notifications(void);
void setup_main_menu(void);

// Applies saved window size/position and dial mode — called once by setup_main_menu() right after
// prefs_init(). Implemented in persistence.c.
void load_saved_settings(void);

// This app's own container tmp directory, with a trailing '/'. The App Sandbox
// (com.apple.security.app-sandbox) makes a hardcoded "/tmp/..." path silently unreachable —
// fopen() just returns NULL, no error — so the backdoor command channel in graphics.c builds its
// paths on top of this instead. Same helper, same reason, as SynthEdit's synth_temp_dir().
const char * emu_temp_dir(void);

#ifdef __cplusplus
}
#endif

#endif // __MISC_H__
