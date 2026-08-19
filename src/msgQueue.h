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

#ifndef __MSG_QUEUE_H__
#define __MSG_QUEUE_H__

#include "sysIncludes.h"
#include "types.h"
#include "synthlibQueue.h" // generic queue mechanism: tMessageQueue / eRcv / msg_init / msg_send / ...

// gToMidiThread is the MIDI thread's command queue. Every other thread (the UI/render thread, the
// CoreMIDI read callback thread, the NSWorkspace sleep/wake block) posts here instead of touching
// the connection state or sending to the device itself, so gDevice / gMidiSource / gMidiDest and the
// CoreMIDI port objects have exactly ONE owner. That ownership rule is the whole point: before this,
// midi_scan_devices() ran on the UI thread from the Scan Devices menu item and from the wake
// notification, concurrently rewriting the very state the MIDI thread's own loop was using.
// SynthEdit hit and fixed the same class of bug (see midi_request_reconnect() there); this is the
// same fix expressed with the shared SynthLib queue instead of a bespoke flag.
//
// There is deliberately NO reverse (MIDI -> UI) queue here yet. Everything this app reports upward
// today is a *coalescing* dirty-bit (gNeedLcdFull/gNeedLcdDelta, gLcd.refresh, gLeds), and those must
// stay flags — N rapid device updates have to collapse into one redraw, where a queue would enqueue
// N. See reverse-queue-design.md ("What belongs on the queue — and what doesn't"). Add gToGuiThread
// when there is a first genuine discrete result to carry.
typedef enum {
    eMsgCmdScanDevices,    // rescan CoreMIDI and re-identify (menu action, sleep/wake, setup change)
    eMsgCmdIdentityReply,  // identityReplyData: an identity reply seen by the CoreMIDI read callback
    eMsgCmdSessionOpen,
    eMsgCmdButtonEvent,    // buttonEventData
    eMsgCmdRotaryEvent,    // rotaryEventData
    eMsgCmdNoteEvent       // noteEventData: a MIDI note from the computer-keyboard note entry
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

typedef struct {
    uint32_t cmd;
    union {
        tIdentityReplyData identityReplyData;
        tButtonEventData   buttonEventData;
        tRotaryEventData   rotaryEventData;
        tNoteEventData     noteEventData;
    };
} tMessageContent;

#endif // __MSG_QUEUE_H__
