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
#include "sampleDump.h"

#define DIAL_LCD_POLL_INTERVAL_MS    120.0 // how often to poll for an LCD delta while a dial drag is held

static void (*gWakeCb)(void) = NULL;
static pthread_t            gMidiThread  = 0;
static pthread_mutex_t      gSendMutex   = PTHREAD_MUTEX_INITIALIZER;

// The MIDI thread's own CFRunLoop, captured once the thread is up. Posting a command signals it so
// the drain happens promptly instead of waiting out the current CFRunLoopRunInMode interval (up to
// 33ms when idle — enough to make an on-screen button press feel laggy). Written once by the MIDI
// thread, read by posting threads; NULL until then, in which case the command still gets drained on
// the first tick, just without the early wake.
static _Atomic CFRunLoopRef gMidiRunLoop = NULL;

static int midi_scan_devices(void);

// Defined below, next to the rest of the transfer state machine; declared here because the command
// drain sits above it.
static void sds_start(const tSdsStartData * start);
static void sds_request(const tSdsRequestData * req);
static void sds_rx_frame(const tSdsRxFrameData * frame);
static void sds_rx_finish(const char * why, bool writeFile);
static void sds_handshake(const tSdsHandshakeData * hs);
static void sds_finish(const char * why);

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
static bool             gLcdWantFull         = true;
static bool             gLcdWantDelta        = false;
static bool             gLcdWantLeds         = true;
static bool             gLcdPendingOwn       = false;
static int              gLcdInFlightOwn      = 0;
static double           gLcdReqMsOwn         = 0.0;
static bool             gLcdSettledOwn       = true;

// What gLastUiEventMs read when the outstanding request went out, and whether the answer that came
// back can be trusted as a base for further deltas.
//
// A delta describes the screen as it was when the DEVICE computed it. If the user keeps pressing
// while that ~700 ms transfer is on the wire, the screen moves on underneath it, and the device's
// idea of "what I last sent you" and ours drift apart by exactly the changes made during the
// transfer. Measured: 383-447 of 1920 bytes wrong, persisting until a full frame. Deltas stay
// correct for isolated input — verified bit-exact — so the answer is not to abandon them, but to
// notice the one condition that spoils them and re-base once the burst is over.
// Encoder ticks waiting to go out as one message — see ROTARY_COALESCE_MS.
static _Atomic uint32_t gRotaryTicksIn       = 0;
static _Atomic uint32_t gRotaryMessagesOut   = 0;
static int              gRotaryAccum         = 0;
static double           gRotaryLastSendMs    = 0.0;

// ── Sample Dump Standard transfer — MIDI THREAD ONLY ─────────────────────────
// Minutes long, so it is a state machine ticked from the poll loop rather than a blocking loop: the
// thread still has to service its run loop and its command queue throughout. The handshake arrives
// on the CoreMIDI callback thread and is POSTED here, same ownership rule as everything else.
typedef enum {
    sdsIdle = 0,
    sdsHeaderSent,      // waiting to learn whether anyone is handshaking
    sdsSending,
} eSdsState;

static eSdsState        gSdsState            = sdsIdle;
static tSampleDump      gSdsDump             = {0};
static uint16_t         gSdsSampleNumber     = 0;
static uint8_t          gSdsChannel          = 0;
static uint32_t         gSdsPacket           = 0;        // next packet to send
static uint32_t         gSdsPacketCount      = 0;
static double           gSdsWaitStartMs      = 0.0;
static double           gSdsLastSendMs       = 0.0;
static bool             gSdsClosedLoop       = false;
static bool             gSdsAwaitingAck      = false;

// Mirrors for the UI/test reader; MIDI thread is the only writer.
static _Atomic bool     gSdsActive           = false;
static _Atomic uint32_t gSdsProgress         = 0;
static _Atomic uint32_t gSdsTotal            = 0;
static _Atomic bool     gSdsProgressClosed   = false;

// ── Receiving a sample FROM the device ───────────────────────────────────────
// The safe direction: a DUMP REQUEST changes nothing on the sampler, where sending a sample TO it
// overwrites whichever one is selected. Same thread rules — the callback hands frames over, this
// thread verifies, assembles and answers.
static bool             gSdsRxActive         = false;
static bool             gSdsRxHaveHeader     = false;
static int16_t *        gSdsRxSamples        = NULL;
static uint32_t         gSdsRxWords          = 0;
static uint32_t         gSdsRxGot            = 0;
static uint32_t         gSdsRxRate           = 44100;
static uint32_t         gSdsRxLoopStart      = 0;
static uint32_t         gSdsRxLoopEnd        = 0;
static bool             gSdsRxHasLoop        = false;
static uint16_t         gSdsRxSampleNumber   = 0;
static char             gSdsRxPath[400]      = {0};
static double           gSdsRxLastMs         = 0.0;
static char             gSdsRxStatus[96]     = "idle";

static _Atomic bool     gSdsRxRunning        = false;
static _Atomic uint32_t gSdsRxProgressGot    = 0;
static _Atomic uint32_t gSdsRxProgressTotal  = 0;

static bool             gLcdLastReplyChanged = true;
static int              gLcdChaseCount       = 0;
static double           gLcdReqUiStamp       = 0.0;

// Mirror of the above for the CoreMIDI callback thread, which has to decide whether the frame it is
// holding is still worth painting. Written only by the MIDI thread.
static _Atomic double   gLcdReqUiStampSeen   = 0.0;

// True while the request on the wire is a probe: a delta asked only to find out WHETHER the screen
// moved. Its content is deliberately discarded — see LCD_PROBE_WHEN_IDLE.
static _Atomic bool     gLcdProbeInFlight    = false;
static double           gLcdLastProbeMs      = 0.0;
static _Atomic bool     gWindowFocused       = true;

// How long passed between the last two input events. Small means the user is working a control
// continuously rather than making one change — see LCD_STREAM_GAP_MS.
static _Atomic double   gLastUiGapMs         = 1.0e9;
static _Atomic double   gPressSettleMs       = LCD_PRESS_SETTLE_MS;
static bool             gLcdDeltaSuspect     = false;

// Read by the CoreMIDI callback for its staleness check, so these must be atomic — but this thread
// is still their only writer.
static _Atomic uint8_t  gLcdOutstandingSeq   = 0;
static _Atomic bool     gLcdOutstandingValid = false;
static _Atomic int      gLcdOutstandingCount = 0;

// Is this reply one we should refuse to apply?
//
// A sequence mismatch ALONE is not enough, and assuming it was cost real updates. The device also
// speaks unprompted — the session-status message is one such, and LCD replies turn up carrying ids
// we never sent — so a mismatched reply is usually perfectly good data that simply was not an answer
// to our outstanding request. Discarding those left the display several presets behind the hardware
// and stuck there, because the request stayed marked pending with nothing left to answer it.
//
// A reply can only be genuinely STALE if more than one request is actually outstanding, which
// happens solely when the timeout gave up on one and sent another. That is the discriminator: the
// mismatch says WHICH reply is the old one, the count says whether an old one can exist at all.
// See midiComms.h. Read from the render thread by the backdoor; the fields are this thread's, and a
// slightly stale read only ever delays a test by one tick.
void midi_rotary_counts(uint32_t * ticksIn, uint32_t * messagesOut) {
    if (ticksIn != NULL) {
        *ticksIn = atomic_load(&gRotaryTicksIn);
    }

    if (messagesOut != NULL) {
        *messagesOut = atomic_load(&gRotaryMessagesOut);
    }
}

bool midi_lcd_is_quiet(void) {
    return !gLcdPendingOwn && !gLcdWantFull && !gLcdWantDelta && !gLcdLastReplyChanged;
}

// Does the reply now in hand describe a screen that has already moved on?
//
// A transfer takes ~700 ms. If the user touched anything while it was in flight, what arrived is a
// picture of a moment that has passed — and painting it puts an OLD value on screen over a newer
// one. Measured directly: a full frame requested at mouse-down arrived 719 ms later still showing
// P000 while the device had already moved to P011, so the display went P011 -> P000 -> P011.
//
// Better to keep showing the last frame we believed than to paint a stale one. The caller asks
// again, and the next reply — taken after the movement — is correct.
void midi_set_press_settle_ms(double ms) {
    atomic_store(&gPressSettleMs, ms);
}

double midi_press_settle_ms(void) {
    return atomic_load(&gPressSettleMs);
}

void midi_set_window_focused(bool focused) {
    atomic_store(&gWindowFocused, focused);
}

bool midi_window_focused(void) {
    return atomic_load(&gWindowFocused);
}

bool midi_lcd_probe_in_flight(void) {
    return atomic_load(&gLcdProbeInFlight);
}

bool midi_lcd_reply_describes_stale_screen(void) {
    return gLastUiEventMs != atomic_load(&gLcdReqUiStampSeen);
}

bool midi_lcd_reply_suspect(uint8_t replySeq) {
    if (atomic_load(&gLcdOutstandingCount) <= 1) {
        return false;
    }

    if (!atomic_load(&gLcdOutstandingValid)) {
        return false;
    }
    return replySeq != atomic_load(&gLcdOutstandingSeq);
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

    // Sample Dump Standard handshake: F0 7E cc <7C WAIT|7D CANCEL|7E NAK|7F ACK> pp F7. Universal
    // non-realtime like the identity reply, but a different sub-id, so there is no ambiguity. Caught
    // here because peptalk_handle_message() would reject it — the manufacturer byte is 7E, not E-mu's
    // 18 — and the transfer would silently fall back to open loop.
    if (  (length == 6)
       && (data[1] == MIDI_NON_REALTIME)
       && (data[3] >= 0x7C) && (data[3] <= 0x7F)) {
        midi_post_sds_handshake(data[3], data[4]);
    } else if (  (length >= 6)
              && (data[1] == MIDI_NON_REALTIME)
              && ((data[3] == 0x01) || (data[3] == 0x02))) {
        // Dump header or data packet coming the other way — the device answering our DUMP REQUEST.
        midi_post_sds_rx_frame(data, length);
    } else if (  (length >= 5)
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
    msg_send(&gToMidiThread, msg);

    // Cut short the MIDI thread's current CFRunLoopRunInMode wait so the command is drained now
    // rather than up to one idle tick (33ms) later. CFRunLoopWakeUp is documented thread-safe.
    //
    // The run loop is read AFTER the send, not before. Reading it first meant a message posted while
    // the MIDI thread was still starting up saw NULL and skipped the wake even though the thread was
    // running by the time the message actually landed — it then sat in the queue for a whole idle
    // tick. Reading after the send cannot go stale in the direction that matters: if the run loop
    // exists once the message is queued, we signal it.
    CFRunLoopRef runLoop = atomic_load(&gMidiRunLoop);

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
    double          now = get_time_ms();

    // The gap since the previous event is what says whether this is one discrete change or part of a
    // stream — see LCD_STREAM_GAP_MS. Computed here because UI threads are the only writers of these.
    atomic_store(&gLastUiGapMs, now - gLastUiEventMs);
    gLastUiEventMs = now;             // UI threads are its only writer; the MIDI thread only reads it
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

void midi_post_sds_start(const tSampleDump * dump, uint16_t sampleNumber, uint8_t channel) {
    tMessageContent msg = {0};

    msg.cmd                       = eMsgCmdSdsStart;
    msg.sdsStartData.dump         = *dump;    // including the samples pointer: ownership moves
    msg.sdsStartData.sampleNumber = sampleNumber;
    msg.sdsStartData.channel      = channel;
    post_to_midi_thread(&msg);
}

void midi_post_sds_request(uint16_t sampleNumber, const char * path) {
    tMessageContent msg = {0};

    msg.cmd                         = eMsgCmdSdsRequest;
    msg.sdsRequestData.sampleNumber = sampleNumber;
    snprintf(msg.sdsRequestData.path, sizeof(msg.sdsRequestData.path), "%s", path);
    post_to_midi_thread(&msg);
}

void midi_post_sds_rx_frame(const uint8_t * data, uint32_t length) {
    tMessageContent msg = {0};

    if (length > sizeof(msg.sdsRxFrameData.data)) {
        return;
    }
    msg.cmd                   = eMsgCmdSdsRxFrame;
    msg.sdsRxFrameData.length = length;
    memcpy(msg.sdsRxFrameData.data, data, length);
    post_to_midi_thread(&msg);
}

bool midi_sds_rx_progress(uint32_t * wordsGot, uint32_t * wordsTotal, char * status, size_t statusMax) {
    if (wordsGot != NULL) {
        *wordsGot = atomic_load(&gSdsRxProgressGot);
    }

    if (wordsTotal != NULL) {
        *wordsTotal = atomic_load(&gSdsRxProgressTotal);
    }

    if ((status != NULL) && (statusMax > 0)) {
        snprintf(status, statusMax, "%s", gSdsRxStatus);
    }
    return atomic_load(&gSdsRxRunning);
}

void midi_post_sds_cancel(void) {
    tMessageContent msg = {0};

    msg.cmd = eMsgCmdSdsCancel;
    post_to_midi_thread(&msg);
}

void midi_post_sds_handshake(uint8_t type, uint8_t packet) {
    tMessageContent msg = {0};

    msg.cmd                     = eMsgCmdSdsHandshake;
    msg.sdsHandshakeData.type   = type;
    msg.sdsHandshakeData.packet = packet;
    post_to_midi_thread(&msg);
}

bool midi_sds_progress(uint32_t * packetsSent, uint32_t * packetsTotal, bool * closedLoop) {
    if (packetsSent != NULL) {
        *packetsSent = atomic_load(&gSdsProgress);
    }

    if (packetsTotal != NULL) {
        *packetsTotal = atomic_load(&gSdsTotal);
    }

    if (closedLoop != NULL) {
        *closedLoop = atomic_load(&gSdsProgressClosed);
    }
    return atomic_load(&gSdsActive);
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
                scanRequested  = true;
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
                // Gathered rather than sent: the flush below turns a flurry of single ticks into one
                // instruction, so the device redraws once instead of once per tick.
                gRotaryAccum  += (int)msg.rotaryEventData.delta;
                atomic_fetch_add(&gRotaryTicksIn, 1);
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
                // Any reply — probe, delta or whole frame — says what the screen shows right now, so
                // the idle poll has nothing to add for another interval. Counting from the reply
                // rather than from the last probe also stops requests bunching up behind a slow frame.
                gLcdLastProbeMs = get_time_ms();

                bool wasProbe = atomic_load(&gLcdProbeInFlight);

                // A probe reported movement: it told us THAT, never what. Fetch a whole frame, which
                // is the only thing allowed to change what is displayed.
                if (wasProbe) {
                    atomic_store(&gLcdProbeInFlight, false);

                    if (msg.lcdReplyData.changed) {
                        LOG_DEBUG("Probe saw the screen move; fetching a whole frame\n");
                        gLcdWantFull = true;
                    }
                }

                // A delta was just applied, so the screen is already showing the new state — that is
                // the fast part, ~200 ms rather than ~715. Chase it immediately with a whole frame
                // rather than waiting for the idle resync: the delta bought the perceived latency,
                // and the frame behind it confirms the picture is actually right. Waiting up to
                // LCD_RESYNC_IDLE_MS instead meant a delta that had drifted stayed on screen for
                // seconds, which is exactly the fault this whole exercise started with.
                if (  !msg.lcdReplyData.stale && !msg.lcdReplyData.wasFullFrame && !wasProbe
                   && msg.lcdReplyData.changed) {
                    gLcdWantFull = true;
                }

                // Did the user act while this was in flight? Then this delta describes a screen that
                // has already moved, and everything built on it inherits the gap. Correct it NOW
                // with a whole frame rather than deferring to the settle.
                //
                // Deferring was worse in both directions. It left a visibly wrong screen up for
                // seconds — measured at ten — because the chase below kept requesting deltas, and
                // the settle cannot fire while a request is wanted, so the corrective frame queued
                // behind the very deltas that could not fix it. And it bought nothing: during a
                // burst these deltas measured 571, 820 and 1001 ms against 715 ms for a whole frame,
                // so the "cheap" option was not cheaper. Deltas earn their keep on isolated input,
                // where they are 62-250 ms; under rapid input a full frame is both faster and right.
                if (  !msg.lcdReplyData.stale && !msg.lcdReplyData.wasFullFrame
                   && (gLastUiEventMs != gLcdReqUiStamp)) {
                    gLcdWantFull     = true;
                    gLcdDeltaSuspect = false;
                }

                if (msg.lcdReplyData.wasFullFrame) {
                    gLcdDeltaSuspect = false;
                }

                // The device is still catching up: it showed us a change we did not ask for by
                // pressing anything just now, which means more of its own backlog is still to come.
                // Ask again rather than leaving the screen wherever this reply happened to catch it.
                if (  !msg.lcdReplyData.stale && msg.lcdReplyData.changed && !gLcdWantFull
                   && (gLastUiEventMs == gLcdReqUiStamp)) {
                    if (gLcdChaseCount < LCD_CHASE_MAX) {
                        gLcdChaseCount++;
                        gLcdWantDelta = true;
                    } else {
                        LOG_DEBUG("LCD chase capped at %d — display may be animating\n", LCD_CHASE_MAX);
                    }
                } else if (!msg.lcdReplyData.changed) {
                    gLcdChaseCount = 0;   // caught up; the next burst starts with a fresh allowance
                }

                if (!msg.lcdReplyData.stale) {
                    gLcdLastReplyChanged = msg.lcdReplyData.changed;
                }
                break;

            case eMsgCmdSdsStart:
                sds_start(&msg.sdsStartData);
                break;

            case eMsgCmdSdsHandshake:
                sds_handshake(&msg.sdsHandshakeData);
                break;

            case eMsgCmdSdsCancel:
                sds_finish("cancelled locally");
                sds_rx_finish("cancelled locally", false);
                break;

            case eMsgCmdSdsRequest:
                sds_request(&msg.sdsRequestData);
                break;

            case eMsgCmdSdsRxFrame:
                sds_rx_frame(&msg.sdsRxFrameData);
                break;

            case eMsgCmdUiActivity:
                gLcdLastReplyChanged = true; // input means the screen is presumed moving again
                gLcdSettledOwn       = false;
                gLcdChaseCount       = 0;    // fresh input, so the chase allowance starts over

                // Restart the idle-poll clock too. A press already triggers its own refresh, so
                // letting the probe fire straight afterwards spends a round trip asking a question
                // we are in the middle of answering — and on a link this slow that queues behind the
                // very update the user is waiting for.
                gLcdLastProbeMs      = get_time_ms();
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

// ── Sample Dump Standard state machine (MIDI thread) ─────────────────────────

static void sds_finish(const char * why) {
    if (gSdsState != sdsIdle) {
        LOG_DEBUG("SDS transfer ended after %u/%u packets: %s\n",
                  (unsigned)gSdsPacket, (unsigned)gSdsPacketCount, why);
    }
    sample_dump_free(&gSdsDump);
    memset(&gSdsDump, 0, sizeof(gSdsDump));
    gSdsState       = sdsIdle;
    gSdsPacket      = 0;
    gSdsPacketCount = 0;
    gSdsAwaitingAck = false;
    gSdsClosedLoop  = false;
    atomic_store(&gSdsActive, false);
}

static void sds_send_packet(void) {
    uint8_t  frame[128];
    uint32_t len = sample_dump_build_packet(&gSdsDump, gSdsChannel, gSdsPacket, frame);

    if (len == 0) {
        sds_finish("complete");
        return;
    }
    midi_send(frame, len);
    gSdsLastSendMs  = get_time_ms();
    gSdsAwaitingAck = gSdsClosedLoop;   // in closed loop the ACK is what releases the next packet
    atomic_store(&gSdsProgress, gSdsPacket + 1);
}

static void sds_start(const tSdsStartData * start) {
    if (gSdsState != sdsIdle) {
        LOG_ERROR("SDS transfer already in progress; ignoring the new one\n");
        // The new sample's buffer would otherwise leak: nothing else owns it now.
        tSampleDump discard = start->dump;
        sample_dump_free(&discard);
        return;
    }
    gSdsDump         = start->dump;
    gSdsSampleNumber = start->sampleNumber;
    gSdsChannel      = start->channel;
    gSdsPacket       = 0;
    gSdsPacketCount  = sample_dump_packet_count(&gSdsDump);
    gSdsClosedLoop   = false;
    gSdsAwaitingAck  = false;

    uint8_t  header[24];
    uint32_t len = sample_dump_build_header(&gSdsDump, gSdsChannel, gSdsSampleNumber, header);

    LOG_DEBUG("SDS start: sample %u, %u words @ %u Hz, %u packets\n",
              (unsigned)gSdsSampleNumber, (unsigned)gSdsDump.frameCount,
              (unsigned)gSdsDump.sampleRate, (unsigned)gSdsPacketCount);
    midi_send(header, len);
    gSdsWaitStartMs  = get_time_ms();
    gSdsState        = sdsHeaderSent;
    atomic_store(&gSdsTotal, gSdsPacketCount);
    atomic_store(&gSdsProgress, 0);
    atomic_store(&gSdsActive, true);
    atomic_store(&gSdsProgressClosed, false);
}

// Any handshake at all proves the return cable is there, which is what closed loop means.
static void sds_handshake(const tSdsHandshakeData * hs) {
    if (gSdsState == sdsIdle) {
        return;
    }

    switch (hs->type) {
        case 0x7F:   // ACK
            gSdsClosedLoop  = true;
            gSdsAwaitingAck = false;
            atomic_store(&gSdsProgressClosed, true);

            if (gSdsState == sdsHeaderSent) {
                gSdsState = sdsSending;      // accepted; start sending packets
            } else {
                gSdsPacket++;                // that packet is confirmed, move on
            }
            break;

        case 0x7E:   // NAK — resend the same packet, so gSdsPacket deliberately does not advance
            gSdsClosedLoop  = true;
            gSdsAwaitingAck = false;
            atomic_store(&gSdsProgressClosed, true);
            LOG_DEBUG("SDS NAK on packet %u; resending\n", (unsigned)hs->packet);
            break;

        case 0x7C:   // WAIT — hold indefinitely; the next message decides
            gSdsClosedLoop  = true;
            gSdsAwaitingAck = true;
            gSdsLastSendMs  = get_time_ms();   // restart the patience clock
            LOG_DEBUG("SDS WAIT at packet %u\n", (unsigned)hs->packet);
            break;

        case 0x7D:   // CANCEL
            sds_finish("cancelled by the receiver");
            break;

        default:
            break;
    }
}

static void sds_tick(void) {
    switch (gSdsState) {
        case sdsIdle:
            return;

        case sdsHeaderSent:

            // Nothing came back, so there is no return cable: the standard says assume an open loop
            // and dump anyway. Slower and unverified, but it is what the standard prescribes.
            if ((get_time_ms() - gSdsWaitStartMs) >= SDS_HANDSHAKE_TIMEOUT_MS) {
                LOG_DEBUG("SDS: no handshake within %.0fms, assuming open loop\n",
                          SDS_HANDSHAKE_TIMEOUT_MS);
                gSdsClosedLoop = false;
                gSdsState      = sdsSending;
                gSdsLastSendMs = 0.0;
            }
            return;

        case sdsSending:

            if (gSdsPacket >= gSdsPacketCount) {
                sds_finish("complete");
                return;
            }

            if (gSdsAwaitingAck) {
                if ((get_time_ms() - gSdsLastSendMs) >= SDS_ACK_TIMEOUT_MS) {
                    sds_finish("timed out waiting for an acknowledgement");
                }
                return;
            }

            // Open loop has no acknowledgement to pace against, so it is paced by the clock instead.
            if (  !gSdsClosedLoop
               && ((get_time_ms() - gSdsLastSendMs) < SDS_OPEN_LOOP_PACE_MS)) {
                return;
            }
            sds_send_packet();
            return;
    }
}

// ── Receiving a sample (MIDI thread) ─────────────────────────────────────────

static void sds_rx_finish(const char * why, bool writeFile) {
    if (gSdsRxActive && writeFile && (gSdsRxSamples != NULL) && (gSdsRxGot > 0)) {
        char     err[128] = {0};
        // Whatever arrived is written even if the dump stopped short — a truncated sample is far
        // more use than nothing after a transfer measured in minutes.
        uint32_t count    = (gSdsRxGot < gSdsRxWords) ? gSdsRxGot : gSdsRxWords;

        if (sample_dump_write_wav(gSdsRxPath, gSdsRxSamples, count, gSdsRxRate,
                                  gSdsRxHasLoop, gSdsRxLoopStart, gSdsRxLoopEnd, err, sizeof(err))) {
            LOG_DEBUG("SDS receive: wrote %u samples to %s\n", (unsigned)count, gSdsRxPath);
            snprintf(gSdsRxStatus, sizeof(gSdsRxStatus), "done: %u samples -> %s", (unsigned)count, gSdsRxPath);
        } else {
            LOG_ERROR("SDS receive: %s\n", err);
            snprintf(gSdsRxStatus, sizeof(gSdsRxStatus), "write failed: %s", err);
        }
    } else if (gSdsRxActive) {
        snprintf(gSdsRxStatus, sizeof(gSdsRxStatus), "%s", why);
    }

    if (gSdsRxActive) {
        LOG_DEBUG("SDS receive ended (%u/%u words): %s\n",
                  (unsigned)gSdsRxGot, (unsigned)gSdsRxWords, why);
    }
    free(gSdsRxSamples);
    gSdsRxSamples    = NULL;
    gSdsRxActive     = false;
    gSdsRxHaveHeader = false;
    gSdsRxWords      = 0;
    gSdsRxGot        = 0;
    atomic_store(&gSdsRxRunning, false);
}

static void sds_request(const tSdsRequestData * req) {
    if (gSdsRxActive || (gSdsState != sdsIdle)) {
        LOG_ERROR("A sample transfer is already in progress\n");
        return;
    }
    sds_rx_finish("superseded", false);
    snprintf(gSdsRxPath, sizeof(gSdsRxPath), "%s", req->path);
    gSdsRxSampleNumber = req->sampleNumber;
    gSdsRxActive       = true;
    gSdsRxHaveHeader   = false;
    gSdsRxGot          = 0;
    gSdsRxWords        = 0;
    gSdsRxLastMs       = get_time_ms();
    snprintf(gSdsRxStatus, sizeof(gSdsRxStatus), "requested sample %u", (unsigned)req->sampleNumber);
    atomic_store(&gSdsRxRunning, true);
    atomic_store(&gSdsRxProgressGot, 0);
    atomic_store(&gSdsRxProgressTotal, 0);

    uint8_t  frame[8];
    uint32_t len = sample_dump_build_request(0, req->sampleNumber, frame);

    LOG_DEBUG("SDS: requesting sample %u -> %s\n", (unsigned)req->sampleNumber, gSdsRxPath);
    midi_send(frame, len);
}

static void sds_rx_header(const uint8_t * d) {
    // Three 7-bit bytes per field, least significant first.
    uint32_t period = (uint32_t)d[7] | ((uint32_t)d[8] << 7) | ((uint32_t)d[9] << 14);
    uint32_t words  = (uint32_t)d[10] | ((uint32_t)d[11] << 7) | ((uint32_t)d[12] << 14);
    uint32_t loopS  = (uint32_t)d[13] | ((uint32_t)d[14] << 7) | ((uint32_t)d[15] << 14);
    uint32_t loopE  = (uint32_t)d[16] | ((uint32_t)d[17] << 7) | ((uint32_t)d[18] << 14);

    if ((words == 0) || (words > SDS_MAX_WORDS) || (period == 0)) {
        sds_rx_finish("the dump header was not usable", false);
        return;
    }
    free(gSdsRxSamples);
    gSdsRxSamples    = (int16_t *)calloc(words, sizeof(int16_t));

    if (gSdsRxSamples == NULL) {
        sds_rx_finish("out of memory for the incoming sample", false);
        return;
    }
    gSdsRxWords      = words;
    gSdsRxGot        = 0;
    gSdsRxRate       = (uint32_t)((1000000000.0 / (double)period) + 0.5);
    gSdsRxHasLoop    = (d[19] != 0x7F) && (loopE > loopS);
    gSdsRxLoopStart  = loopS;
    gSdsRxLoopEnd    = loopE;
    gSdsRxHaveHeader = true;
    gSdsRxLastMs     = get_time_ms();
    LOG_DEBUG("SDS header in: %u words, %u bits, %u Hz, loop %s\n",
              (unsigned)words, (unsigned)d[6], (unsigned)gSdsRxRate, gSdsRxHasLoop ? "yes" : "none");
    snprintf(gSdsRxStatus, sizeof(gSdsRxStatus), "receiving %u samples @ %u Hz",
             (unsigned)words, (unsigned)gSdsRxRate);
    atomic_store(&gSdsRxProgressTotal, words);

    // ACK the header: that is what tells the sender to start sending packets, and it is what puts
    // the transfer into closed loop rather than a blind dump.
    uint8_t ack[8];

    midi_send(ack, sample_dump_build_handshake(0, 0x7F, 0, ack));
}

static void sds_rx_packet(const uint8_t * d, uint32_t length) {
    int16_t words[SDS_WORDS_PER_PACKET];
    uint8_t number = 0;
    uint8_t reply[8];

    if (!sample_dump_decode_packet(d, length, words, &number)) {
        // Bad checksum or framing: NAK and the sender repeats that very packet.
        LOG_ERROR("SDS packet failed its checksum; NAKing\n");
        midi_send(reply, sample_dump_build_handshake(0, 0x7E, number, reply));
        return;
    }

    for (uint32_t w = 0; (w < SDS_WORDS_PER_PACKET) && (gSdsRxGot < gSdsRxWords); w++) {
        gSdsRxSamples[gSdsRxGot++] = words[w];
    }

    gSdsRxLastMs = get_time_ms();
    atomic_store(&gSdsRxProgressGot, gSdsRxGot);
    midi_send(reply, sample_dump_build_handshake(0, 0x7F, number, reply));

    if (gSdsRxGot >= gSdsRxWords) {
        sds_rx_finish("complete", true);
    }
}

static void sds_rx_frame(const tSdsRxFrameData * frame) {
    if (!gSdsRxActive) {
        return;
    }
    const uint8_t * d = frame->data;

    if ((frame->length >= 21) && (d[3] == 0x01)) {
        sds_rx_header(d);
    } else if (d[3] == 0x02) {
        if (gSdsRxHaveHeader) {
            sds_rx_packet(d, frame->length);
        }
    }
}

static void sds_rx_tick(void) {
    if (!gSdsRxActive) {
        return;
    }

    // Nothing at all within the window means the sample number does not exist — the standard says a
    // request for an out-of-range sample is simply ignored, with no reply of any kind.
    if (!gSdsRxHaveHeader && ((get_time_ms() - gSdsRxLastMs) >= SDS_HANDSHAKE_TIMEOUT_MS)) {
        sds_rx_finish("no reply: that sample number is probably empty", false);
        return;
    }

    if (gSdsRxHaveHeader) {
        bool   tailOnly = (gSdsRxWords - gSdsRxGot) < SDS_WORDS_PER_PACKET;
        double quietFor = get_time_ms() - gSdsRxLastMs;

        // Everything but a part-packet is in, and nothing more has come: that is this device's idea
        // of finished, so take it as such rather than reporting a timeout on a good transfer.
        if (tailOnly && (quietFor >= SDS_TAIL_GRACE_MS)) {
            sds_rx_finish("complete", true);
        } else if (quietFor >= SDS_ACK_TIMEOUT_MS) {
            sds_rx_finish("the sender went quiet", true);
        }
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

        // Flush gathered encoder ticks. Done here rather than in the drain so a burst arriving as
        // several queued messages still leaves as one, and so a slow trickle still goes out promptly.
        if (  (gRotaryAccum != 0)
           && ((get_time_ms() - gRotaryLastSendMs) >= ROTARY_COALESCE_MS)) {
            peptalk_send_rotary_event(gRotaryAccum);
            atomic_fetch_add(&gRotaryMessagesOut, 1);
            gRotaryAccum      = 0;
            gRotaryLastSendMs = get_time_ms();
        }
        sds_tick();
        sds_rx_tick();

        // Poll: if session open, request LCD/LED updates as needed.
        //
        // Every want-bit below is private to this thread (see their declarations). Other threads ASK
        // via midi_post_lcd_refresh(); nothing outside this loop sets or clears them, so a request
        // cannot be cleared before it was served and a reply cannot retire a request it did not
        // answer.
        // A sample transfer owns the link while it runs. An LCD frame is 2205 bytes — most of a
        // second — and interleaving one would stall the dump and blur the progress for no gain,
        // since the screen is not what the user is watching during a transfer.
        if (gSessionOpen && (gSdsState == sdsIdle) && !gSdsRxActive) {
            // While a dial drag is held, poll for an LCD delta on a steady
            // throttled cadence — independent of whether new encoder ticks
            // are currently being sent. This covers both a long continuous
            // drag (ticks never stop long enough to "go quiet") and a held
            // but paused drag (no ticks at all) alike. Never sends a new
            // encoder value itself — that's only ever driven by dial_nudge().
            if (  gDialDragActive && !gLcdPendingOwn
               && ((get_time_ms() - gLastLcdPollMs) >= DIAL_LCD_POLL_INTERVAL_MS)) {
                // FULL frames while the wheel is moving, not deltas.
                //
                // A delta describes the device's screen as it was when the device built it, and the
                // wheel keeps moving underneath the ~700 ms it takes to arrive — so what lands is a
                // picture of a moment that has passed, and the device then reports "nothing changed"
                // and never corrects it. That is how the display came to show a preset the hardware
                // never displayed at all (P099, owner-confirmed absent from the device).
                //
                // It costs nothing to be right here: measured mid-burst, deltas took 516-1001 ms
                // against a flat 715 ms for a whole frame. The cheap option was not cheaper, only
                // wrong.
                gLcdWantFull   = true;
                gLastLcdPollMs = get_time_ms();
            }

            // Re-base the delta stream once the display has gone quiet. Deltas are XORs against
            // the frame we hold, so a lost one would leave the display wrong indefinitely; this
            // bounds that to LCD_RESYNC_IDLE_MS without ever putting a ~705 ms full frame in front
            // of a user who is still pressing keys. Deliberately skipped while a dial drag is held
            // — that path is a continuous stream of deltas and is quiet only once the drag ends.
            // Keep asking even when we believe we are in sync: the device never reports front-panel
            // activity, so polling is the only way to see it. Cheap, because an unchanged screen
            // answers in 61 ms — see LCD_IDLE_PROBE_MS.
            // No separate idle test: gLcdLastProbeMs is restarted by every input and every reply, so
            // this interval IS the debounce. Borrowing the resync's 4 s here meant a front-panel
            // change made shortly after touching the app went unseen for up to four seconds — and
            // the probe is the ONLY channel by which such a change reaches us.
            if (  gLcdBaseTrusted && !gLcdPendingOwn && !gDialDragActive
               && !gLcdWantFull && !gLcdWantDelta
               && ((get_time_ms() - gLcdLastProbeMs)
                   >= (atomic_load(&gWindowFocused) ? LCD_IDLE_PROBE_MS : LCD_UNFOCUSED_PROBE_MS))) {
                gLcdWantDelta   = true;
                gLcdLastProbeMs = get_time_ms();
            }

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
                // A full frame, not a delta, when the burst overlapped a transfer: that is the one
                // case where the delta stream cannot be trusted to have kept up, and the user has
                // just stopped, so it is the cheapest possible moment to spend ~715 ms putting the
                // picture beyond doubt.
                // A delta suffices here now that deltas are only applied for discrete events: the
                // trailing read just needs to catch the settled result. Drift is handled separately
                // and properly — applying any delta marks the base untrusted, and the resync then
                // fetches a whole frame once things have been quiet for LCD_RESYNC_IDLE_MS. That is
                // the "let the full refresh clean up" arrangement, with the delta never allowed to
                // be the thing that decides what is displayed for long.
                gLcdWantDelta  = true;
                gLcdSettledOwn = true;
            }

            // The display outranks the LEDs. They share one 31250-baud link, so an LED request sent
            // ahead of a pending screen update simply delays the thing the user is looking at; it
            // goes out only when there is no display work waiting.
            if (!gLcdPendingOwn) {
                extern void peptalk_send_lcd_dump_request(void);
                extern void peptalk_send_lcd_delta_request(void);
                extern uint8_t peptalk_last_request_seq(void);

                // Marked pending BEFORE sending, so a reply that arrives while we are still in this
                // block cannot be mistaken for there being nothing outstanding.
                // Give the device a moment to finish redrawing before asking what it now shows.
                // Only applies to the discrete case: a probe has no input to wait for, and while
                // streaming there is no settled state to wait for either.
                double settle  = atomic_load(&gPressSettleMs);
                bool   tooSoon = (settle > 0.0) && !gLcdWantFull
                                 && ((get_time_ms() - gLastUiEventMs) < settle);

                if ((gLcdWantFull || gLcdWantDelta) && !tooSoon) {
                    // A probe is worth it only when nothing has moved for a while: that is where
                    // "unchanged" is the likely answer and it costs 61 ms instead of 715.
                    bool idle      = (get_time_ms() - gLastUiEventMs) >= LCD_RESYNC_IDLE_MS;
                    bool probe     = LCD_PROBE_WHEN_IDLE && !gLcdWantFull && gLcdBaseTrusted
                                     && !gDialDragActive && idle;

                    // Streaming input: the last two events were close together AND one was just now.
                    // A delta would land describing a screen that has already moved past.
                    bool streaming = gDialDragActive
                                     || (  (atomic_load(&gLastUiGapMs) < LCD_STREAM_GAP_MS)
                                        && ((get_time_ms() - gLastUiEventMs) < LCD_STREAM_GAP_MS));
                    bool full      = !probe && (gLcdWantFull || streaming || !LCD_USE_DELTAS);

                    atomic_store(&gLcdProbeInFlight, probe);

                    gLcdWantFull   = false;
                    gLcdWantDelta  = false;          // a full frame answers a pending delta too
                    gLcdReqMsOwn   = get_time_ms();
                    gLcdReqUiStamp = gLastUiEventMs; // to spot the screen moving during the transfer
                    atomic_store(&gLcdReqUiStampSeen, gLcdReqUiStamp);
                    gLcdPendingOwn = true;
                    gLcdInFlightOwn++;

                    if (full) {
                        peptalk_send_lcd_dump_request();
                    } else {
                        peptalk_send_lcd_delta_request();
                    }
                    atomic_store(&gLcdOutstandingSeq, peptalk_last_request_seq());
                    atomic_store(&gLcdOutstandingValid, true);
                } else if (gLcdWantLeds) {
                    extern void peptalk_send_led_state_request(void);
                    gLcdWantLeds = false;
                    peptalk_send_led_state_request();
                }
            }
        }
        // Drive this thread's CFRunLoop so the midi_notify_cb fires here.
        // Use a short interval when work is in progress, idle at ~30 Hz otherwise.
        // A transfer wants the fast tick throughout: every packet waits on this loop to come round
        // and acknowledge it, so an idle 33 ms tick would be added to the cost of all several
        // hundred of them.
        bool   busy    = (gSdsState != sdsIdle)
                         || gSdsRxActive
                         || gLcdPendingOwn
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
