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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>
#pragma clang diagnostic pop

#include "defs.h"
#include "synthlibDefs.h"
#include "types.h"
#include "globalVars.h"
#include "midiComms.h"
#include "noteEntry.h"

// The key map, in semitone order from the leftmost key. The home row is the white notes and the row
// above holds the blacks — a = C, w = C#, s = D, e = D#, d = E, f = F and so on, with k, o, l and p
// carrying on into the octave above where the home row runs out. Identical to G2-Edit's
// note_offset_for_key() (src/virtualKeyboard.c), deliberately: the same fingers should play the
// same notes in both applications.
//
// A table rather than a switch because the release path needs the reverse lookup too — which slot a
// key occupies — so that a key can be remembered by slot and released with the note it actually
// started, not the note its position would produce now (see gKeyNote below).
static const int gNoteKeys[] = {
    GLFW_KEY_A,   // C
    GLFW_KEY_W,   // C#
    GLFW_KEY_S,   // D
    GLFW_KEY_E,   // D#
    GLFW_KEY_D,   // E
    GLFW_KEY_F,   // F
    GLFW_KEY_T,   // F#
    GLFW_KEY_G,   // G
    GLFW_KEY_Y,   // G#
    GLFW_KEY_H,   // A
    GLFW_KEY_U,   // A#
    GLFW_KEY_J,   // B
    GLFW_KEY_K,   // C, octave up
    GLFW_KEY_O,   // C#
    GLFW_KEY_L,   // D
    GLFW_KEY_P,   // D#
};

#define NOTE_KEY_COUNT    ((int)(sizeof(gNoteKeys) / sizeof(gNoteKeys[0])))

static uint8_t   gFirstNote               = NOTE_ENTRY_FIRST_NOTE;
static bool      gEnabled                 = true;

// What each key is currently sounding, or -1 for a key that is up. Recorded per key rather than
// recomputed on release, so shifting the octave while a note is held still releases the note that
// was actually struck instead of one an octave away.
static int16_t   gKeyNote[NOTE_KEY_COUNT] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

// Which slot in the map this key occupies, or -1 for a key that is not a note.
static int note_slot_for_key(int key) {
    for (int i = 0; i < NOTE_KEY_COUNT; i++) {
        if (gNoteKeys[i] == key) {
            return i;
        }
    }

    return -1;
}

static void note_off(int slot) {
    if (gKeyNote[slot] >= 0) {
        midi_post_note_event((uint8_t)gKeyNote[slot], 0, false);
        gKeyNote[slot] = -1;
    }
}

void note_entry_all_notes_off(void) {
    for (int i = 0; i < NOTE_KEY_COUNT; i++) {
        note_off(i);
    }
}

uint8_t note_entry_first_note(void) {
    return gFirstNote;
}

// Shift by whole octaves, and stop where the map's top key would run off the end of the MIDI range
// rather than where the bottom key would — otherwise the top of the keyboard silently stops
// sounding while the octave display carries on climbing.
static void shift_octave(int semitones) {
    int next = (int)gFirstNote + semitones;

    if ((next < 0) || ((next + NOTE_KEY_COUNT - 1) > NOTE_ENTRY_MAX_NOTE)) {
        return;
    }
    // Anything still held belongs to the old octave. Release it here, or its own key-up will look
    // up a slot that has since been re-pointed and leave the original note ringing.
    note_entry_all_notes_off();
    gFirstNote = (uint8_t)next;
}

void note_entry_set_enabled(bool enabled) {
    if (!enabled) {
        note_entry_all_notes_off();   // never leave a note ringing across the switch
    }
    gEnabled = enabled;
}

bool note_entry_enabled(void) {
    return gEnabled;
}

bool handle_note_entry_key(int key, int mods, int action) {
    // Switched off: consume nothing, so the letter keys remain available to whatever else wants them
    // rather than being silently swallowed by a keyboard that is not playing.
    if (!gEnabled) {
        return false;
    }

    // Cmd/Ctrl/Alt suppress note entry entirely, so a shortcut on one of these letters can never
    // also play. Checked before the map lookup so a modified key falls through as an ordinary key
    // press rather than being swallowed.
    if ((mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL | GLFW_MOD_ALT)) != 0) {
        return false;
    }

    // The octave shift belongs with the notes: it decides WHICH octave those keys play. Same Z/X
    // keys as G2-Edit's virtual keyboard.
    if ((key == GLFW_KEY_Z) || (key == GLFW_KEY_X)) {
        if (action == GLFW_PRESS) {
            shift_octave((key == GLFW_KEY_Z) ? -12 : 12);
            synthlib_request_redraw();
        }
        return true;
    }
    int slot = note_slot_for_key(key);

    if (slot < 0) {
        return false;
    }

    // GLFW_REPEAT is ignored: the OS auto-repeat would restrike the note several times a second for
    // as long as the key was held, which is not what holding a note means.
    if (action == GLFW_PRESS) {
        // A press with the key already down can only be a repeat GLFW failed to mark, or a release
        // that went missing. Either way the note is already sounding.
        if (gKeyNote[slot] < 0) {
            gKeyNote[slot] = (int16_t)(gFirstNote + slot);
            midi_post_note_event((uint8_t)gKeyNote[slot], NOTE_ENTRY_VELOCITY, true);
        }
    } else if (action == GLFW_RELEASE) {
        note_off(slot);
    }
    return true;
}

#ifdef __cplusplus
}
#endif
