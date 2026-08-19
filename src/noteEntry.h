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

#ifndef __NOTE_ENTRY_H__
#define __NOTE_ENTRY_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// The computer keyboard as a music keyboard: the home row plays the white notes and the row above
// holds the blacks, the layout every tracker and DAW uses. Ported from G2-Edit's
// handle_note_entry_key() (src/virtualKeyboard.c there), with the same key map and the same Z/X
// octave shift, so muscle memory carries between the two applications.
//
// Routed from handle_key() BEFORE its front-panel button mapping. Returns true when it consumed the
// key, so a key that plays a note can never also be a panel shortcut.
bool handle_note_entry_key(int key, int mods, int action);

// Release everything still sounding. Called when the window loses focus — the release half of a
// held key is delivered to whoever has focus, so without this a note started here and finished
// elsewhere would ring forever on real hardware.
void note_entry_all_notes_off(void);

// The MIDI note the 'a' key currently plays; moved by the Z/X octave shift. For the status line and
// the backdoor's STATE dump.
uint8_t note_entry_first_note(void);

// Whether the computer keyboard plays notes at all.
//
// Turned off while editing text on the device — a name, say — where the letter keys are wanted for
// the sampler's own purposes and playing a note on every keystroke is worse than useless. Switching
// off releases anything currently sounding, so a key held across the switch cannot stick.
void note_entry_set_enabled(bool enabled);
bool note_entry_enabled(void);

// Whether the computer keyboard plays notes at all.
//
// Turned off while editing text on the device — a name, say — where the letter keys are wanted for
// the sampler's own purposes and playing a note on every keystroke is worse than useless. Switching
// off releases anything currently sounding, so a key held at the moment of the switch cannot stick.
void note_entry_set_enabled(bool enabled);
bool note_entry_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // __NOTE_ENTRY_H__
