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
#include "peptalk.h"
#include "midiComms.h"

static void (* gWakeCb)(void) = NULL;
static pthread_t gMidiThread  = 0;
static pthread_mutex_t gSendMutex = PTHREAD_MUTEX_INITIALIZER;

// ── MIDI notification callback ────────────────────────────────────────────────

static void midi_notify_cb(const MIDINotification * msg, void * refCon) {
    (void)refCon;

    if (msg->messageID == kMIDIMsgSetupChanged) {
        LOG_DEBUG("CoreMIDI setup changed\n");
        midi_scan_devices();
        atomic_store(&gReDraw, true);

        if (gWakeCb != NULL) {
            gWakeCb();
        }
    }
}

// ── MIDI read callback (called on MIDI thread) ────────────────────────────────

static void midi_read_cb(const MIDIPacketList * pktList, void * readProcRefCon, void * srcConnRefCon) {
    (void)readProcRefCon;
    (void)srcConnRefCon;

    const MIDIPacket * pkt = &pktList->packet[0];

    for (uint32_t i = 0; i < pktList->numPackets; i++) {
        if ((pkt->length > 0) && (pkt->data[0] == MIDI_SYSEX_START)) {
            peptalk_handle_message(pkt->data, pkt->length);

            if (gWakeCb != NULL) {
                gWakeCb();
            }
        }
        pkt = MIDIPacketNext(pkt);
    }
}

// ── Device scanning ───────────────────────────────────────────────────────────

int midi_scan_devices(void) {
    ItemCount srcCount  = MIDIGetNumberOfSources();
    ItemCount destCount = MIDIGetNumberOfDestinations();

    gMidiSource = 0;
    gMidiDest   = 0;
    memset(&gDevice, 0, sizeof(gDevice));

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
        // Accept any source for now; will filter on identity response
        if (gMidiSource == 0) {
            gMidiSource = src;
        }
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

        if (gMidiDest == 0) {
            gMidiDest = dest;
        }
    }

    if ((gMidiSource != 0) && (gMidiDest != 0)) {
        // Connect source to our input port
        MIDIPortConnectSource(gMidiInPort, gMidiSource, NULL);
        LOG_DEBUG("Connected MIDI source to input port\n");

        midi_send_identity_request();
        return EXIT_SUCCESS;
    }

    LOG_DEBUG("No MIDI sources/destinations found\n");
    return EXIT_FAILURE;
}

// ── Identity request ──────────────────────────────────────────────────────────

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
    midi_send(idReq, sizeof(idReq));
}

// ── Send ──────────────────────────────────────────────────────────────────────

void midi_send(const uint8_t * data, uint32_t length) {
    if ((gMidiOutPort == 0) || (gMidiDest == 0) || (data == NULL) || (length == 0)) {
        return;
    }

    uint8_t             buf[512 + sizeof(MIDIPacketList)];
    MIDIPacketList    * pktList = (MIDIPacketList *)buf;
    MIDIPacket        * pkt     = MIDIPacketListInit(pktList);

    pkt = MIDIPacketListAdd(pktList, sizeof(buf), pkt, 0, length, data);

    if (pkt == NULL) {
        LOG_ERROR("MIDIPacketListAdd failed (message too long?)\n");
        return;
    }

    pthread_mutex_lock(&gSendMutex);
    OSStatus err = MIDISend(gMidiOutPort, gMidiDest, pktList);
    pthread_mutex_unlock(&gSendMutex);

    if (err != noErr) {
        LOG_ERROR("MIDISend error %d\n", (int)err);
    }
}

// ── MIDI poll thread ──────────────────────────────────────────────────────────

static void * midi_thread(void * arg) {
    (void)arg;

    LOG_DEBUG("MIDI thread started\n");

    midi_scan_devices();

    while (!atomic_load(&gQuitAll)) {
        // Poll: if session open, request LCD/LED updates as needed
        if (atomic_load(&gSessionOpen)) {
            if (atomic_load(&gNeedLeds)) {
                extern void peptalk_send_led_state_request(void);
                peptalk_send_led_state_request();
            } else if (atomic_load(&gNeedLcdFull)) {
                extern void peptalk_send_lcd_dump_request(void);
                peptalk_send_lcd_dump_request();
            } else if (atomic_load(&gNeedLcdDelta)) {
                extern void peptalk_send_lcd_delta_request(void);
                peptalk_send_lcd_delta_request();
                atomic_store(&gNeedLcdDelta, false);
            }
        }

        struct timespec ts = {0, 33000000};   // ~30 Hz poll
        nanosleep(&ts, NULL);
    }

    LOG_DEBUG("MIDI thread exiting\n");
    return NULL;
}

// ── Startup ───────────────────────────────────────────────────────────────────

int start_midi_thread(void) {
    CFStringRef clientName = CFSTR("EmuUtility");
    OSStatus    err;

    err = MIDIClientCreate(clientName, midi_notify_cb, NULL, &gMidiClient);

    if (err != noErr) {
        LOG_ERROR("MIDIClientCreate failed: %d\n", (int)err);
        return EXIT_FAILURE;
    }

    CFStringRef inName = CFSTR("EmuUtility In");
    err = MIDIInputPortCreate(gMidiClient, inName, midi_read_cb, NULL, &gMidiInPort);

    if (err != noErr) {
        LOG_ERROR("MIDIInputPortCreate failed: %d\n", (int)err);
        return EXIT_FAILURE;
    }

    CFStringRef outName = CFSTR("EmuUtility Out");
    err = MIDIOutputPortCreate(gMidiClient, outName, &gMidiOutPort);

    if (err != noErr) {
        LOG_ERROR("MIDIOutputPortCreate failed: %d\n", (int)err);
        return EXIT_FAILURE;
    }

    if (pthread_create(&gMidiThread, NULL, midi_thread, NULL) != 0) {
        LOG_ERROR("pthread_create for MIDI thread failed\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void register_midi_wake_cb(void (* cb)(void)) {
    gWakeCb = cb;
}
