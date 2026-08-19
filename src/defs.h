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

#ifndef __DEFS_H__
#define __DEFS_H__

// ── Build toggles ─────────────────────────────────────────────────────────────
#define ENABLE_DEBUG                1
#define ENABLE_LOG_DEBUG            1

// Window
#define WINDOW_TITLE                "EmuUtility"
#define TARGET_FRAME_BUFF_WIDTH     (2560)
#define TARGET_FRAME_BUFF_HEIGHT    (1440)     // 16:9, matching G2-Edit/SynthEdit

// LCD display geometry
#define LCD_WIDTH                   (240)
#define LCD_HEIGHT                  (64)
#define LCD_BYTES                   (LCD_WIDTH * LCD_HEIGHT / 8)            // 1920

// PEPTALK SysEx constants
#define EMU_MANUFACTURER_ID         (0x18)            // E-mu/Ensoniq
#define PEPTALK_DEST                (0x7F)            // broadcast destination

// PEPTALK message types
#define PEPTALK_SESSION_OPEN        (0x10)
#define PEPTALK_SESSION_CLOSE       (0x11)
#define PEPTALK_BUTTON_EVENT        (0x40)
#define PEPTALK_ROTARY_EVENT        (0x43)
#define PEPTALK_LCD_DUMP_RESP       (0x50)
#define PEPTALK_LCD_DUMP_REQ        (0x51)
#define PEPTALK_LCD_DELTA_REQ       (0x52)
#define PEPTALK_LCD_DELTA_RESP      (0x53)
#define PEPTALK_LED_STATE_REQ       (0x60)
#define PEPTALK_LED_STATE_RESP      (0x61)
#define PEPTALK_SESSION_STATUS      (0x7F)

// E-mu EOS device family (E4, E5000, etc.)
#define EMU_EOS_FAMILY              (1025)

// ── LCD refresh ──────────────────────────────────────────────────────────────
// How long the display has to stay quiet before one full frame is fetched to re-base the delta
// stream. Measured on a real E5000 over DIN MIDI (2026-08-19): a full frame is 2205 bytes on the
// wire = ~705 ms at 31250 baud, and the round trip measures 714-874 ms; the delta for a typical
// button press is 377 bytes / ~256 ms. So routine updates go by delta and the expensive full frame
// is deferred to a moment when nobody is waiting for it.
//
// Deliberately NOT eager. A resync cannot be recalled once its request is on the wire, so one
// firing just before a key press makes THAT press wait out the full frame ahead of it. At 1500 ms
// a user clicking every couple of seconds triggered one between almost every press; five seconds
// means only a genuine pause pays for it. The precise trigger lives elsewhere anyway — a delta that
// overruns the frame re-bases immediately (see peptalk_apply_lcd_delta) — so this timer only covers
// the case of a delta that was never delivered at all.
//
// Raised 5000 -> 20000 on 2026-08-20. A resync cannot be recalled once its request is on the wire,
// so if the user resumes inside the ~715 ms it takes, their input queues behind it: the hardware
// moves on and the screen catches up a beat later. A five-second pause is a normal part of using the
// thing, so that collision was reachable. Twenty seconds is a genuine walk-away.
//
// The cost of waiting longer is now much lower than it was when this value was chosen. Back then
// deltas were suspect and this was the only safety net; since, the actual causes have been found and
// fixed (foreign MIDI splicing into the reassembly buffer, replies mispaired with requests, the
// request state machine racing between two threads), and a delta that overruns the frame already
// forces an immediate re-base. This is now genuine insurance rather than routine maintenance.
#define LCD_RESYNC_IDLE_MS    (20000.0)

// How long an LCD request may stay in flight before it is written off. gLcdPending exists to keep
// one request on the wire at a time, but nothing ever cleared it except a reply — so a single lost
// or corrupted response left it set forever and the display simply stopped updating, with no error
// and no way back short of a restart. That mattered less when every update was a full frame the
// user was already waiting on; it matters more now the delta stream is the normal path. Comfortably
// clear of the worst round trip measured.
//
// Raised 3000 -> 8000 on 2026-08-20. At 3 s it was firing on responses that were merely SLOW, not
// lost — under load (an unsolicited session status forcing a full frame, an LED request sharing the
// link) a reply can take well over 3 s. Timing one out issues a second request while the first is
// still on its way, and the late reply is then read as the answer to the new one: a stale delta gets
// applied to a frame that has moved on. That was measured corrupting 295 to 1286 of 1920 bytes. The
// gLcdInFlight counter makes that safe even when it does happen; this just stops it happening for
// no reason.
#define LCD_REQUEST_TIMEOUT_MS    (8000.0)

// How long after the last button press or dial detent to take ONE more delta.
//
// Every other refresh in this app is triggered BY an event, and its request goes out in the same
// breath as the event that caused it — so the last request of a burst can be answered before the
// device has finished acting on the final event, and then nothing asks again. The display sits one
// step behind the hardware until the 5 s resync, which is far too slow to read as responsive. This
// is the trailing request that catches the landing point: the dial's own 120 ms polling stops dead
// when the drag ends, and coalesced Inc/Dec presses have the same gap.
//
// Comfortably past the device's own turnaround (~130 ms fixed overhead, measured), while still
// feeling immediate. Costs one small delta (~200 ms) per burst, not per event.
#define LCD_SETTLE_MS    (250.0)

// ── Computer-keyboard note entry ─────────────────────────────────────────────
// Notes go out as ordinary MIDI, not PEPTALK: PEPTALK drives the front panel, and a note is not a
// front-panel event. The sampler plays them on its own basic channel, so this must match whatever
// that is set to (MASTER > MIDI on the device). Channel 1 is the factory default, and 0 here is
// the wire value for it — MIDI channels are 1-based on a front panel and 0-based on the wire.
#define NOTE_ENTRY_MIDI_CHANNEL       (0)
#define NOTE_ENTRY_VELOCITY           (100)
#define NOTE_ENTRY_FIRST_NOTE         (48)     // C3 — the note the 'a' key plays before any octave shift
#define NOTE_ENTRY_MAX_NOTE           (127)

#define MIDI_NOTE_OFF                 (0x80)
#define MIDI_NOTE_ON                  (0x90)

// MIDI identity request (Universal SysEx)
#define MIDI_SYSEX_START              (0xF0)
#define MIDI_SYSEX_END                (0xF7)
#define MIDI_NON_REALTIME             (0x7E)
#define MIDI_DEVICE_INQUIRY           (0x7F)          // all-call
#define MIDI_IDENTITY_REQUEST_SUB1    (0x06)
#define MIDI_IDENTITY_REQUEST_SUB2    (0x01)
#define MIDI_IDENTITY_REPLY_SUB2      (0x02)

// ── Graphics / layout constants (used by synthlibDefs / utilsGraphics) ───────
#define MENU_BAR_HEIGHT               (24.0)

// ── Colour macros ─────────────────────────────────────────────────────────────

#endif // __DEFS_H__
