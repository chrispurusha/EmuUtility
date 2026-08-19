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

// ── Sample Dump Standard ─────────────────────────────────────────────────────

// Begin sending a sample to the device. Ownership of dump->samples passes to the MIDI thread, which
// frees it when the transfer ends however it ends — the caller must not touch it afterwards.
void midi_post_sds_start(const tSampleDump * dump, uint16_t sampleNumber, uint8_t channel);

// Abandon a transfer in progress.
void midi_post_sds_cancel(void);

// Hand the MIDI thread a handshake byte seen by the CoreMIDI callback (ACK/NAK/WAIT/CANCEL).
void midi_post_sds_handshake(uint8_t type, uint8_t packet);

// Ask the device to send us sample `sampleNumber`, writing it to `path` as a .wav when it arrives.
// NON-DESTRUCTIVE — nothing on the device changes, unlike sending a sample to it.
void midi_post_sds_request(uint16_t sampleNumber, const char * path);

// Hand the MIDI thread a dump header or data packet seen by the CoreMIDI callback.
void midi_post_sds_rx_frame(const uint8_t * data, uint32_t length);

// Receive progress. Returns false when nothing is being received.
bool midi_sds_rx_progress(uint32_t * wordsGot, uint32_t * wordsTotal, char * status, size_t statusMax);

// Progress, for the UI and for tests. Returns false when nothing is being sent.
bool midi_sds_progress(uint32_t * packetsSent, uint32_t * packetsTotal, bool * closedLoop);

// Hand the MIDI thread the outcome of an LCD reply the CoreMIDI callback has already dealt with.
void midi_post_lcd_reply(const tLcdReplyData * reply);

// Should this reply be refused rather than applied? True only when the timeout has left more than
// one request outstanding AND this reply's sequence id is not the one we are waiting for — a
// mismatch alone means nothing, because the device also speaks unprompted. MIDI thread owns the
// state; the callback thread only reads it.
bool midi_lcd_reply_suspect(uint8_t replySeq);

// True when the reply in hand describes a screen the user has already moved past — painting it would
// put an old value on screen over a newer one. See its definition for the measurement behind it.
bool midi_lcd_reply_describes_stale_screen(void);

// True while the outstanding request is a PROBE — a delta asked purely to learn whether the screen
// changed, whose content must not be applied. See LCD_PROBE_WHEN_IDLE.
bool midi_lcd_probe_in_flight(void);

// Has the display stopped moving? True when nothing is on the wire, nothing is wanted, and the last
// reply came back reporting no change — i.e. the device itself has said "nothing has changed since
// you last asked". This is the only sound moment to compare our frame against a fetched one: before
// it, a difference may simply be the device still working through its own backlog rather than
// anything wrong on our side.
bool midi_lcd_is_quiet(void);

// Encoder accounting, for measuring the coalescing: how many ticks arrived from the UI, and how many
// PEPTALK messages those actually became on the wire. Counters only; nothing depends on them.
void midi_rotary_counts(uint32_t * ticksIn, uint32_t * messagesOut);

// Send MIDI Identity Request to all outputs to discover connected devices. MIDI-THREAD ONLY.
void midi_send_identity_request(void);

// Called from the graphics layer to wake the GLFW event loop.
void register_midi_wake_cb(void ( *cb )(void));

#ifdef __cplusplus
}
#endif

#endif // __MIDI_COMMS_H__
