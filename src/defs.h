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
#define ENABLE_DEBUG                  1
#define ENABLE_LOG_DEBUG              1

// Window
#define WINDOW_TITLE                  "EmuUtility"
#define TARGET_FRAME_BUFF_WIDTH       (2560)
#define TARGET_FRAME_BUFF_HEIGHT      (1200)

// LCD display geometry
#define LCD_WIDTH                     (240)
#define LCD_HEIGHT                    (64)
#define LCD_BYTES                     (LCD_WIDTH * LCD_HEIGHT / 8)          // 1920

// PEPTALK SysEx constants
#define EMU_MANUFACTURER_ID           (0x18)          // E-mu/Ensoniq
#define PEPTALK_DEST                  (0x7F)          // broadcast destination

// PEPTALK message types
#define PEPTALK_SESSION_OPEN          (0x10)
#define PEPTALK_SESSION_CLOSE         (0x11)
#define PEPTALK_BUTTON_EVENT          (0x40)
#define PEPTALK_ROTARY_EVENT          (0x43)
#define PEPTALK_LCD_DUMP_RESP         (0x50)
#define PEPTALK_LCD_DUMP_REQ          (0x51)
#define PEPTALK_LCD_DELTA_REQ         (0x52)
#define PEPTALK_LCD_DELTA_RESP        (0x53)
#define PEPTALK_LED_STATE_REQ         (0x60)
#define PEPTALK_LED_STATE_RESP        (0x61)
#define PEPTALK_SESSION_STATUS        (0x7F)

// E-mu EOS device family (E4, E5000, etc.)
#define EMU_EOS_FAMILY                (1025)

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
