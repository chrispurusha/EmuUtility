/*
 * The EmuUtility application.
 *
 * Copyright (C) 2025 Chris Turner <chris_purusha@icloud.com>
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

#include "sysIncludes.h"
#include "defs.h"
#include "types.h"
#include "globalVars.h"
#include "midiComms.h"
#include "peptalk.h"

// SysEx frame: [F0, 18, 7F, device_id, seq_id, msg_type, ...data, F7]
#define PEPTALK_HDR_LEN       6
#define PEPTALK_FOOTER_LEN    1

static void send_peptalk(uint8_t msgType, const uint8_t * data, uint32_t dataLen) {
    uint32_t frameLen = PEPTALK_HDR_LEN + dataLen + PEPTALK_FOOTER_LEN;
    uint8_t  frame[256];

    if (frameLen > sizeof(frame)) {
        LOG_ERROR("PEPTALK frame too large (%u)\n", frameLen);
        return;
    }
    uint8_t  seqId    = atomic_fetch_add(&gSessionSeqId, 1) & 0x7F;

    frame[0]            = MIDI_SYSEX_START;
    frame[1]            = EMU_MANUFACTURER_ID;
    frame[2]            = PEPTALK_DEST;
    frame[3]            = gDevice.id;
    frame[4]            = seqId;
    frame[5]            = msgType;

    if ((data != NULL) && (dataLen > 0)) {
        memcpy(&frame[6], data, dataLen);
    }
    frame[frameLen - 1] = MIDI_SYSEX_END;

    midi_send(frame, frameLen);
}

void peptalk_send_session_open(void) {
    LOG_DEBUG("PEPTALK session open\n");
    send_peptalk(PEPTALK_SESSION_OPEN, NULL, 0);
}

void peptalk_send_session_close(void) {
    LOG_DEBUG("PEPTALK session close\n");
    send_peptalk(PEPTALK_SESSION_CLOSE, NULL, 0);
}

void peptalk_send_button_event(tButtonKey key, bool pressed) {
    uint16_t k = (uint16_t)key;
    uint8_t  data[3];

    data[0] = k & 0x7F;
    data[1] = (k >> 7) & 0x7F;
    data[2] = pressed ? 1 : 0;
    send_peptalk(PEPTALK_BUTTON_EVENT, data, sizeof(data));
}

void peptalk_send_rotary_event(int delta) {
    uint16_t enc = (uint16_t)(delta & 0x3FFF);
    uint8_t  data[3];

    data[0] = 1;
    data[1] = enc & 0x7F;
    data[2] = (enc >> 7) & 0x7F;
    send_peptalk(PEPTALK_ROTARY_EVENT, data, sizeof(data));
}

void peptalk_send_lcd_dump_request(void) {
    LOG_DEBUG("PEPTALK LCD dump request\n");
    send_peptalk(PEPTALK_LCD_DUMP_REQ, NULL, 0);
}

void peptalk_send_lcd_delta_request(void) {
    send_peptalk(PEPTALK_LCD_DELTA_REQ, NULL, 0);
}

void peptalk_send_led_state_request(void) {
    send_peptalk(PEPTALK_LED_STATE_REQ, NULL, 0);
}

// ── 7-bit MIDI unpack ────────────────────────────────────────────────────────
// Each 7 input bytes encode 6 output bytes (MSB stripped per MIDI SysEx rule).
// The LCD payload uses this packing (7 bits per byte, MSB first within each group).

uint32_t peptalk_unpack_7bit(const uint8_t * src, uint32_t srcLen, uint8_t * dst, uint32_t dstLen) {
    uint32_t srcIdx = 0;
    uint32_t dstIdx = 0;
    int      bitBuf = 0;
    int      bitCnt = 0;

    while ((srcIdx < srcLen) && (dstIdx < dstLen)) {
        bitBuf  = (bitBuf << 7) | (src[srcIdx++] & 0x7F);
        bitCnt += 7;

        if (bitCnt >= 8) {
            bitCnt       -= 8;
            dst[dstIdx++] = (uint8_t)(bitBuf >> bitCnt);
            bitBuf       &= (1 << bitCnt) - 1;
        }
    }
    return dstIdx;
}

// ── LCD delta (RLE XOR) ──────────────────────────────────────────────────────
// Derived from the JS implementation in ctrl.mjs.
// The delta stream encodes runs of pixels to skip or flip using RLE.

void peptalk_apply_lcd_delta(const uint8_t * unpacked, uint32_t unpackedLen) {
    bool     flipping = false;
    uint32_t bytePos  = 0;
    int      bitPos   = 7;

    for (uint32_t i = 0; i < unpackedLen; i++) {
        uint8_t run = unpacked[i];

        if (flipping) {
            for (uint8_t j = 0; j < run; j++) {
                if (bytePos < LCD_BYTES) {
                    gLcd.pixels[bytePos] ^= (uint8_t)(1 << bitPos);
                }

                if (--bitPos < 0) {
                    bitPos = 7;
                    bytePos++;
                }
            }
        } else {
            uint32_t skipBits = run >> 3;
            int      skipFrac = run & 0x07;

            bytePos += skipBits;
            bitPos  -= skipFrac;

            if (bitPos < 0) {
                bytePos++;
                bitPos += 8;
            }
        }

        if (run != 255) {
            flipping = !flipping;
        }
    }
}

// ── Incoming message dispatch ─────────────────────────────────────────────────

void peptalk_handle_message(const uint8_t * data, uint32_t length) {
    LOG_DEBUG("peptalk rx length=%u hdr: %02X %02X %02X %02X %02X %02X\n",
              (unsigned)length,
              (length > 0) ? data[0] : 0xFF,
              (length > 1) ? data[1] : 0xFF,
              (length > 2) ? data[2] : 0xFF,
              (length > 3) ? data[3] : 0xFF,
              (length > 4) ? data[4] : 0xFF,
              (length > 5) ? data[5] : 0xFF);

    if (length < 7) {
        LOG_DEBUG("peptalk rx too short\n");
        return;
    }

    if ((data[0] != MIDI_SYSEX_START) || (data[length - 1] != MIDI_SYSEX_END)) {
        LOG_DEBUG("peptalk rx bad framing\n");
        return;
    }

    if (data[1] != EMU_MANUFACTURER_ID) {
        LOG_DEBUG("peptalk rx mfr 0x%02X != 0x%02X\n", data[1], EMU_MANUFACTURER_ID);
        return;
    }

    if (data[2] != PEPTALK_DEST) {
        LOG_DEBUG("peptalk rx dest 0x%02X != 0x%02X — passing through\n", data[2], PEPTALK_DEST);
    }
    uint8_t         msgType    = data[5];
    const uint8_t * payload    = &data[6];
    uint32_t        payloadLen = length - 7; // strip header (6) + footer (1)

    switch (msgType) {
        case PEPTALK_SESSION_STATUS:
        {
            LOG_DEBUG("PEPTALK session status\n");
            atomic_store(&gSessionOpen, true);
            atomic_store(&gNeedLcdFull, true);
            atomic_store(&gNeedLeds, true);
            atomic_store(&gReDraw, true);
            break;
        }

        case PEPTALK_LCD_DUMP_RESP:
        {
            LOG_DEBUG("peptalk LCD 0x50 payloadLen=%u\n", (unsigned)payloadLen);

            if (payloadLen < 10) {
                break;
            }
            uint8_t  tmp[LCD_BYTES + 16];
            uint32_t unpacked = peptalk_unpack_7bit(payload + 10, payloadLen - 10, tmp, sizeof(tmp));

            LOG_DEBUG("peptalk LCD 0x50 unpacked=%u (full=%u)\n", (unsigned)unpacked, (unsigned)LCD_BYTES);

            atomic_store(&gLcdPending, false);

            if (unpacked >= LCD_BYTES) {
                // Full frame — replace pixels entirely
                memcpy(gLcd.pixels, tmp, LCD_BYTES);
                gLcd.refresh++;
                atomic_store(&gNeedLcdFull, false);
                atomic_store(&gNeedLcdDelta, false);
                atomic_store(&gReDraw, true);
            } else if (unpacked > 0) {
                // Partial payload in same message type — treat as delta
                peptalk_apply_lcd_delta(tmp, unpacked);
                gLcd.refresh++;
                atomic_store(&gNeedLcdDelta, false);
                atomic_store(&gReDraw, true);
            }
            break;
        }

        case PEPTALK_LCD_DELTA_RESP:
        {
            LOG_DEBUG("peptalk LCD 0x53 payloadLen=%u\n", (unsigned)payloadLen);

            atomic_store(&gLcdPending, false);

            if (payloadLen < 10) {
                break;
            }
            uint8_t  tmp[LCD_BYTES + 16];
            uint32_t unpacked = peptalk_unpack_7bit(payload + 10, payloadLen - 10, tmp, sizeof(tmp));

            LOG_DEBUG("peptalk LCD 0x53 unpacked=%u\n", (unsigned)unpacked);

            if (unpacked > 0) {
                peptalk_apply_lcd_delta(tmp, unpacked);
                gLcd.refresh++;
                atomic_store(&gNeedLcdDelta, false);
                atomic_store(&gReDraw, true);
            }
            break;
        }

        case PEPTALK_LED_STATE_RESP:
        {
            if (payloadLen >= 2) {
                uint32_t leds = (uint32_t)(payload[1] << 7) | payload[0];
                atomic_store(&gLeds, ~leds);
                atomic_store(&gNeedLeds, false);
                atomic_store(&gReDraw, true);
            }
            break;
        }

        case PEPTALK_BUTTON_EVENT:
        {
            // Echo from device of a button state change — trigger LCD poll
            atomic_store(&gNeedLcdDelta, true);
            atomic_store(&gReDraw, true);
            break;
        }

        case PEPTALK_ROTARY_EVENT:
        {
            atomic_store(&gNeedLcdDelta, true);
            atomic_store(&gReDraw, true);
            break;
        }

        default:
            LOG_DEBUG("PEPTALK unknown message type 0x%02X\n", msgType);
            break;
    }
}
