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

#ifndef __MIDI_COMMS_H__
#define __MIDI_COMMS_H__

#include "sysIncludes.h"
#include "types.h"
#include "msgQueue.h"   // tLcdReplyData, for the callback thread's reply hand-off

#ifdef __cplusplus
extern "C" {
#endif

// Called once from main() to start the MIDI thread and open CoreMIDI ports. Initialises
// gToMidiThread before creating the thread, so posting a command before the thread is running is
// safe (it is simply drained on the first tick).
int start_midi_thread(void);

// Send raw SysEx (or any MIDI bytes) to the currently selected output. MIDI-THREAD ONLY — it reads
// gMidiDest, which that thread owns. Everything the UI wants sent goes through the midi_post_*
// commands below instead.
void midi_send(const uint8_t * data, uint32_t length);

// ── Commands (safe to call from ANY thread) ──────────────────────────────────
// Each posts to gToMidiThread and wakes the MIDI thread's CFRunLoop, so the work runs on the one
// thread that owns the connection state. Named to match SynthEdit's midi_request_reconnect(), which
// solves the same problem there.

// Rescan CoreMIDI and re-identify. Replaces the old public midi_scan_devices(), which callers on the
// UI thread (Scan Devices menu, sleep/wake notification) used to invoke directly — racing the MIDI
// thread's own use of gMidiSource/gMidiDest/gDevice and CoreMIDI's port connections.
void midi_request_reconnect(void);

// Hand an identity reply seen on the CoreMIDI read callback thread to the MIDI thread, which does
// the destination lookup and takes ownership of the resulting connection.
void midi_post_identity_reply(MIDIEndpointRef source, uint8_t deviceId, uint16_t family, uint16_t member);

// PEPTALK events raised by UI interaction (on-screen buttons, keyboard, dial, scroll wheel).
void midi_post_button_event(tButtonKey key, bool pressed);
void midi_post_rotary_event(int delta);
void midi_post_session_open(void);

// A MIDI Note On/Off for the computer-keyboard note entry (noteEntry.c). Ordinary channel-voice
// MIDI rather than PEPTALK — see NOTE_ENTRY_MIDI_CHANNEL in defs.h.
void midi_post_note_event(uint8_t note, uint8_t velocity, bool on);

// Record that the user just did something that may change the display, so the MIDI thread takes one
// more delta once the burst stops. Call alongside any button or dial event — see LCD_SETTLE_MS.
void midi_note_ui_activity(void);

// Ask for the display to be re-read: a whole frame, or a delta if that will do. Safe from any
// thread. This is a REQUEST rather than a flag write on purpose — the want-bits belong to the MIDI
// thread, so nothing can clear one that was never served. See tLcdRefreshData.
void midi_post_lcd_refresh(bool full);

// Ask for the front-panel LED state. Safe from any thread, same reasoning as the refresh above.
void midi_post_led_refresh(void);

// Hand the MIDI thread the outcome of an LCD reply the CoreMIDI callback has already dealt with.
void midi_post_lcd_reply(const tLcdReplyData * reply);

// The sequence id of the LCD request currently outstanding, for the callback thread's staleness
// check. Written only by the MIDI thread. Returns false when nothing is outstanding.
bool midi_outstanding_lcd_seq(uint8_t * seqOut);

// Send MIDI Identity Request to all outputs to discover connected devices. MIDI-THREAD ONLY.
void midi_send_identity_request(void);

// Called from the graphics layer to wake the GLFW event loop.
void register_midi_wake_cb(void ( *cb )(void));

#ifdef __cplusplus
}
#endif

#endif // __MIDI_COMMS_H__
