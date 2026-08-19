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
#include "utils.h"
#include "peptalk.h"
#include "msgQueue.h"
#include "midiComms.h"

#define DIAL_LCD_POLL_INTERVAL_MS    120.0 // how often to poll for an LCD delta while a dial drag is held

static void (*gWakeCb)(void) = NULL;
static pthread_t            gMidiThread          = 0;
static pthread_mutex_t      gSendMutex           = PTHREAD_MUTEX_INITIALIZER;

// The MIDI thread's own CFRunLoop, captured once the thread is up. Posting a command signals it so
// the drain happens promptly instead of waiting out the current CFRunLoopRunInMode interval (up to
// 33ms when idle — enough to make an on-screen button press feel laggy). Written once by the MIDI
// thread, read by posting threads; NULL until then, in which case the command still gets drained on
// the first tick, just without the early wake.
static _Atomic CFRunLoopRef gMidiRunLoop         = NULL;

static int midi_scan_devices(void);

// ── LCD request state — MIDI THREAD ONLY ─────────────────────────────────────
// Every one of these used to be an _Atomic global written by BOTH this thread and the CoreMIDI read
// callback. Individually atomic, but the SEQUENCES were not, and two races fell out of that:
//
//   * a want-bit set by the callback between this thread's read and its clear was silently dropped,
//     leaving the display stale until the settle or the 5 s resync;
//   * the callback's "gLcdPending = false" racing this thread's "gLcdPending = true" let a second
//     request go out while the first was still on the wire — the very overlap that makes a stale
//     delta land on a frame that has moved on, which is measurable corruption.
//
// They are plain statics now, not atomics, because exactly one thread touches them. Everyone else
// POSTS (midi_post_lcd_refresh / midi_post_lcd_reply) and this thread decides what it means — the
// same split handle_identity_reply() already uses, and the ownership rule msgQueue.h sets out.
// Coalescing still works: N refresh requests all set the same bit, so they collapse into one
// transfer, but none of them can be lost.
static bool                 gLcdWantFull         = true;
static bool                 gLcdWantDelta        = false;
static bool                 gLcdWantLeds         = true;
static bool                 gLcdPendingOwn       = false;
static int                  gLcdInFlightOwn      = 0;
static double               gLcdReqMsOwn         = 0.0;
static bool                 gLcdSettledOwn       = true;

// Read by the CoreMIDI callback for its staleness check, so it must be atomic — but this thread is
// still its only writer.
static _Atomic uint8_t      gLcdOutstandingSeq   = 0;
static _Atomic bool         gLcdOutstandingValid = false;

bool midi_outstanding_lcd_seq(uint8_t * seqOut) {
    if (!atomic_load(&gLcdOutstandingValid)) {
        return false;
    }
    *seqOut = atomic_load(&gLcdOutstandingSeq);
    return true;
}

// SysEx reassembly — CoreMIDI fragments large messages across multiple packets, so the bytes have
// to be gathered up until F7 before anything can be made of them.
//
// ONE BUFFER PER SOURCE, and that is the whole point. midi_scan_devices() connects EVERY MIDI source
// on the machine (it has to — an identity reply can come from any of them), so on a real rig this
// callback sees a dozen devices' traffic interleaved. A single shared buffer therefore let one
// device's bytes land in the middle of another's message. That was not theoretical: with a 2205-byte
// LCD frame taking ~705 ms to arrive over DIN, a controller streaming CCs in running status spliced
// its data bytes straight into the E-mu's payload — measured 2026-08-20, payloads arriving at 2363
// to 2759 bytes instead of 2205 and unpacking to 1936 where a frame is exactly 1920. A CC WITH its
// status byte was no better: it took the "any other status byte aborts" path and destroyed the
// transfer outright. Both were visible as corruption or a stalled display.
//
// No locking: CoreMIDI calls a port's read proc on one dedicated thread, and this app has a single
// input port, so every callback for every source is serialised onto that one thread. The defect was
// logical, not a race.
#define SYSEX_BUF_SIZE       8192
#define SYSEX_MAX_SOURCES    16

typedef struct {
    MIDIEndpointRef src;                  // the endpoint this slot is reassembling for; 0 = unused
    uint32_t        len;                  // bytes buffered so far; 0 = no message in progress
    uint8_t         buf[SYSEX_BUF_SIZE];
} tSysExReassembly;

static tSysExReassembly        gSysEx[SYSEX_MAX_SOURCES];

// Once we have locked onto the E-mu there is nothing this app wants from any other device, so its
// traffic is dropped at the door rather than merely kept in its own slot. Belt and braces over the
// per-source buffers above: those make interleaving HARMLESS, this stops it being delivered at all,
// and it keeps the CoreMIDI callback off the hot path for a dozen devices we do not care about.
//
// Zero while no device is locked on, which is exactly the window a scan needs — midi_scan_devices()
// clears it before sending identity requests, so replies from every source still get through, and
// handle_identity_reply() sets it once a device answers.
//
// Deliberately NOT a read of gMidiSource: that belongs to the MIDI thread (see globalVars.h), and
// this is read on the CoreMIDI callback thread. Its own atomic, written by the owning thread.
static _Atomic MIDIEndpointRef gSysExAcceptSrc = 0;

// The slot for this source, claiming a free one on first sight. A slot is only ever reclaimed from a
// source that is NOT mid-message, so growing past SYSEX_MAX_SOURCES can never truncate a transfer
// that is already under way — it drops the newcomer's message instead, which is the safe direction.
static tSysExReassembly * sysex_slot_for(MIDIEndpointRef src) {
    tSysExReassembly * freeSlot = NULL;

    for (int i = 0; i < SYSEX_MAX_SOURCES; i++) {
        if (gSysEx[i].src == src) {
            return &gSysEx[i];
        }

        if ((freeSlot == NULL) && (gSysEx[i].src == 0)) {
            freeSlot = &gSysEx[i];
        }
    }

    if (freeSlot == NULL) {
        for (int i = 0; (i < SYSEX_MAX_SOURCES) && (freeSlot == NULL); i++) {
            if (gSysEx[i].len == 0) {
                freeSlot = &gSysEx[i];
            }
        }
    }

    if (freeSlot == NULL) {
        LOG_ERROR("No free SysEx reassembly slot for source 0x%08X\n", (unsigned)src);
        return NULL;
    }
    freeSlot->src = src;
    freeSlot->len = 0;
    return freeSlot;
}

// Endpoint refs do not survive a CoreMIDI setup change, so a rescan starts the table over rather
// than leaving slots keyed to endpoints that no longer exist.
static void sysex_reset_all(void) {
    memset(gSysEx, 0, sizeof(gSysEx));
}

// ── Internal send to a specific destination ───────────────────────────────────

static void midi_send_to(const uint8_t * data, uint32_t length, MIDIEndpointRef dest) {
    if ((gMidiOutPort == 0) || (dest == 0) || (data == NULL) || (length == 0)) {
        return;
    }
    uint8_t          buf[512 + sizeof(MIDIPacketList)];
    MIDIPacketList * pktList = (MIDIPacketList *)buf;
    MIDIPacket *     pkt     = MIDIPacketListInit(pktList);

    pkt = MIDIPacketListAdd(pktList, sizeof(buf), pkt, 0, length, data);

    if (pkt == NULL) {
        LOG_ERROR("MIDIPacketListAdd failed (message too long?)\n");
        return;
    }
    pthread_mutex_lock(&gSendMutex);
    OSStatus         err     = MIDISend(gMidiOutPort, dest, pktList);
    pthread_mutex_unlock(&gSendMutex);

    if (err != noErr) {
        LOG_ERROR("MIDISend error %d\n", (int)err);
    }
}

// ── Identity reply ────────────────────────────────────────────────────────────
// Split across two threads on purpose. The CoreMIDI read callback only validates and unpacks the
// reply (parse_identity_reply, below) then posts it; the MIDI thread does the entity/destination
// lookup and the writes to gDevice/gMidiSource/gMidiDest, because it owns that state. Before the
// split, the callback thread wrote all three while the MIDI thread's own scan/poll loop was reading
// and rewriting them — the same unsynchronized-ownership bug SynthEdit found and fixed on its side
// (see midi_request_reconnect()'s comment there).

// Runs on the MIDI thread, from the gToMidiThread drain.
static void handle_identity_reply(const tIdentityReplyData * reply) {
    MIDIEndpointRef src    = (MIDIEndpointRef)reply->source;
    MIDIEntityRef   entity = 0;
    MIDIEndpointRef dest   = 0;

    // Find the destination endpoint in the same entity as the replying source
    if (MIDIEndpointGetEntity(src, &entity) == noErr && entity != 0) {
        ItemCount dests = MIDIEntityGetNumberOfDestinations(entity);

        if (dests > 0) {
            dest = MIDIEntityGetDestination(entity, 0);
        }
    }

    if (dest == 0) {
        LOG_ERROR("No destination found for E-mu identity reply source\n");
        return;
    }
    gDevice.id        = reply->deviceId;
    gDevice.family    = reply->family;
    gDevice.member    = reply->member;
    gDevice.connected = true;
    gMidiSource       = src;
    gMidiDest         = dest;
    atomic_store(&gSysExAcceptSrc, src);   // from here on, ignore every other device's traffic

    LOG_DEBUG("Locked onto E-mu device\n");

    peptalk_send_session_open();

    if (gWakeCb != NULL) {
        gWakeCb();
    }
}

// Runs on the CoreMIDI read callback thread.
static void parse_identity_reply(MIDIEndpointRef src, const uint8_t * data, uint32_t length) {
    // F0 7E <device_id> 06 02 <mfr_id> <fam_lsb> <fam_msb> <mem_lsb> <mem_msb> ... F7
    LOG_DEBUG("identity reply length=%u mfr=0x%02X\n", (unsigned)length, (length >= 6) ? data[5] : 0xFF);

    if (length < 10) {
        LOG_DEBUG("identity reply too short\n");
        return;
    }

    if (data[5] != EMU_MANUFACTURER_ID) {
        LOG_DEBUG("identity reply mfr 0x%02X != E-mu 0x%02X, ignoring\n", data[5], EMU_MANUFACTURER_ID);
        return;
    }
    uint8_t  deviceId = data[2];
    uint16_t family   = (uint16_t)(data[6] | ((uint16_t)data[7] << 7));
    uint16_t member   = (uint16_t)(data[8] | ((uint16_t)data[9] << 7));

    LOG_DEBUG("E-mu identity reply: device_id=0x%02X family=%u member=%u\n",
              deviceId, (unsigned)family, (unsigned)member);

    midi_post_identity_reply(src, deviceId, family, member);
}

// ── MIDI notification callback ────────────────────────────────────────────────

static void midi_notify_cb(const MIDINotification * msg, void * refCon) {
    (void)refCon;

    if (msg->messageID == kMIDIMsgSetupChanged) {
        LOG_DEBUG("CoreMIDI setup changed\n");

        // This notification is delivered on the MIDI thread's own CFRunLoop (the client was created
        // there), so calling midi_scan_devices() directly here would in fact be safe. It still goes
        // through the queue: it keeps ONE rule — "the scan runs from the drain" — rather than one
        // safe direct caller plus a rule everyone else has to remember, and it means a setup change
        // arriving mid-drain queues behind the command already being serviced instead of re-entering
        // the scan from underneath it.
        midi_request_reconnect();
        synthlib_request_redraw();

        if (gWakeCb != NULL) {
            gWakeCb();
        }
    }
}

// ── SysEx dispatch (called once a complete message has been reassembled) ─────

static void dispatch_sysex(MIDIEndpointRef src, const uint8_t * data, uint32_t length) {
    LOG_DEBUG("SysEx complete %u bytes from src 0x%08X hdr: %02X %02X %02X %02X %02X %02X\n",
              (unsigned)length, (unsigned)src,
              (length > 0) ? data[0] : 0xFF,
              (length > 1) ? data[1] : 0xFF,
              (length > 2) ? data[2] : 0xFF,
              (length > 3) ? data[3] : 0xFF,
              (length > 4) ? data[4] : 0xFF,
              (length > 5) ? data[5] : 0xFF);

    if (  (length >= 5)
       && (data[1] == MIDI_NON_REALTIME)
       && (data[3] == MIDI_IDENTITY_REQUEST_SUB1)
       && (data[4] == MIDI_IDENTITY_REPLY_SUB2)) {
        parse_identity_reply(src, data, length);
    } else {
        peptalk_handle_message(data, length);
    }

    if (gWakeCb != NULL) {
        gWakeCb();
    }
}

// ── MIDI read callback (called on MIDI thread) ────────────────────────────────
// CoreMIDI fragments large SysEx across multiple MIDIPackets; we reassemble
// byte-by-byte before dispatching.

static void midi_read_cb(const MIDIPacketList * pktList, void * readProcRefCon, void * srcConnRefCon) {
    (void)readProcRefCon;

    MIDIEndpointRef    src    = (MIDIEndpointRef)(uintptr_t)srcConnRefCon;
    const MIDIPacket * pkt    = &pktList->packet[0];

    // Resolved once per callback, not per byte: every byte in this packet list came from the same
    // source, and only this source's reassembly may be touched by them.
    MIDIEndpointRef    accept = atomic_load(&gSysExAcceptSrc);

    if ((accept != 0) && (src != accept)) {
        return;   // locked onto a device; nothing else on the rig is of any interest
    }
    tSysExReassembly * slot   = sysex_slot_for(src);

    if (slot == NULL) {
        return;
    }

    for (uint32_t i = 0; i < pktList->numPackets; i++) {
        for (uint16_t b = 0; b < pkt->length; b++) {
            uint8_t byte = pkt->data[b];

            if (byte == MIDI_SYSEX_START) {
                slot->buf[0] = byte;
                slot->len    = 1;
            } else if (byte == MIDI_SYSEX_END) {
                if (slot->len > 0) {
                    if (slot->len < SYSEX_BUF_SIZE) {
                        slot->buf[slot->len++] = byte;
                    }
                    dispatch_sysex(slot->src, slot->buf, slot->len);
                    slot->len = 0;
                }
            } else if (byte >= 0xF8) {
                // Realtime byte — valid inside SysEx, ignore for our purposes
            } else if (byte >= 0x80) {
                // Any other status byte aborts the in-progress SysEx — but only THIS source's, which
                // is the fix: a CC from another device used to abort whatever the E-mu was sending.
                if (slot->len > 0) {
                    LOG_DEBUG("SysEx aborted by status 0x%02X after %u bytes (src 0x%08X)\n",
                              byte, (unsigned)slot->len, (unsigned)slot->src);
                    slot->len = 0;
                }
            } else {
                // Data byte. Appended only to the message this source has open; with a shared buffer
                // these were what spliced one device's stream into another's payload.
                if (slot->len > 0) {
                    if (slot->len < SYSEX_BUF_SIZE) {
                        slot->buf[slot->len++] = byte;
                    } else {
                        LOG_ERROR("SysEx buffer overflow after %u bytes, discarding (src 0x%08X)\n",
                                  (unsigned)slot->len, (unsigned)slot->src);
                        slot->len = 0;
                    }
                }
            }
        }

        pkt = MIDIPacketNext(pkt);
    }
}

// ── Device scanning (MIDI thread only — reached via eMsgCmdScanDevices) ──────

static int midi_scan_devices(void) {
    static const uint8_t idReq[]   = {
        MIDI_SYSEX_START,
        MIDI_NON_REALTIME,
        MIDI_DEVICE_INQUIRY,
        MIDI_IDENTITY_REQUEST_SUB1,
        MIDI_IDENTITY_REQUEST_SUB2,
        MIDI_SYSEX_END
    };

    ItemCount            srcCount  = MIDIGetNumberOfSources();
    ItemCount            destCount = MIDIGetNumberOfDestinations();

    gMidiSource = 0;
    gMidiDest   = 0;
    memset(&gDevice, 0, sizeof(gDevice));
    sysex_reset_all();
    atomic_store(&gSysExAcceptSrc, 0);      // a scan must hear every source, or no identity reply can land   // endpoint refs do not survive a setup change; see sysex_slot_for()

    for (ItemCount i = 0; i < srcCount; i++) {
        MIDIEndpointRef src  = MIDIGetSource(i);
        CFStringRef     name = NULL;

        MIDIObjectGetStringProperty(src, kMIDIPropertyName, &name);

        if (name != NULL) {
            char buf[128] = {0};
            CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
            CFRelease(name);
            LOG_DEBUG("MIDI source %lu: %s\n", (unsigned long)i, buf);
        }
        // Pass src as connRefCon so midi_read_cb knows which endpoint replied
        MIDIPortConnectSource(gMidiInPort, src, (void *)(uintptr_t)src);
    }

    for (ItemCount i = 0; i < destCount; i++) {
        MIDIEndpointRef dest = MIDIGetDestination(i);
        CFStringRef     name = NULL;

        MIDIObjectGetStringProperty(dest, kMIDIPropertyName, &name);

        if (name != NULL) {
            char buf[128] = {0};
            CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
            CFRelease(name);
            LOG_DEBUG("MIDI dest %lu: %s\n", (unsigned long)i, buf);
        }
        midi_send_to(idReq, sizeof(idReq), dest);

        // Small stagger between each destination's identity request — ported
        // from SynthEdit's identical fix (midiComms.c, 2026-07-13), found
        // debugging a Korg Z1 that connected in under a second alone but
        // took 20+ seconds or hung entirely with 3 other synths (Moog
        // Minitaur, Waldorf Pulse, ASM Hydrasynth) sharing the same
        // interface. Blasting every destination's request back-to-back with
        // zero gap made all their replies land at nearly the same instant on
        // the interface's merged input, where they likely collide/corrupt
        // rather than interleave cleanly, rather than any bug in the
        // matching logic itself. Matters more here than in SynthEdit: this
        // function only runs once at startup (or on a CoreMIDI setup-change
        // notification, see midi_notify_cb above) with no periodic retry
        // loop behind it, so a single collision leaves the E-mu undetected
        // until something re-triggers a rescan, rather than quietly
        // succeeding a couple seconds later. CFRunLoopRunInMode, not
        // usleep/nanosleep — this thread is CFRunLoop-driven throughout (see
        // midi_thread()'s own comment above), not the platform-thread model
        // those assume.
        if ((i + 1) < destCount) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.015, false);
        }
    }

    if ((srcCount > 0) && (destCount > 0)) {
        return EXIT_SUCCESS;
    }
    LOG_DEBUG("No MIDI sources/destinations found\n");
    return EXIT_FAILURE;
}

// ── Identity request (public — re-sends to confirmed destination) ─────────────

void midi_send_identity_request(void) {
    static const uint8_t idReq[] = {
        MIDI_SYSEX_START,
        MIDI_NON_REALTIME,
        MIDI_DEVICE_INQUIRY,
        MIDI_IDENTITY_REQUEST_SUB1,
        MIDI_IDENTITY_REQUEST_SUB2,
        MIDI_SYSEX_END
    };

    LOG_DEBUG("Sending MIDI identity request\n");
    midi_send_to(idReq, sizeof(idReq), gMidiDest);
}

// ── Send ──────────────────────────────────────────────────────────────────────

void midi_send(const uint8_t * data, uint32_t length) {
    midi_send_to(data, length, gMidiDest);
}

// ── Command posting (any thread) ─────────────────────────────────────────────

static void post_to_midi_thread(const tMessageContent * msg) {
    CFRunLoopRef runLoop = atomic_load(&gMidiRunLoop);

    msg_send(&gToMidiThread, msg);

    // Cut short the MIDI thread's current CFRunLoopRunInMode wait so the command is drained now
    // rather than up to one idle tick (33ms) later. CFRunLoopWakeUp is documented thread-safe.
    if (runLoop != NULL) {
        CFRunLoopWakeUp(runLoop);
    }
}

void midi_request_reconnect(void) {
    tMessageContent msg = {0};

    msg.cmd = eMsgCmdScanDevices;
    post_to_midi_thread(&msg);
}

void midi_post_identity_reply(MIDIEndpointRef source, uint8_t deviceId, uint16_t family, uint16_t member) {
    tMessageContent msg = {0};

    msg.cmd                        = eMsgCmdIdentityReply;
    msg.identityReplyData.source   = (uint32_t)source;
    msg.identityReplyData.deviceId = deviceId;
    msg.identityReplyData.family   = family;
    msg.identityReplyData.member   = member;
    post_to_midi_thread(&msg);
}

void midi_post_button_event(tButtonKey key, bool pressed) {
    tMessageContent msg = {0};

    msg.cmd                     = eMsgCmdButtonEvent;
    msg.buttonEventData.key     = (uint32_t)key;
    msg.buttonEventData.pressed = pressed;
    post_to_midi_thread(&msg);
}

void midi_post_rotary_event(int delta) {
    tMessageContent msg = {0};

    msg.cmd                   = eMsgCmdRotaryEvent;
    msg.rotaryEventData.delta = (int32_t)delta;
    post_to_midi_thread(&msg);
}

void midi_post_note_event(uint8_t note, uint8_t velocity, bool on) {
    tMessageContent msg = {0};

    msg.cmd                    = eMsgCmdNoteEvent;
    msg.noteEventData.note     = note;
    msg.noteEventData.velocity = velocity;
    msg.noteEventData.on       = on;
    post_to_midi_thread(&msg);
}

void midi_note_ui_activity(void) {
    tMessageContent msg = {0};

    gLastUiEventMs = get_time_ms();   // UI threads are its only writer; the MIDI thread only reads it
    msg.cmd        = eMsgCmdUiActivity;
    post_to_midi_thread(&msg);
}

void midi_post_lcd_refresh(bool full) {
    tMessageContent msg = {0};

    msg.cmd                  = eMsgCmdLcdRefresh;
    msg.lcdRefreshData.full  = full;
    msg.lcdRefreshData.delta = !full;
    post_to_midi_thread(&msg);
}

void midi_post_led_refresh(void) {
    tMessageContent msg = {0};

    msg.cmd                 = eMsgCmdLcdRefresh;
    msg.lcdRefreshData.leds = true;
    post_to_midi_thread(&msg);
}

void midi_post_lcd_reply(const tLcdReplyData * reply) {
    tMessageContent msg = {0};

    msg.cmd          = eMsgCmdLcdReply;
    msg.lcdReplyData = *reply;
    post_to_midi_thread(&msg);
}

void midi_post_session_open(void) {
    tMessageContent msg = {0};

    msg.cmd = eMsgCmdSessionOpen;
    post_to_midi_thread(&msg);
}

// ── Command drain (MIDI thread) ──────────────────────────────────────────────
// Poll-drained, NOT blocked on (eRcvPoll, not eRcvWait as G2-Edit's USB thread uses): this thread
// has to keep driving its CFRunLoop so midi_notify_cb fires, and it has its own time-based polling
// cadence (the LCD delta throttle and the idle tick below). Blocking in msg_receive would stall
// both. Drains everything queued each tick — unlike the render loop's one-per-frame drain in
// G2-Edit, nothing here is modal, so there is no reason to spread the work across ticks.
//
// eMsgCmdScanDevices is COALESCED: however many arrived this tick, the scan runs at most once,
// after the rest of the batch. A rescan is a request for a state ("be freshly scanned"), not a
// discrete event, so N of them must collapse to one — the same rule reverse-queue-design.md gives
// for gotPatchChangeIndication et al., applied to a command rather than a response. It matters
// here: a single hub plug/unplug can fire several kMIDIMsgSetupChanged notifications, and each scan
// walks every destination sending a staggered identity request (15ms apart, see midi_scan_devices),
// so running the scan per message would multiply that up for no gain. Deferring it to the end of
// the batch is also the right order — identity replies still in the queue belong to the PREVIOUS
// scan and should be handled against the connection state that produced them.

static void drain_midi_commands(void) {
    tMessageContent msg           = {0};
    bool            scanRequested = false;

    while (msg_receive(&gToMidiThread, eRcvPoll, &msg) == EXIT_SUCCESS) {
        switch (msg.cmd) {
            case eMsgCmdScanDevices:
                scanRequested = true;
                break;

            case eMsgCmdIdentityReply:
                handle_identity_reply(&msg.identityReplyData);
                break;

            case eMsgCmdSessionOpen:
                peptalk_send_session_open();
                break;

            case eMsgCmdButtonEvent:
                peptalk_send_button_event((tButtonKey)msg.buttonEventData.key,
                                          msg.buttonEventData.pressed);
                break;

            case eMsgCmdRotaryEvent:
                peptalk_send_rotary_event((int)msg.rotaryEventData.delta);
                break;

            case eMsgCmdLcdRefresh:

                // Set a want-bit, never clear one — that asymmetry is what makes losing a request
                // impossible, and it is why these are messages rather than flags other threads write.
                gLcdWantFull  |= msg.lcdRefreshData.full;
                gLcdWantDelta |= msg.lcdRefreshData.delta;
                gLcdWantLeds  |= msg.lcdRefreshData.leds;
                break;

            case eMsgCmdLcdReply:
                // Logged here, not at the point of receipt: this thread is the one that knows when
                // the request went out, so it is the only one that can time the round trip.
                LOG_DEBUG("LCD reply seq=%02X%s%s roundTrip=%.0fms\n",
                          (unsigned)msg.lcdReplyData.seq,
                          msg.lcdReplyData.stale ? " STALE" : "",
                          msg.lcdReplyData.wasFullFrame ? " full" : " delta",
                          get_time_ms() - gLcdReqMsOwn);

                // One fewer message on the wire, whatever it turned out to be.
                if (gLcdInFlightOwn > 0) {
                    gLcdInFlightOwn--;
                }

                // A stale reply answered a request we had already abandoned, so the one we are
                // actually waiting for is still outstanding and must stay marked as such.
                if (!msg.lcdReplyData.stale) {
                    gLcdPendingOwn = false;
                    atomic_store(&gLcdOutstandingValid, false);
                }

                if (msg.lcdReplyData.needsFullFrame) {
                    gLcdWantFull = true;
                }
                break;

            case eMsgCmdUiActivity:
                gLcdSettledOwn = false;
                break;

            case eMsgCmdNoteEvent:
            {
                // An explicit Note Off (0x80) rather than the running-status "Note On, velocity 0"
                // shorthand: this app never uses running status, so the shorthand saves nothing,
                // and a device that treats a zero-velocity Note On as a real strike would be left
                // with a stuck note.
                uint8_t note[3] = {
                    (uint8_t)((msg.noteEventData.on ? MIDI_NOTE_ON : MIDI_NOTE_OFF) | NOTE_ENTRY_MIDI_CHANNEL),
                    (uint8_t)(msg.noteEventData.note & 0x7F),
                    (uint8_t)(msg.noteEventData.on ? (msg.noteEventData.velocity & 0x7F) : 0)
                };

                LOG_DEBUG("MIDI note %s ch=%u note=%u vel=%u (%02X %02X %02X)\n",
                          msg.noteEventData.on ? "on" : "off", (unsigned)(NOTE_ENTRY_MIDI_CHANNEL + 1),
                          (unsigned)msg.noteEventData.note, (unsigned)note[2], note[0], note[1], note[2]);
                midi_send(note, sizeof(note));
                break;
            }

            default:
                LOG_ERROR("Unknown MIDI-thread command %u\n", (unsigned)msg.cmd);
                break;
        }
    }

    if (scanRequested) {
        midi_scan_devices();
    }
}

// ── MIDI poll thread ──────────────────────────────────────────────────────────

static void * midi_thread(void * arg) {
    (void)arg;

    LOG_DEBUG("MIDI thread started\n");

    // Publish this thread's run loop so post_to_midi_thread() can cut short the wait below.
    atomic_store(&gMidiRunLoop, CFRunLoopGetCurrent());

    // Create MIDI client here (not on main thread) so MIDIClientCreate does not
    // block app startup.  The notification callback is tied to this thread's
    // CFRunLoop, which we drive with CFRunLoopRunInMode in place of nanosleep.
    OSStatus err;
    err = MIDIClientCreate(CFSTR("EmuUtility"), midi_notify_cb, NULL, &gMidiClient);

    if (err != noErr) {
        LOG_ERROR("MIDIClientCreate failed: %d\n", (int)err);
        return NULL;
    }
    err = MIDIInputPortCreate(gMidiClient, CFSTR("EmuUtility In"), midi_read_cb, NULL, &gMidiInPort);

    if (err != noErr) {
        LOG_ERROR("MIDIInputPortCreate failed: %d\n", (int)err);
        return NULL;
    }
    err = MIDIOutputPortCreate(gMidiClient, CFSTR("EmuUtility Out"), &gMidiOutPort);

    if (err != noErr) {
        LOG_ERROR("MIDIOutputPortCreate failed: %d\n", (int)err);
        return NULL;
    }
    midi_scan_devices();

    while (!synthlib_quit_requested()) {
        // Service queued commands first, so a scan/identity/button posted since the last tick is
        // applied before this tick's polling decisions read the state it may have just changed.
        drain_midi_commands();

        // Poll: if session open, request LCD/LED updates as needed.
        //
        // Every want-bit below is private to this thread (see their declarations). Other threads ASK
        // via midi_post_lcd_refresh(); nothing outside this loop sets or clears them, so a request
        // cannot be cleared before it was served and a reply cannot retire a request it did not
        // answer.
        if (gSessionOpen) {
            // While a dial drag is held, poll for an LCD delta on a steady
            // throttled cadence — independent of whether new encoder ticks
            // are currently being sent. This covers both a long continuous
            // drag (ticks never stop long enough to "go quiet") and a held
            // but paused drag (no ticks at all) alike. Never sends a new
            // encoder value itself — that's only ever driven by dial_nudge().
            if (  gDialDragActive && !gLcdPendingOwn
               && ((get_time_ms() - gLastLcdPollMs) >= DIAL_LCD_POLL_INTERVAL_MS)) {
                gLcdWantDelta  = true;
                gLastLcdPollMs = get_time_ms();
            }

            // Re-base the delta stream once the display has gone quiet. Deltas are XORs against
            // the frame we hold, so a lost one would leave the display wrong indefinitely; this
            // bounds that to LCD_RESYNC_IDLE_MS without ever putting a ~705 ms full frame in front
            // of a user who is still pressing keys. Deliberately skipped while a dial drag is held
            // — that path is a continuous stream of deltas and is quiet only once the drag ends.
            if (  !gLcdBaseTrusted && !gLcdPendingOwn && !gDialDragActive
               && !gLcdWantFull && !gLcdWantDelta
               && ((get_time_ms() - gLcdLastDeltaMs) >= LCD_RESYNC_IDLE_MS)) {
                LOG_DEBUG("LCD idle resync: taking one full frame to re-base the delta stream\n");
                gLcdWantFull = true;
            }

            // Write off a request whose reply never came, and re-base with a full frame — after a
            // lost reply the delta stream has no valid base anyway. Without this, the pending flag
            // latched on forever and the display went permanently stale.
            if (gLcdPendingOwn && ((get_time_ms() - gLcdReqMsOwn) >= LCD_REQUEST_TIMEOUT_MS)) {
                LOG_ERROR("LCD request timed out after %.0fms — re-basing with a full frame\n",
                          get_time_ms() - gLcdReqMsOwn);
                // gLcdInFlightOwn is deliberately NOT decremented: the reply we gave up on is still
                // coming, and recognising it by its sequence id when it lands is the point.
                gLcdPendingOwn = false;
                atomic_store(&gLcdOutstandingValid, false);
                gLcdWantFull   = true;
            }

            // One trailing delta once the burst stops, so the display ends up on the same value the
            // hardware landed on. Without it the last request of a burst can be answered before the
            // device finished acting on the final event, and nothing asks again.
            if (  !gLcdSettledOwn && !gLcdPendingOwn && !gDialDragActive
               && !gLcdWantFull && !gLcdWantDelta
               && ((get_time_ms() - gLastUiEventMs) >= LCD_SETTLE_MS)) {
                gLcdWantDelta  = true;
                gLcdSettledOwn = true;
            }

            // The LED request shares the link with the LCD transfer, so it waits its turn too —
            // firing one mid-transfer only lengthens the round trip everything else is queued behind.
            if (gLcdWantLeds && !gLcdPendingOwn) {
                extern void peptalk_send_led_state_request(void);
                gLcdWantLeds = false;
                peptalk_send_led_state_request();
            } else if (!gLcdPendingOwn) {
                extern void peptalk_send_lcd_dump_request(void);
                extern void peptalk_send_lcd_delta_request(void);
                extern uint8_t peptalk_last_request_seq(void);

                // Marked pending BEFORE sending, so a reply that arrives while we are still in this
                // block cannot be mistaken for there being nothing outstanding.
                if (gLcdWantFull || gLcdWantDelta) {
                    bool full = gLcdWantFull;

                    gLcdWantFull   = false;
                    gLcdWantDelta  = false;   // a full frame answers a pending delta too
                    gLcdReqMsOwn   = get_time_ms();
                    gLcdPendingOwn = true;
                    gLcdInFlightOwn++;

                    if (full) {
                        peptalk_send_lcd_dump_request();
                    } else {
                        peptalk_send_lcd_delta_request();
                    }
                    atomic_store(&gLcdOutstandingSeq, peptalk_last_request_seq());
                    atomic_store(&gLcdOutstandingValid, true);
                }
            }
        }
        // Drive this thread's CFRunLoop so the midi_notify_cb fires here.
        // Use a short interval when work is in progress, idle at ~30 Hz otherwise.
        bool   busy    = gLcdPendingOwn
                         || gLcdWantFull
                         || gLcdWantDelta
                         || gDialDragActive;
        double seconds = busy ? 0.005 : 0.033;
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, seconds, false);
    }
    LOG_DEBUG("MIDI thread exiting\n");
    atomic_store(&gMidiRunLoop, NULL);
    return NULL;
}

// ── Startup ───────────────────────────────────────────────────────────────────

int start_midi_thread(void) {
    // Before pthread_create, not inside the thread: main() registers the sleep/wake notification and
    // builds the menus first, so a command could in principle be posted before the thread's first
    // line runs. An initialised-but-undrained queue just holds it until the first tick.
    msg_init(&gToMidiThread, "toMidiThread", sizeof(tMessageContent));

    if (pthread_create(&gMidiThread, NULL, midi_thread, NULL) != 0) {
        LOG_ERROR("pthread_create for MIDI thread failed\n");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

void register_midi_wake_cb(void ( *cb )(void)) {
    gWakeCb = cb;
}
