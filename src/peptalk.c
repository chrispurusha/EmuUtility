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

#include "sysIncludes.h"
#include "defs.h"
#include "synthlibDefs.h"
#include "types.h"
#include "globalVars.h"
#include "midiComms.h"
#include "utils.h"     // get_time_ms(), for the delta-stream idle timer
#include "peptalk.h"

// SysEx frame: [F0, 18, 7F, device_id, seq_id, msg_type, ...data, F7]
#define PEPTALK_HDR_LEN       6
#define PEPTALK_FOOTER_LEN    1

// Returns the sequence id this message went out with. The device ECHOES that id back in byte 4 of
// its reply, which is what lets a reply be matched to the request that asked for it — see
// gLcdReqSeq. Requests we do not need to correlate simply ignore the return value.
static uint8_t send_peptalk(uint8_t msgType, const uint8_t * data, uint32_t dataLen) {
    uint32_t frameLen = PEPTALK_HDR_LEN + dataLen + PEPTALK_FOOTER_LEN;
    uint8_t  frame[256];

    if (frameLen > sizeof(frame)) {
        LOG_ERROR("PEPTALK frame too large (%u)\n", frameLen);
        return 0;
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
    return seqId;
}

// The sequence id the last LCD request went out with. Written and read on the MIDI thread only —
// it is handed straight to that thread's outstanding-request state, which owns the question of what
// is in flight. The device echoes the id back, so a reply carrying any other id belongs to a request
// we already gave up on, and applying its delta would corrupt the frame exactly as a spliced payload
// does. Matching on the protocol's own identifier rather than on arrival order means we are provably
// answering the right question.
static uint8_t gLastRequestSeq = 0;

uint8_t peptalk_last_request_seq(void) {
    return gLastRequestSeq;
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
    gLastRequestSeq = send_peptalk(PEPTALK_LCD_DUMP_REQ, NULL, 0);
    LOG_DEBUG("PEPTALK LCD dump request seq=%02X\n", (unsigned)gLastRequestSeq);
}

void peptalk_send_lcd_delta_request(void) {
    gLastRequestSeq = send_peptalk(PEPTALK_LCD_DELTA_REQ, NULL, 0);
    LOG_DEBUG("PEPTALK LCD delta request seq=%02X\n", (unsigned)gLastRequestSeq);
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

bool peptalk_apply_lcd_delta(const uint8_t * unpacked, uint32_t unpackedLen) {
    bool     flipping = false;
    bool     overran  = false;
    uint32_t bytePos  = 0;
    int      bitPos   = 7;

    for (uint32_t i = 0; i < unpackedLen; i++) {
        uint8_t run = unpacked[i];

        if (flipping) {
            for (uint8_t j = 0; j < run; j++) {
                if (bytePos < LCD_BYTES) {
                    gLcd.pixels[bytePos] ^= (uint8_t)(1 << bitPos);
                } else {
                    // A delta that runs off the end of the frame was computed against a different
                    // base than the one we hold — the clamp keeps it from corrupting memory, but the
                    // picture is now definitely wrong and only a full frame can fix it. Reported
                    // rather than silently swallowed: this is the one moment we can KNOW we are out
                    // of step, instead of waiting for the idle resync to find out.
                    overran = true;
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

    return !overran;
}

// ── Incoming message dispatch ─────────────────────────────────────────────────

// A reply has landed. Returns false when it belongs to a request we already gave up on — replies come
// back in the order the requests went out, so if anything is STILL outstanding after accounting for
// this one, this is the older reply and applying it would corrupt the frame.
// Does this reply answer the request that is actually outstanding? Asked of the MIDI thread, which
// owns that state; this thread only reads it. A reply carrying a different sequence id answers a
// request already abandoned (the timeout sends a second one without recalling the first), and its
// delta would XOR against a frame that has since moved on. Nothing here writes request state — the
// outcome goes back as a message so the owning thread decides what it means.
static bool lcd_reply_is_current(uint8_t replySeq) {
    uint8_t expected = 0;

    if (midi_outstanding_lcd_seq(&expected) && (replySeq != expected)) {
        LOG_ERROR("Discarding stale LCD reply: seq=%02X, expected %02X\n",
                  (unsigned)replySeq, (unsigned)expected);
        return false;
    }
    return true;
}

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
            gSessionOpen = true;
            midi_post_lcd_refresh(true);
            midi_post_led_refresh();
            synthlib_request_redraw();
            break;
        }

        case PEPTALK_LCD_DUMP_RESP:
        {
            // What this reply means for the REQUEST state is decided by the MIDI thread, which
            // owns it; this thread only fills in what it observed.
            tLcdReplyData reply    = {0};

            reply.seq = data[4];

            LOG_DEBUG("peptalk LCD 0x50 payloadLen=%u seq=%02X\n",
                      (unsigned)payloadLen, (unsigned)data[4]);

            if (payloadLen < 10) {
                // Cleared here too: this used to break out with the request still marked in flight,
                // so a runt reply stalled every further refresh until the 3 s timeout swept it up.
                reply.stale = !lcd_reply_is_current(data[4]);
                midi_post_lcd_reply(&reply);
                break;
            }
            uint8_t       tmp[LCD_BYTES + 16];
            uint32_t      unpacked = peptalk_unpack_7bit(payload + 10, payloadLen - 10, tmp, sizeof(tmp));

            LOG_DEBUG("peptalk LCD 0x50 unpacked=%u (full=%u)\n", (unsigned)unpacked, (unsigned)LCD_BYTES);

            if (!lcd_reply_is_current(data[4])) {
                reply.stale = true;
                midi_post_lcd_reply(&reply);
                break;
            }

            if (unpacked > LCD_BYTES) {
                // Longer than a whole frame, so it is neither: a full frame unpacks to EXACTLY
                // LCD_BYTES, and the device never sends a delta bigger than the frame it would
                // replace. The likeliest cause is a payload that arrived spliced — the SysEx
                // reassembly buffer in midiComms.c is shared by every connected MIDI source, so
                // traffic from another device on the rig lands in the middle of a transfer that
                // takes ~705 ms to arrive. Refetch rather than memcpy it over the pixels: this
                // branch used to be folded into the full-frame case by a `>=`, which is how
                // garbage reached the screen.
                LOG_ERROR("LCD payload unpacked to %u, longer than a frame (%u) — refetching\n",
                          (unsigned)unpacked, (unsigned)LCD_BYTES);
                reply.needsFullFrame = true;
            } else if (unpacked == LCD_BYTES) {
                // Full frame — replace pixels entirely. Hold gLcdMutex so the
                // UI thread can't snapshot a half-written buffer (torn frame).
                // Divergence check, and it costs nothing: we are about to overwrite the frame the
                // delta stream built, and here is the device's own copy of what that frame SHOULD
                // be. If they differ, the deltas got out of step — the exact failure that shows as
                // on-screen corruption sitting there until a full frame washes it away.
                //
                // Only meaningful once the display has settled: gLcdBaseTrusted false means deltas
                // have been applied since the last full frame, and the idle resync only fires after
                // LCD_RESYNC_IDLE_MS of quiet, by which time the trailing delta has long landed. A
                // difference at THAT point is a real defect, not a legitimate pending change.
                // gLcdLastDeltaMs is still zero if no delta has ever been applied, which is the
                // state at launch — where the buffer is blank and "differs" from the first real
                // frame for entirely innocent reasons.
                bool     wasDelta = !gLcdBaseTrusted && (gLcdLastDeltaMs > 0.0);
                uint32_t differed = 0;

                pthread_mutex_lock(&gLcdMutex);

                for (uint32_t i = 0; i < LCD_BYTES; i++) {
                    if (gLcd.pixels[i] != tmp[i]) {
                        differed++;
                    }
                }

                memcpy(gLcd.pixels, tmp, LCD_BYTES);
                gLcd.refresh++;
                pthread_mutex_unlock(&gLcdMutex);

                if (wasDelta && (differed > 0)) {
                    LOG_ERROR("DELTA DIVERGENCE: %u of %u bytes wrong before this full frame\n",
                              (unsigned)differed, (unsigned)LCD_BYTES);
                }
                gLcdBaseTrusted    = true;   // a whole frame: the delta stream is re-based on it
                reply.wasFullFrame = true;
                synthlib_request_redraw();
            } else if (unpacked > 0) {
                // Partial payload in the same message type — treat as a delta. The device answers a
                // delta REQUEST with 0x50 rather than 0x53 when it feels like it, so this branch is
                // the normal path for a button press, not an oddity.
                pthread_mutex_lock(&gLcdMutex);
                bool applied = peptalk_apply_lcd_delta(tmp, unpacked);
                gLcd.refresh++;
                pthread_mutex_unlock(&gLcdMutex);
                gLcdBaseTrusted = false;  // holds only while every delta since the last full frame landed
                gLcdLastDeltaMs = get_time_ms();

                if (!applied) {
                    LOG_ERROR("LCD delta overran the frame — re-basing immediately\n");
                    reply.needsFullFrame = true;
                }
                synthlib_request_redraw();
            }
            midi_post_lcd_reply(&reply);
            break;
        }

        case PEPTALK_LCD_DELTA_RESP:
        {
            tLcdReplyData reply    = {0};

            reply.seq = data[4];

            LOG_DEBUG("peptalk LCD 0x53 payloadLen=%u seq=%02X\n",
                      (unsigned)payloadLen, (unsigned)data[4]);

            if (!lcd_reply_is_current(data[4])) {
                reply.stale = true;
                midi_post_lcd_reply(&reply);
                break;
            }

            if (payloadLen < 10) {
                midi_post_lcd_reply(&reply);
                break;
            }
            uint8_t       tmp[LCD_BYTES + 16];
            uint32_t      unpacked = peptalk_unpack_7bit(payload + 10, payloadLen - 10, tmp, sizeof(tmp));

            LOG_DEBUG("peptalk LCD 0x53 unpacked=%u\n", (unsigned)unpacked);

            if (unpacked > 0) {
                pthread_mutex_lock(&gLcdMutex);
                bool applied = peptalk_apply_lcd_delta(tmp, unpacked);
                gLcd.refresh++;
                pthread_mutex_unlock(&gLcdMutex);
                gLcdBaseTrusted = false;
                gLcdLastDeltaMs = get_time_ms();

                if (!applied) {
                    LOG_ERROR("LCD delta overran the frame — re-basing immediately\n");
                    reply.needsFullFrame = true;
                }
                synthlib_request_redraw();
            }
            midi_post_lcd_reply(&reply);
            break;
        }

        case PEPTALK_LED_STATE_RESP:
        {
            if (payloadLen >= 2) {
                uint32_t leds = (uint32_t)(payload[1] << 7) | payload[0];
                gLeds = ~leds;
                synthlib_request_redraw();
            }
            break;
        }

        case PEPTALK_BUTTON_EVENT:
        {
            // A delta, for the same reason emu_button_press() asks for one; the idle resync in
            // midi_thread() is what guarantees the frame is eventually re-based.
            midi_post_lcd_refresh(false);
            synthlib_request_redraw();
            break;
        }

        case PEPTALK_ROTARY_EVENT:
        {
            midi_post_lcd_refresh(false);
            synthlib_request_redraw();
            break;
        }

        default:
            LOG_DEBUG("PEPTALK unknown message type 0x%02X\n", msgType);
            break;
    }
}
