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

#ifndef __SAMPLE_DUMP_H__
#define __SAMPLE_DUMP_H__

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// MIDI Sample Dump Standard (MMA/JMSC, January 1986) — the published, non-proprietary way to move
// sample data over MIDI. It is the only route into this sampler over the link we already have: SMDI
// is faster but needs SCSI and the Emulator is a slave that cannot initiate, and the E-mu
// editor/librarian SysEx was never published.
//
// Wire format, for reference (cc = channel, ss ss = sample number LSB first, pp = packet number):
//   DUMP REQUEST  F0 7E cc 03 ss ss F7
//   DUMP HEADER   F0 7E cc 01 ss ss ee ff ff ff gg gg gg hh hh hh ii ii ii jj F7
//   DATA PACKET   F0 7E cc 02 pp <120 data bytes> <checksum> F7      (127 bytes total)
//   ACK  7F / NAK 7E / CANCEL 7D / WAIT 7C, each F0 7E cc <type> pp F7
//
// Every multi-byte value is three 7-bit bytes, LSB first. Sample data is left-justified in each
// 7-bit byte, MSB first across the bytes of a word. The checksum is the running XOR of everything
// after the F0 up to but excluding the checksum itself.

#define SDS_PACKET_DATA_BYTES    120                       // by the standard; 127 bytes on the wire
#define SDS_WORD_BYTES           3                         // 16-bit sample -> three 7-bit bytes
#define SDS_WORDS_PER_PACKET     (SDS_PACKET_DATA_BYTES / SDS_WORD_BYTES)

// Hard limits imposed by the standard's own field widths — 21 bits each, being three 7-bit bytes.
#define SDS_MAX_WORDS            (2097151U)                // sample length field
#define SDS_MAX_PERIOD_NS        (2097151U)                // sample period field
#define SDS_MIN_SAMPLE_RATE      (478U)                    // any slower and the period will not fit
#define SDS_MAX_SAMPLE_NUMBER    (16383U)                  // two 7-bit bytes

// A sample ready to send: always mono 16-bit, whatever the file held.
typedef struct {
    int16_t * samples;        // frameCount entries, owned by this struct
    uint32_t  frameCount;
    uint32_t  sampleRate;
    uint32_t  loopStart;
    uint32_t  loopEnd;
    bool      hasLoop;
    bool      loopAlternating;
    // What the file actually contained, kept for the confirmation the user sees before committing
    // to a transfer measured in minutes.
    uint16_t  srcBits;
    uint16_t  srcChannels;
    char      sourceName[64];
} tSampleDump;

// Read a .wav into a tSampleDump, converting to mono 16-bit, and check it can actually be sent.
// Returns false with a human-readable reason in `err` — the caller shows that rather than starting a
// transfer that would fail minutes in. `channel` picks a side of a stereo file (0 = left).
bool sample_dump_load_wav(const char * path, uint32_t channel, tSampleDump * out, char * err, size_t errMax);

void sample_dump_free(tSampleDump * dump);

// Roughly how long sending this will take on a 31250-baud MIDI link, in seconds. Measured rate on
// real hardware is ~3125 bytes/s, and a 127-byte packet carries 40 words, so this is not far off.
double sample_dump_estimate_seconds(const tSampleDump * dump);

// ── Wire encoding (pure, no I/O — the transfer state machine in midiComms drives these) ─────────

// Builds the DUMP HEADER into `frame`, which must hold at least 21 bytes. Returns the length used.
uint32_t sample_dump_build_header(const tSampleDump * dump, uint8_t channel, uint16_t sampleNumber, uint8_t * frame);

// Builds data packet `packetIndex` into `frame`, which must hold at least 127 bytes. Returns the
// length used, or 0 once every word has been sent.
uint32_t sample_dump_build_packet(const tSampleDump * dump, uint8_t channel, uint32_t packetIndex, uint8_t * frame);

// How many data packets this sample needs.
uint32_t sample_dump_packet_count(const tSampleDump * dump);

// ── Receiving ────────────────────────────────────────────────────────────────

// Builds a DUMP REQUEST — the message that asks the sampler to send us a sample. `frame` must hold
// at least 7 bytes. Non-destructive: nothing on the device changes.
uint32_t sample_dump_build_request(uint8_t channel, uint16_t sampleNumber, uint8_t * frame);

// Builds one of the handshake replies (0x7F ACK, 0x7E NAK, 0x7D CANCEL, 0x7C WAIT) into `frame`,
// which must hold at least 6 bytes.
uint32_t sample_dump_build_handshake(uint8_t channel, uint8_t type, uint8_t packet, uint8_t * frame);

// Verifies a received 127-byte data packet and unpacks its 40 words. Returns false if the framing or
// the running-XOR checksum is wrong, in which case the caller should NAK and let it be resent.
bool sample_dump_decode_packet(const uint8_t * frame, uint32_t length, int16_t * wordsOut, uint8_t * packetNumberOut);

// Writes 16-bit mono PCM out as a .wav, including a 'smpl' chunk when the dump carried loop points.
bool sample_dump_write_wav(const char * path, const int16_t * samples, uint32_t count, uint32_t rate, bool hasLoop, uint32_t loopStart, uint32_t loopEnd, char * err, size_t errMax);

#ifdef __cplusplus
}
#endif

#endif // __SAMPLE_DUMP_H__
