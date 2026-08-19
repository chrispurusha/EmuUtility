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

#ifdef __cplusplus
extern "C" {
#endif

#include "sysIncludes.h"
#include "defs.h"
#include "synthlibDefs.h"
#include "types.h"
#include "sampleDump.h"

// ── Little-endian readers ────────────────────────────────────────────────────
// WAV is little-endian regardless of host, so the bytes are assembled explicitly rather than cast.

static uint32_t rd32(const uint8_t * p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t rd16(const uint8_t * p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// Triangular (TPDF) dither, +/-1 LSB, from a small deterministic generator.
//
// Deterministic on purpose: the same file converts to the same bytes every time, so a transfer can
// be diffed against a previous one. A hardware RNG would make every send of the same sample differ.
static int32_t tpdf_dither(void) {
    static uint32_t state = 0x1234567u;
    int32_t         a;
    int32_t         b;

    state = (state * 1664525u) + 1013904223u;
    a     = (int32_t)((state >> 16) & 0xFF);
    state = (state * 1664525u) + 1013904223u;
    b     = (int32_t)((state >> 16) & 0xFF);
    // Two independent uniform values summed gives a triangular distribution, which is what avoids
    // the noise floor modulating with the signal the way flat dither does.
    return ((a - b) * 256) / 255;   // roughly -256..+256, i.e. +/-1 LSB at 24-bit scale
}

static int16_t clamp16(int32_t v) {
    if (v > 32767) {
        return 32767;
    }

    if (v < -32768) {
        return -32768;
    }
    return (int16_t)v;
}

// One sample of any supported width, normalised to signed 16-bit.
//
// The guiding rule is that anything ALREADY in the target format passes through untouched — a 16-bit
// file is sent bit for bit, with no rounding, no dither and no resampling to go wrong. Conversion
// happens only where the source genuinely is not what the wire carries.
static int16_t sample_to_16(const uint8_t * p, uint16_t bits) {
    switch (bits) {
        case 8:
            // 8-bit WAV is UNSIGNED by the spec, unlike every wider width. Getting this wrong
            // offsets the whole sample by half full scale rather than failing visibly. Widening is
            // exact, so there is nothing to round.
            return (int16_t)(((int32_t)p[0] - 128) << 8);

        case 16:
            return (int16_t)rd16(p);   // already the target format: untouched

        case 24:
        {
            // Reducing depth, so round to nearest with dither rather than truncating. Truncation
            // biases every sample toward zero and turns quantisation error into harmonic
            // distortion that correlates with the signal; dithered rounding turns it into a steady
            // low-level hiss instead, which is the standard trade and much easier on the ear.
            int32_t v = (int32_t)(((uint32_t)p[2] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[0] << 8));

            return clamp16((v + tpdf_dither() + 32768) >> 16);
        }

        case 32:
        {
            int32_t v = (int32_t)((uint32_t)rd16(p) | ((uint32_t)rd16(p + 2) << 16));

            return clamp16((v + tpdf_dither() + 32768) >> 16);
        }

        default:
            return 0;
    }
}

// ── WAV loading ──────────────────────────────────────────────────────────────

bool sample_dump_load_wav(const char * path, uint32_t channel, tSampleDump * out, char * err, size_t errMax) {
    if ((path == NULL) || (out == NULL)) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    FILE *          file    = fopen(path, "rb");

    if (file == NULL) {
        snprintf(err, errMax, "Cannot open the file.");
        return false;
    }
    fseek(file, 0, SEEK_END);
    long            fileLen = ftell(file);
    fseek(file, 0, SEEK_SET);

    if ((fileLen < 44) || (fileLen > (64 * 1024 * 1024))) {
        snprintf(err, errMax, "Not a usable .wav (%ld bytes).", fileLen);
        fclose(file);
        return false;
    }
    uint8_t *       buf     = (uint8_t *)malloc((size_t)fileLen);

    if (buf == NULL) {
        snprintf(err, errMax, "Out of memory reading the file.");
        fclose(file);
        return false;
    }
    size_t          got     = fread(buf, 1, (size_t)fileLen, file);

    fclose(file);

    if (got != (size_t)fileLen) {
        snprintf(err, errMax, "Could not read the whole file.");
        free(buf);
        return false;
    }

    if ((memcmp(buf, "RIFF", 4) != 0) || (memcmp(buf + 8, "WAVE", 4) != 0)) {
        snprintf(err, errMax, "Not a RIFF/WAVE file.");
        free(buf);
        return false;
    }
    // Walk the chunks rather than assuming the canonical 44-byte layout: real files carry LIST,
    // fact and smpl chunks in any order, and a fixed offset would silently read the wrong bytes.
    const uint8_t * fmt     = NULL;
    uint32_t        fmtLen  = 0;
    const uint8_t * data    = NULL;
    uint32_t        dataLen = 0;
    const uint8_t * smpl    = NULL;
    uint32_t        smplLen = 0;
    uint32_t        at      = 12;

    while ((at + 8) <= (uint32_t)fileLen) {
        uint32_t        chunkLen = rd32(buf + at + 4);
        const uint8_t * body     = buf + at + 8;

        if ((at + 8 + chunkLen) > (uint32_t)fileLen) {
            chunkLen = (uint32_t)fileLen - at - 8;   // truncated final chunk; use what is there
        }

        if (memcmp(buf + at, "fmt ", 4) == 0) {
            fmt    = body;
            fmtLen = chunkLen;
        } else if (memcmp(buf + at, "data", 4) == 0) {
            data    = body;
            dataLen = chunkLen;
        } else if (memcmp(buf + at, "smpl", 4) == 0) {
            smpl    = body;
            smplLen = chunkLen;
        }
        at += 8 + chunkLen + (chunkLen & 1);   // chunks are word-aligned
    }

    if ((fmt == NULL) || (fmtLen < 16) || (data == NULL) || (dataLen == 0)) {
        snprintf(err, errMax, "Missing 'fmt ' or 'data' chunk.");
        free(buf);
        return false;
    }
    uint16_t format   = rd16(fmt + 0);
    uint16_t channels = rd16(fmt + 2);
    uint32_t rate     = rd32(fmt + 4);
    uint16_t bits     = rd16(fmt + 14);

    // WAVE_FORMAT_EXTENSIBLE keeps the real format in a sub-GUID whose first two bytes are the
    // format tag; anything else here is compressed and there is nothing sensible to send.
    if ((format == 0xFFFE) && (fmtLen >= 40)) {
        format = rd16(fmt + 24);
    }

    if (format != 1) {
        snprintf(err, errMax, "Only uncompressed PCM is supported (this file is format %u).", format);
        free(buf);
        return false;
    }

    if ((bits != 8) && (bits != 16) && (bits != 24) && (bits != 32)) {
        snprintf(err, errMax, "Unsupported bit depth: %u. Use 8, 16, 24 or 32-bit PCM.", bits);
        free(buf);
        return false;
    }

    if ((channels < 1) || (channels > 2)) {
        snprintf(err, errMax, "Only mono or stereo files can be sent (this has %u channels).", channels);
        free(buf);
        return false;
    }

    // The sample PERIOD goes on the wire as three 7-bit bytes — 21 bits of nanoseconds — so a slow
    // enough rate simply cannot be expressed. Worth checking here rather than sending a header the
    // sampler would interpret as some other rate entirely.
    if ((rate < SDS_MIN_SAMPLE_RATE) || (rate > 1000000U)) {
        snprintf(err, errMax, "Sample rate %u Hz cannot be sent. The Sample Dump Standard carries "
                 "the sample period in 21 bits, so rates below %u Hz do not fit.",
                 rate, SDS_MIN_SAMPLE_RATE);
        free(buf);
        return false;
    }
    uint32_t  frameBytes = (uint32_t)(bits / 8) * channels;
    uint32_t  frames     = dataLen / frameBytes;

    if (frames == 0) {
        snprintf(err, errMax, "The file contains no audio.");
        free(buf);
        return false;
    }

    // The length field is 21 bits too, which caps a single dump at about 47 seconds of 44.1 kHz
    // audio however much memory either end has.
    if (frames > SDS_MAX_WORDS) {
        snprintf(err, errMax, "Too long: %u samples. The Sample Dump Standard caps one transfer at "
                 "%u samples (%.1f seconds at %u Hz).",
                 frames, SDS_MAX_WORDS, (double)SDS_MAX_WORDS / (double)rate, rate);
        free(buf);
        return false;
    }
    int16_t * pcm        = (int16_t *)malloc((size_t)frames * sizeof(int16_t));

    if (pcm == NULL) {
        snprintf(err, errMax, "Out of memory converting the sample.");
        free(buf);
        return false;
    }
    uint32_t  take       = (channel < channels) ? channel : 0;

    for (uint32_t i = 0; i < frames; i++) {
        pcm[i] = sample_to_16(data + (size_t)i * frameBytes + ((size_t)take * (bits / 8)), bits);
    }

    out->samples     = pcm;
    out->frameCount  = frames;
    out->sampleRate  = rate;
    out->srcBits     = bits;
    out->srcChannels = channels;

    // Loop points, if the file carries a 'smpl' chunk. Layout: 28 bytes of header then 24 bytes per
    // loop, with the type at +0 and start/end at +8/+12.
    if ((smpl != NULL) && (smplLen >= 36) && (rd32(smpl + 28) > 0)) {
        const uint8_t * loop = smpl + 36;

        if (smplLen >= (36 + 24)) {
            uint32_t type  = rd32(loop + 4);
            uint32_t start = rd32(loop + 8);
            uint32_t end   = rd32(loop + 12);

            if ((start < frames) && (end < frames) && (end > start)) {
                out->hasLoop         = true;
                out->loopStart       = start;
                out->loopEnd         = end;
                out->loopAlternating = (type == 1);
            }
        }
    }
    const char * base = strrchr(path, '/');

    snprintf(out->sourceName, sizeof(out->sourceName), "%s", (base != NULL) ? (base + 1) : path);
    free(buf);
    return true;
}

void sample_dump_free(tSampleDump * dump) {
    if ((dump != NULL) && (dump->samples != NULL)) {
        free(dump->samples);
        dump->samples    = NULL;
        dump->frameCount = 0;
    }
}

// The length actually put on the wire: the sample, plus the tail padding this device is going to
// discard. See SDS_EMU_TAIL_PAD in defs.h for the measurements behind it.
static uint32_t sample_dump_wire_words(const tSampleDump * dump) {
    return dump->frameCount + SDS_EMU_TAIL_PAD;
}

double sample_dump_estimate_seconds(const tSampleDump * dump) {
    if ((dump == NULL) || (dump->frameCount == 0)) {
        return 0.0;
    }
    // Every 40 words travel as one 127-byte packet, and the header and per-packet acknowledgements
    // are lost in the rounding at this scale. 3125 bytes/s is the measured rate of this link.
    double packets = ceil((double)dump->frameCount / (double)SDS_WORDS_PER_PACKET);

    return (packets * 127.0) / 3125.0;
}

uint32_t sample_dump_packet_count(const tSampleDump * dump) {
    if ((dump == NULL) || (dump->frameCount == 0)) {
        return 0;
    }
    return (sample_dump_wire_words(dump) + SDS_WORDS_PER_PACKET - 1) / SDS_WORDS_PER_PACKET;
}

// ── Wire encoding ────────────────────────────────────────────────────────────

// Three 7-bit bytes, least significant first — the standard's encoding for every multi-byte field.
static void put21(uint8_t * p, uint32_t value) {
    p[0] = (uint8_t)(value & 0x7F);
    p[1] = (uint8_t)((value >> 7) & 0x7F);
    p[2] = (uint8_t)((value >> 14) & 0x7F);
}

uint32_t sample_dump_build_header(const tSampleDump * dump, uint8_t channel, uint16_t sampleNumber,
                                  uint8_t * frame) {
    uint32_t period    = (uint32_t)((1000000000.0 / (double)dump->sampleRate) + 0.5);

    if (period > SDS_MAX_PERIOD_NS) {
        period = SDS_MAX_PERIOD_NS;   // rejected at load time; clamped so a stray call cannot corrupt the field
    }
    // No loop is signalled by type 0x7F. The two loop-point fields still have to carry something
    // legal, so they describe the whole sample; a receiver honouring 0x7F ignores them anyway.
    uint32_t loopStart = dump->hasLoop ? dump->loopStart : 0;
    uint32_t loopEnd   = dump->hasLoop ? dump->loopEnd : ((dump->frameCount > 0) ? (dump->frameCount - 1) : 0);

    frame[0]  = MIDI_SYSEX_START;
    frame[1]  = 0x7E;                       // universal non-realtime
    frame[2]  = channel;
    frame[3]  = 0x01;                       // dump header
    frame[4]  = (uint8_t)(sampleNumber & 0x7F);
    frame[5]  = (uint8_t)((sampleNumber >> 7) & 0x7F);
    frame[6]  = 16;                                  // significant bits per word
    put21(&frame[7], period);
    put21(&frame[10], sample_dump_wire_words(dump)); // includes the tail the device will discard
    put21(&frame[13], loopStart);
    put21(&frame[16], loopEnd);
    frame[19] = dump->hasLoop ? (dump->loopAlternating ? 0x01 : 0x00) : 0x7F;
    frame[20] = MIDI_SYSEX_END;
    return 21;
}

uint32_t sample_dump_build_packet(const tSampleDump * dump, uint8_t channel, uint32_t packetIndex,
                                  uint8_t * frame) {
    uint32_t first = packetIndex * SDS_WORDS_PER_PACKET;
    uint32_t total = sample_dump_wire_words(dump);

    if (first >= total) {
        return 0;
    }
    frame[0] = MIDI_SYSEX_START;
    frame[1] = 0x7E;
    frame[2] = channel;
    frame[3] = 0x02;                                  // data packet
    frame[4] = (uint8_t)(packetIndex & 0x7F);         // wraps at 7F by the standard

    // Always a full 120 data bytes even on the last packet: the standard fixes the packet size, and
    // the header's length field is what tells the receiver where the sample really ends. Short
    // packets are a common way to get a dump rejected.
    for (uint32_t w = 0; w < SDS_WORDS_PER_PACKET; w++) {
        uint32_t  idx   = first + w;
        // Past the real audio: hold the final value across the padding words, then zero to fill out
        // the packet. Holding avoids welding a step discontinuity — a click — onto the sample's end.
        uint16_t  value = 0;
        uint8_t * dst   = &frame[5 + (w * SDS_WORD_BYTES)];

        if (idx < dump->frameCount) {
            value = (uint16_t)dump->samples[idx];
        } else if ((idx < total) && (dump->frameCount > 0)) {
            value = (uint16_t)dump->samples[dump->frameCount - 1];
        }
        // Left-justified across three 7-bit bytes, most significant first: 0x87E5 becomes
        // 1000011 1111001 0100000.
        dst[0] = (uint8_t)((value >> 9) & 0x7F);
        dst[1] = (uint8_t)((value >> 2) & 0x7F);
        dst[2] = (uint8_t)((value << 5) & 0x7F);
    }

    uint8_t checksum = 0;

    for (uint32_t i = 1; i < (5 + SDS_PACKET_DATA_BYTES); i++) {
        checksum ^= frame[i];        // running XOR of everything after the F0
    }

    frame[5 + SDS_PACKET_DATA_BYTES]     = (uint8_t)(checksum & 0x7F);
    frame[5 + SDS_PACKET_DATA_BYTES + 1] = MIDI_SYSEX_END;
    return 5 + SDS_PACKET_DATA_BYTES + 2;   // 127
}

// ── Receiving ────────────────────────────────────────────────────────────────

uint32_t sample_dump_build_request(uint8_t channel, uint16_t sampleNumber, uint8_t * frame) {
    frame[0] = MIDI_SYSEX_START;
    frame[1] = 0x7E;
    frame[2] = channel;
    frame[3] = 0x03;                                   // dump request
    frame[4] = (uint8_t)(sampleNumber & 0x7F);
    frame[5] = (uint8_t)((sampleNumber >> 7) & 0x7F);
    frame[6] = MIDI_SYSEX_END;
    return 7;
}

uint32_t sample_dump_build_handshake(uint8_t channel, uint8_t type, uint8_t packet, uint8_t * frame) {
    frame[0] = MIDI_SYSEX_START;
    frame[1] = 0x7E;
    frame[2] = channel;
    frame[3] = type;
    frame[4] = (uint8_t)(packet & 0x7F);
    frame[5] = MIDI_SYSEX_END;
    return 6;
}

bool sample_dump_decode_packet(const uint8_t * frame, uint32_t length, int16_t * wordsOut,
                               uint8_t * packetNumberOut) {
    if (  (length != 127)
       || (frame[0] != MIDI_SYSEX_START) || (frame[126] != MIDI_SYSEX_END)
       || (frame[1] != 0x7E) || (frame[3] != 0x02)) {
        return false;
    }
    uint8_t checksum = 0;

    for (uint32_t i = 1; i < (5 + SDS_PACKET_DATA_BYTES); i++) {
        checksum ^= frame[i];
    }

    if ((checksum & 0x7F) != frame[5 + SDS_PACKET_DATA_BYTES]) {
        return false;   // corrupt on the wire; the caller NAKs and the sender repeats it
    }

    for (uint32_t w = 0; w < SDS_WORDS_PER_PACKET; w++) {
        const uint8_t * src   = &frame[5 + (w * SDS_WORD_BYTES)];
        // The inverse of the left-justified MSB-first packing used when sending.
        uint16_t        value = (uint16_t)(((uint16_t)src[0] << 9) | ((uint16_t)src[1] << 2) | (src[2] >> 5));

        wordsOut[w] = (int16_t)value;
    }

    if (packetNumberOut != NULL) {
        *packetNumberOut = frame[4];
    }
    return true;
}

// ── WAV writing ──────────────────────────────────────────────────────────────

static void wr32(uint8_t * p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void wr16(uint8_t * p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

bool sample_dump_write_wav(const char * path, const int16_t * samples, uint32_t count, uint32_t rate,
                           bool hasLoop, uint32_t loopStart, uint32_t loopEnd,
                           char * err, size_t errMax) {
    FILE *   file      = fopen(path, "wb");

    if (file == NULL) {
        snprintf(err, errMax, "Cannot write to %s", path);
        return false;
    }
    uint32_t dataBytes = count * 2;
    uint32_t smplBytes = hasLoop ? (36 + 24) : 0;
    uint32_t riffLen   = 4 + (8 + 16) + (hasLoop ? (8 + smplBytes) : 0) + 8 + dataBytes;
    uint8_t  hdr[12];

    memcpy(hdr, "RIFF", 4);
    wr32(hdr + 4, riffLen);
    memcpy(hdr + 8, "WAVE", 4);
    fwrite(hdr, 1, 12, file);

    uint8_t  fmt[24];

    memcpy(fmt, "fmt ", 4);
    wr32(fmt + 4, 16);
    wr16(fmt + 8, 1);                       // PCM
    wr16(fmt + 10, 1);                      // mono
    wr32(fmt + 12, rate);
    wr32(fmt + 16, rate * 2);               // byte rate
    wr16(fmt + 20, 2);                      // block align
    wr16(fmt + 22, 16);                     // bits
    fwrite(fmt, 1, 24, file);

    if (hasLoop) {
        uint8_t smpl[8 + 36 + 24];

        memset(smpl, 0, sizeof(smpl));
        memcpy(smpl, "smpl", 4);
        wr32(smpl + 4, smplBytes);
        wr32(smpl + 8 + 12, (uint32_t)(1000000000.0 / (double)rate + 0.5));   // sample period
        wr32(smpl + 8 + 16, 60);                                              // MIDI unity note
        wr32(smpl + 8 + 28, 1);                                               // one loop
        wr32(smpl + 8 + 36 + 8, loopStart);
        wr32(smpl + 8 + 36 + 12, loopEnd);
        fwrite(smpl, 1, sizeof(smpl), file);
    }
    uint8_t dataHdr[8];

    memcpy(dataHdr, "data", 4);
    wr32(dataHdr + 4, dataBytes);
    fwrite(dataHdr, 1, 8, file);

    // Written a sample at a time so the file is little-endian whatever the host is.
    for (uint32_t i = 0; i < count; i++) {
        uint8_t s16[2];

        wr16(s16, (uint16_t)samples[i]);
        fwrite(s16, 1, 2, file);
    }

    fclose(file);
    return true;
}

#ifdef __cplusplus
}
#endif
