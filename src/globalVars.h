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

#ifndef __GLOBAL_VARS_H__
#define __GLOBAL_VARS_H__

#include "sysIncludes.h"
#include "types.h"
#include "msgQueue.h"
#include "synthlibGlobals.h" // synthlib_quit_requested()/synthlib_request_redraw()/synthlib_window()/synthlib_dial_mode() etc.

//extern double           gGlobalGuiScale;
//extern tScrollState     gScrollState;

// ── MIDI / device ────────────────────────────────────────────────────────────
// All six of these are owned by the MIDI thread (midiComms.c) — only it writes them, and only from
// its own loop or from a command drained off gToMidiThread. Other threads read gDevice for display
// and post commands; they must not call into the scan/connect path directly. See msgQueue.h.
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
extern _Atomic bool     gNeedLcdFull;         // request full LCD dump next poll
extern _Atomic bool     gNeedLcdDelta;        // request delta LCD dump next poll
extern _Atomic bool     gLcdPending;          // an LCD request is in-flight; don't send another

// Delta stream bookkeeping. Routine updates are fetched as deltas rather than full frames because a
// full frame is 2205 bytes on a 31250-baud DIN link — ~705 ms of wire time for every button press,
// where the delta for a typical change is 377 bytes / ~256 ms (measured on an E5000, 2026-08-19).
//
// A delta is an XOR against the frame we already hold, so the two ends stay in step only for as
// long as every delta is applied; one lost message would leave the display quietly wrong forever.
// gLcdBaseTrusted is false from the moment a delta is applied until a full frame re-bases it, and
// the MIDI thread fetches exactly one full frame once the display has been quiet for
// LCD_RESYNC_IDLE_MS. That puts the expensive transfer where nobody is waiting on it, and bounds
// how long a lost delta can go uncorrected. gLcdLastDeltaMs is when the last delta landed, i.e.
// when that idle timer restarts.
extern _Atomic bool     gLcdBaseTrusted;
extern _Atomic double   gLcdLastDeltaMs;

// When the in-flight LCD request went out, so the debug log can report the round trip it cost. Worth
// carrying permanently: on a DIN link the size of the reply IS the response time (2205 bytes = ~705
// ms at 31250 baud), so "payloadLen + dt" is the whole latency story in one line.
extern _Atomic double   gLcdReqMs;

// Throttled LCD refresh while a dial drag is held: gDialDragActive is true
// for the whole press-to-release span, and the MIDI poll thread requests one
// delta dump every DIAL_LCD_POLL_INTERVAL_MS while it's set — regardless of
// whether new encoder ticks are currently being sent — so the screen keeps
// refreshing during a long continuous drag and during a held-but-paused
// drag alike, without requesting faster than the throttle interval. Never
// causes a new value to be sent — that stays solely driven by dial_nudge().
extern _Atomic bool     gDialDragActive;
extern _Atomic double   gLastLcdPollMs;

// ── LEDs ─────────────────────────────────────────────────────────────────────
extern _Atomic uint32_t gLeds;            // bitmask of lit LEDs
extern _Atomic bool     gNeedLeds;        // request LED state next poll

#endif // __GLOBAL_VARS_H__
