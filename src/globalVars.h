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
// The LCD REQUEST state that used to live here — gNeedLcdFull, gNeedLcdDelta, gLcdPending,
// gNeedLeds, gLcdInFlight, gLcdReqMs, gLcdSettled — is gone. It was written by both the MIDI thread
// and the CoreMIDI read callback, and although each was _Atomic the read-modify-write SEQUENCES were
// not: a want-bit set between one thread's test and the other's clear was silently dropped, and a
// pending flag cleared while a request was being issued let two transfers overlap, which corrupts
// the frame. It is now private to midiComms.c and everything else posts (midi_post_lcd_refresh /
// midi_post_led_refresh / midi_post_lcd_reply). See that file for the ownership rule.

extern _Atomic bool     gLcdBaseTrusted;
extern _Atomic double   gLcdLastDeltaMs;


// Throttled LCD refresh while a dial drag is held: gDialDragActive is true
// for the whole press-to-release span, and the MIDI poll thread requests one
// delta dump every DIAL_LCD_POLL_INTERVAL_MS while it's set — regardless of
// whether new encoder ticks are currently being sent — so the screen keeps
// refreshing during a long continuous drag and during a held-but-paused
// drag alike, without requesting faster than the throttle interval. Never
// causes a new value to be sent — that stays solely driven by dial_nudge().
extern _Atomic bool     gDialDragActive;
extern _Atomic double   gLastLcdPollMs;

// When the user last did anything that could change the display. Written only by UI threads and read
// only by the MIDI thread — ONE writer, which is why this one may stay a plain atomic. The matching
// "has the trailing delta been taken yet" bit lives inside midiComms.c, because both threads would
// otherwise write it. See LCD_SETTLE_MS in defs.h.
extern _Atomic double   gLastUiEventMs;

// ── LEDs ─────────────────────────────────────────────────────────────────────
extern _Atomic uint32_t gLeds;            // bitmask of lit LEDs

#endif // __GLOBAL_VARS_H__
