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
// Notes: Docs/code-notes/defs.h.md - "// notes §k" refers there.

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

// notes §1
#define SESSION_PROBE_INTERVAL_MS    (100.0)

// E-mu EOS device family (E4, E5000, etc.)
#define EMU_EOS_FAMILY               (1025)

// notes §2
#define LCD_RESYNC_IDLE_MS    (4000.0)

// notes §3
#define LCD_USE_DELTAS    (0)

// notes §4
#define LCD_PROBE_WHEN_IDLE    (1)

// notes §5
#define LCD_IDLE_PROBE_MS    (150.0)

// notes §6
#define LCD_UNFOCUSED_PROBE_MS    (1000.0)

// notes §7
#define LCD_STREAM_GAP_MS    (400.0)

// notes §8
#define LCD_PRESS_SETTLE_MS    (0.0)

// notes §9
#define LCD_REQUEST_TIMEOUT_MS    (8000.0)

// notes §10
#define LCD_SETTLE_MS    (250.0)

// notes §11
#define LCD_CHASE_MAX    (8)

// notes §12
#define ROTARY_COALESCE_MS    (40.0)

// notes §13
#define SDS_HANDSHAKE_TIMEOUT_MS    (2500.0)

// notes §14
#define SDS_OPEN_LOOP_PACE_MS    (45.0)

// How long to wait for an acknowledgement before giving up on the whole transfer. Generous: the
// receiver is entitled to send WAIT and go off to do housekeeping for a while.
#define SDS_ACK_TIMEOUT_MS    (30000.0)

// notes §15
#define SDS_TAIL_GRACE_MS    (2500.0)

// notes §16
#define SDS_EMU_TAIL_PAD    (3)


// notes §17
#define EMU_MIDI_CHANNEL              (0)      // channel 1: note entry and any other channel message
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
