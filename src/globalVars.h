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

// ── Lifecycle ────────────────────────────────────────────────────────────────
extern _Atomic bool     gQuitAll;
extern _Atomic bool     gReDraw;

// ── GLFW window ──────────────────────────────────────────────────────────────
extern void *           gWindow;           // GLFWwindow*; void* avoids pulling GLFW into C headers
//extern double           gGlobalGuiScale;
//extern tScrollState     gScrollState;

// ── MIDI / device ────────────────────────────────────────────────────────────
extern tEmuDevice       gDevice;
extern MIDIClientRef    gMidiClient;
extern MIDIPortRef      gMidiInPort;
extern MIDIPortRef      gMidiOutPort;
extern MIDIEndpointRef  gMidiSource;
extern MIDIEndpointRef  gMidiDest;

// ── PEPTALK session ──────────────────────────────────────────────────────────
extern _Atomic bool     gSessionOpen;
extern _Atomic uint8_t  gSessionSeqId;

// ── LCD ──────────────────────────────────────────────────────────────────────
extern tLcdBuffer       gLcd;
extern _Atomic bool     gNeedLcdFull;         // request full LCD dump next poll
extern _Atomic bool     gNeedLcdDelta;        // request delta LCD dump next poll
extern _Atomic bool     gLcdPending;          // an LCD request is in-flight; don't send another

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

// ── UI ───────────────────────────────────────────────────────────────────────
extern tDialMode        gDialMode;

#endif // __GLOBAL_VARS_H__
