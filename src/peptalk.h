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

#ifndef __PEPTALK_H__
#define __PEPTALK_H__

#include "sysIncludes.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── Outgoing messages ────────────────────────────────────────────────────────

void peptalk_send_session_open(void);
void peptalk_send_session_close(void);
void peptalk_send_button_event(tButtonKey key, bool pressed);
void peptalk_send_rotary_event(int delta);
void peptalk_send_lcd_dump_request(void);
void peptalk_send_lcd_delta_request(void);
void peptalk_send_led_state_request(void);

// ── Incoming message handlers ────────────────────────────────────────────────

void peptalk_handle_message(const uint8_t * data, uint32_t length);

// ── LCD decode ───────────────────────────────────────────────────────────────

// Unpack MIDI 7-bit encoded payload into a byte buffer; returns number of bytes written.
uint32_t peptalk_unpack_7bit(const uint8_t * src, uint32_t srcLen, uint8_t * dst, uint32_t dstLen);

// Apply a delta (RLE XOR) update to gLcd.pixels.
// Applies an RLE-XOR delta to gLcd.pixels. Returns false if the delta ran off the end of the frame,
// which means it was computed against a base we no longer hold — the caller must then fetch a full
// frame, because nothing short of one can put the picture right. Caller holds gLcdMutex.
bool peptalk_apply_lcd_delta(const uint8_t * unpacked, uint32_t unpackedLen);

// The sequence id the last LCD request went out with. MIDI thread only — it hands this to the
// outstanding-request state so a reply can be matched to the request that asked for it.
uint8_t peptalk_last_request_seq(void);

#ifdef __cplusplus
}
#endif

#endif // __PEPTALK_H__
