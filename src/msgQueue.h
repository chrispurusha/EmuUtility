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
// Notes: Docs/code-notes/msgQueue.h.md - "// notes §k" refers there.

#ifndef __MSG_QUEUE_H__
#define __MSG_QUEUE_H__

#include "sysIncludes.h"
#include "types.h"
#include "sampleDump.h"
#include "synthlibQueue.h" // generic queue mechanism: tMessageQueue / eRcv / msg_init / msg_send / ...

// notes §1
typedef enum {
    eMsgCmdScanDevices,    // rescan CoreMIDI and re-identify (menu action, sleep/wake, setup change)
    eMsgCmdIdentityReply,  // identityReplyData: an identity reply seen by the CoreMIDI read callback
    eMsgCmdSessionOpen,
    eMsgCmdSessionStatus,  // sessionStatusData: the device answered a session open
    eMsgCmdButtonEvent,    // buttonEventData
    eMsgCmdRotaryEvent,    // rotaryEventData
    eMsgCmdNoteEvent,      // noteEventData: a MIDI note from the computer-keyboard note entry
    eMsgCmdLcdRefresh,     // lcdRefreshData: somebody wants the display re-read
    eMsgCmdLcdReply,       // lcdReplyData: a reply landed; only the MIDI thread may act on that
    eMsgCmdUiActivity,     // the user touched something; starts the settle timer
    eMsgCmdSdsStart,       // sdsStartData: begin a Sample Dump Standard transfer to the device
    eMsgCmdSdsHandshake,   // sdsHandshakeData: the receiver answered ACK/NAK/WAIT/CANCEL
    eMsgCmdSdsCancel,      // abandon the transfer in progress
    eMsgCmdSdsRequest,     // sdsRequestData: ask the device to send us a sample (non-destructive)
    eMsgCmdSdsRxFrame      // sdsRxFrameData: a dump header or data packet arrived from the device
} eMsgCmd;

// Posted by the CoreMIDI read callback, acted on by the MIDI thread: the callback only validates and
// unpacks the reply, the MIDI thread does the entity/destination lookup and takes ownership of the
// resulting connection.
typedef struct {
    uint32_t source;   // MIDIEndpointRef the reply arrived on
    uint8_t  deviceId;
    uint16_t family;
    uint16_t member;
} tIdentityReplyData;

// notes §2
typedef struct {
    uint8_t seq;
} tSessionStatusData;

typedef struct {
    uint32_t key;      // tButtonKey
    bool     pressed;
} tButtonEventData;

typedef struct {
    int32_t delta;
} tRotaryEventData;

typedef struct {
    uint8_t note;
    uint8_t velocity;
    bool    on;
} tNoteEventData;

// Posted by anyone who wants the display re-read. It is a REQUEST, not a flag write: the MIDI thread
// keeps the actual want-bits, so N of these still collapse into one transfer while nothing can lose
// an update the way a boolean set by one thread and cleared by another can.
typedef struct {
    bool full;     // a whole frame is needed
    bool delta;    // a delta will do
    bool leds;     // the LED state is wanted too
} tLcdRefreshData;

// notes §3
typedef struct {
    tSampleDump dump;
    uint16_t    sampleNumber;
    uint8_t     channel;
} tSdsStartData;

typedef struct {
    uint8_t type;      // 0x7F ACK, 0x7E NAK, 0x7D CANCEL, 0x7C WAIT
    uint8_t packet;
} tSdsHandshakeData;

typedef struct {
    uint16_t sampleNumber;
    char     path[400];     // where to write the .wav once it has all arrived
} tSdsRequestData;

// The raw frame, handed over whole: verifying the checksum and deciding whether to ACK or NAK is the
// MIDI thread's business, because it owns the transfer state and is the only thread that may send.
typedef struct {
    uint32_t length;
    uint8_t  data[132];
} tSdsRxFrameData;

typedef struct {
    uint8_t seq;             // the sequence id the reply carried
    bool    stale;           // did not match the outstanding request; pixels untouched
    bool    wasFullFrame;    // a whole frame, so the delta stream is re-based
    bool    needsFullFrame;  // the payload was unusable; only a full frame can put it right
    bool    changed;         // the display actually moved; the device may still have more to show
} tLcdReplyData;

typedef struct {
    uint32_t cmd;
    union {
        tIdentityReplyData identityReplyData;
        tSessionStatusData sessionStatusData;
        tButtonEventData   buttonEventData;
        tRotaryEventData   rotaryEventData;
        tNoteEventData     noteEventData;
        tLcdRefreshData    lcdRefreshData;
        tLcdReplyData      lcdReplyData;
        tSdsStartData      sdsStartData;
        tSdsHandshakeData  sdsHandshakeData;
        tSdsRequestData    sdsRequestData;
        tSdsRxFrameData    sdsRxFrameData;
    };
} tMessageContent;

#endif // __MSG_QUEUE_H__
