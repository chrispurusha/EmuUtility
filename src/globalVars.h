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
// Notes: Docs/code-notes/globalVars.h.md - "// notes §k" refers there.

#ifndef __GLOBAL_VARS_H__
#define __GLOBAL_VARS_H__

#include "sysIncludes.h"
#include "types.h"
#include "msgQueue.h"
#include "synthlibGlobals.h" // synthlib_quit_requested()/synthlib_request_redraw()/synthlib_window()/synthlib_dial_mode() etc.

//extern double           gGlobalGuiScale;
//extern tScrollState     gScrollState;

// notes §1
extern tEmuDevice       gDevice;
extern MIDIClientRef    gMidiClient;
extern MIDIPortRef      gMidiInPort;
extern MIDIPortRef      gMidiOutPort;
extern MIDIEndpointRef  gMidiSource;
extern MIDIEndpointRef  gMidiDest;

// UI/callback threads -> MIDI thread command queue. Initialised by start_midi_thread() before the
// thread is created, so a command posted early is safe.
extern tMessageQueue    gToMidiThread;

// ── PEPTALK session ──────────────────────────────────────────────────────────
extern _Atomic bool     gSessionOpen;
extern _Atomic uint8_t  gSessionSeqId;

// ── LCD ──────────────────────────────────────────────────────────────────────
extern tLcdBuffer       gLcd;
extern pthread_mutex_t  gLcdMutex;            // guards gLcd.pixels/refresh: MIDI-callback writer vs UI-thread reader
// notes §2

extern _Atomic bool     gLcdBaseTrusted;
extern _Atomic double   gLcdLastDeltaMs;


// notes §3
extern _Atomic bool     gDialDragActive;
extern _Atomic double   gLastLcdPollMs;

// notes §4
extern _Atomic double   gLastUiEventMs;

// ── LEDs ─────────────────────────────────────────────────────────────────────
extern _Atomic uint32_t gLeds;            // bitmask of lit LEDs

#endif // __GLOBAL_VARS_H__
