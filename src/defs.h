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

#ifndef __DEFS_H__
#define __DEFS_H__

// ── Build toggles ─────────────────────────────────────────────────────────────
#define ENABLE_DEBUG                1
#define ENABLE_LOG_DEBUG            1

// Window
#define WINDOW_TITLE                "EmuUtility"
#define TARGET_FRAME_BUFF_WIDTH     (2560)
#define TARGET_FRAME_BUFF_HEIGHT    (1440)     // 16:9, matching G2-Edit/SynthEdit

// LCD display geometry
#define LCD_WIDTH                   (240)
#define LCD_HEIGHT                  (64)
#define LCD_BYTES                   (LCD_WIDTH * LCD_HEIGHT / 8)            // 1920

// PEPTALK SysEx constants
#define EMU_MANUFACTURER_ID         (0x18)            // E-mu/Ensoniq
#define PEPTALK_DEST                (0x7F)            // broadcast destination

// PEPTALK message types
#define PEPTALK_SESSION_OPEN        (0x10)
#define PEPTALK_SESSION_CLOSE       (0x11)
#define PEPTALK_BUTTON_EVENT        (0x40)
#define PEPTALK_ROTARY_EVENT        (0x43)
#define PEPTALK_LCD_DUMP_RESP       (0x50)
#define PEPTALK_LCD_DUMP_REQ        (0x51)
#define PEPTALK_LCD_DELTA_REQ       (0x52)
#define PEPTALK_LCD_DELTA_RESP      (0x53)
#define PEPTALK_LED_STATE_REQ       (0x60)
#define PEPTALK_LED_STATE_RESP      (0x61)
#define PEPTALK_SESSION_STATUS      (0x7F)

// E-mu EOS device family (E4, E5000, etc.)
#define EMU_EOS_FAMILY              (1025)

// ── LCD refresh ──────────────────────────────────────────────────────────────
// How long the display has to stay quiet before one full frame is fetched to re-base the delta
// stream. Measured on a real E5000 over DIN MIDI (2026-08-19): a full frame is 2205 bytes on the
// wire = ~705 ms at 31250 baud, and the round trip measures 714-874 ms; the delta for a typical
// button press is 377 bytes / ~256 ms. So routine updates go by delta and the expensive full frame
// is deferred to a moment when nobody is waiting for it.
//
// Deliberately NOT eager. A resync cannot be recalled once its request is on the wire, so one
// firing just before a key press makes THAT press wait out the full frame ahead of it. At 1500 ms
// a user clicking every couple of seconds triggered one between almost every press; five seconds
// means only a genuine pause pays for it. The precise trigger lives elsewhere anyway — a delta that
// overruns the frame re-bases immediately (see peptalk_apply_lcd_delta) — so this timer only covers
// the case of a delta that was never delivered at all.
//
// Raised 5000 -> 20000, then cut back to 4000 the same day once it was PROVEN that this timer is
// the only thing that heals a diverged display. Captured live: the painted framebuffer and the pixel
// buffer were the identical wrong picture, so the renderer is faithful and the buffer really had
// drifted — and the delta immediately before showed 79 bytes, "nothing changed", while the next full
// frame reported 377 of 1920 bytes wrong. The device believes we are in sync when we are not, so
// deltas cannot reveal the drift and this timer is the cure, not insurance. Twenty seconds of a
// visibly wrong preset is far worse than one press occasionally queueing behind a 715 ms frame.
//
// Originally raised because A resync cannot be recalled once its request is on the wire,
// so if the user resumes inside the ~715 ms it takes, their input queues behind it: the hardware
// moves on and the screen catches up a beat later. A five-second pause is a normal part of using the
// thing, so that collision was reachable. Twenty seconds is a genuine walk-away.
//
// The cost of waiting longer is now much lower than it was when this value was chosen. Back then
// deltas were suspect and this was the only safety net; since, the actual causes have been found and
// fixed (foreign MIDI splicing into the reassembly buffer, replies mispaired with requests, the
// request state machine racing between two threads), and a delta that overruns the frame already
// forces an immediate re-base. This is now genuine insurance rather than routine maintenance.
// Deltas are no longer used at all — see LCD_USE_DELTAS. This remains as the idle re-read interval.
#define LCD_RESYNC_IDLE_MS    (4000.0)

// Whether to use the protocol's delta (XOR) refresh at all. OFF, deliberately.
//
// Delta CONTENT is never displayed. Deltas are used only as a change detector (LCD_PROBE_WHEN_IDLE),
// and whole frames are the only thing allowed to decide what is on screen.
//
// This is not conservatism, it is arithmetic. A delta is an XOR against "what I last sent you", and
// the device advances that reference whenever it sends one — including for a probe, whose payload we
// deliberately discard, and including for a reply the stale-guard throws away. Once the reference has
// moved and our buffer has not, the next delta computes as A^B and applying it to B yields exactly A:
// the OLD value, restored perfectly. That is the "old, new, old, new" bounce, and it is why deltas
// and probes cannot both be used: any discarded delta poisons every delta after it.
//
// The cost of giving this up is small and was measured: a delta carrying a real change took 446-1001
// ms against a flat ~715 ms for a whole frame, so displaying deltas was never much faster. The one
// genuinely cheap case, the 61 ms "nothing changed" reply, is retained — as a signal, not as data.
//
// That line is drawn where the evidence puts it. A delta describes the device's screen as it was
// when the device built it, ~700 ms before it arrives, so the hazard is a SECOND change overtaking
// the first. One button press is a single change that then settles, and the delta is both safe and
// three times cheaper: 200-270 ms against ~715 ms for a whole frame. A dial drag or a held Inc is a
// stream of changes, where the delta lands describing a moment that has passed — measured putting a
// preset on screen that the hardware never displayed at all.
//
// The failures below all came from using deltas for the STREAMING case, and are recorded so the
// attempts are not repeated:
//   * a delta describes the device's screen when the DEVICE built it, ~700 ms before it arrives;
//   * the device then reports "nothing changed" (79-byte reply) while our copy is measurably
//     hundreds of bytes wrong, so a delta can never REVEAL the drift it caused;
//   * deltas are computed against "what I last sent you", so discarding a stale one — which is
//     necessary to avoid painting an old screen — desynchronises that base and corrupts every
//     delta after it. Measured live: a full frame corrected 346 bytes, the next delta put the same
//     346 bytes back.
//
// And it buys nothing. Timed on real hardware, a delta carrying an actual change took 446-1001 ms
// against a flat ~715 ms for a whole frame. The only fast delta is the 61 ms "nothing changed" one,
// which is exactly the case where its answer cannot be trusted.
//
// Whole frames are ~1.4 per second and always correct. If this is ever revisited, the thing to fix
// first is the base-tracking, not the speed.
#define LCD_USE_DELTAS    (0)

// Deltas ARE still worth having, but only as a change DETECTOR, and only while nothing is moving.
//
// Every failure above came from applying delta CONTENT. Used purely to answer "has anything changed
// since you last told me?", none of it applies: the payload is thrown away, so it cannot corrupt the
// frame, and the base cannot drift because we only ever probe from a state a whole frame has just
// verified. If the answer is yes, a whole frame is fetched and THAT is what gets shown.
//
// The economics are the point. An unchanged screen answers in 61 ms against ~715 ms for a frame, so
// idling costs a tenth of what polling whole frames would, and a change made on the sampler's own
// front panel is still noticed within one poll. A changed screen costs 61 + 715, slightly worse than
// a frame alone — which is why this is used only when idle, where "unchanged" is the overwhelmingly
// common answer, and never during input, where whole frames go out directly.
#define LCD_PROBE_WHEN_IDLE    (1)

// How often to probe while idle.
//
// This is not optional politeness — it is the ONLY way we learn about anything done on the sampler's
// own front panel. Message types actually received from the device are 0x50 (LCD reply), 0x61 (LED
// reply) and 0x7F (session status); the first two only ever answer a request, and no button (0x40)
// or rotary (0x43) echo has EVER arrived. The device simply does not volunteer that its screen
// changed, so without polling, turning a knob on the unit itself would leave our display wrong
// indefinitely.
//
// This doubles as the DEBOUNCE. The clock is restarted by every input and by every reply, so a probe
// can only ever fire once things have been quiet for this long — which is why the probe needs no
// separate "am I idle" test. During a burst, inputs arrive closer together than this and no probe
// goes out at all; the moment you stop, polling resumes within 150 ms.
//
// At 61 ms per probe that is roughly 40% of the link while genuinely idle, which costs nothing since
// nothing else wants it then, and it surfaces a front-panel change in about a sixth of a second. Nothing is competing for the wire at that point: whole frames only go out when
// something actually changed, and the entire LCD block is skipped while a sample transfer owns the
// link (see the gSdsState / gSdsRxActive gate in midi_thread), so a dump is never slowed by this.
#define LCD_IDLE_PROBE_MS    (150.0)

// Two input events closer together than this mean the user is STREAMING — holding Inc, or working
// the dial — rather than making one discrete change.
//
// This is the real distinction, and it is about timing rather than which control was used: an
// isolated Inc press is as safe for a delta as a function key, while a rapid run of them is exactly
// the case where a delta lands describing a screen that has already moved on. Whole frames are used
// while streaming, deltas otherwise.
#define LCD_STREAM_GAP_MS    (400.0)

// How long to wait after an input before asking the device what its screen now shows.
//
// We send the button event and the refresh request back to back, microseconds apart, and rely on the
// sampler servicing them in order. In-order servicing guarantees the redraw STARTS first; it does not
// guarantee the redraw has FINISHED when the device snapshots the screen for our reply. If it has
// not, we get a half-drawn frame — which would look exactly like brief corruption.
//
// Default 0 until measured. midi_set_press_settle_ms() makes it settable at runtime so the sweep can
// decide the value rather than intuition.
#define LCD_PRESS_SETTLE_MS    (0.0)

// How long an LCD request may stay in flight before it is written off. gLcdPending exists to keep
// one request on the wire at a time, but nothing ever cleared it except a reply — so a single lost
// or corrupted response left it set forever and the display simply stopped updating, with no error
// and no way back short of a restart. That mattered less when every update was a full frame the
// user was already waiting on; it matters more now the delta stream is the normal path. Comfortably
// clear of the worst round trip measured.
//
// Raised 3000 -> 8000 on 2026-08-20. At 3 s it was firing on responses that were merely SLOW, not
// lost — under load (an unsolicited session status forcing a full frame, an LED request sharing the
// link) a reply can take well over 3 s. Timing one out issues a second request while the first is
// still on its way, and the late reply is then read as the answer to the new one: a stale delta gets
// applied to a frame that has moved on. That was measured corrupting 295 to 1286 of 1920 bytes. The
// gLcdInFlight counter makes that safe even when it does happen; this just stops it happening for
// no reason.
#define LCD_REQUEST_TIMEOUT_MS    (8000.0)

// How long after the last button press or dial detent to take ONE more delta.
//
// Every other refresh in this app is triggered BY an event, and its request goes out in the same
// breath as the event that caused it — so the last request of a burst can be answered before the
// device has finished acting on the final event, and then nothing asks again. The display sits one
// step behind the hardware until the 5 s resync, which is far too slow to read as responsive. This
// is the trailing request that catches the landing point: the dial's own 120 ms polling stops dead
// when the drag ends, and coalesced Inc/Dec presses have the same gap.
//
// Comfortably past the device's own turnaround (~130 ms fixed overhead, measured), while still
// feeling immediate. Costs one small delta (~200 ms) per burst, not per event.
#define LCD_SETTLE_MS    (250.0)

// How many times in a row the display may be re-read purely because the LAST read showed it moving.
//
// Input stops, but the DEVICE does not: it is still working through the button events already sent,
// and its screen keeps changing for a while afterwards. One trailing refresh samples that backlog
// mid-flight and then nothing asks again, which is how the screen ends up a whole preset behind and
// stays there — measured as 35 of 64 rows wrong at the settle point, corrected only by the next
// manual full dump. So a refresh that comes back CHANGED asks once more, until one comes back empty:
// "nothing has changed since you last asked" is the device telling us it has caught up.
//
// Capped because some screens move on their own — a meter, anything animated — and chasing one of
// those would poll the link forever. Hitting the cap simply stops until the next input or the idle
// resync, which is the right way to lose this race.
#define LCD_CHASE_MAX    (8)

// How long encoder ticks are gathered up before being sent as ONE event.
//
// A mouse drag produces a tick per cursor move — dozens a second — and each is a separate PEPTALK
// message the device must act on, redrawing its screen every time. That is what makes a drag bursty,
// and a screen changing under a ~700 ms transfer is precisely what puts the delta stream out of step.
// The wheel is a relative control, so N ticks of +1 and one tick of +N are the same instruction: the
// device lands on the same preset either way, having redrawn once instead of N times.
//
// Deliberately short. Long enough to collapse a flurry of mouse-move ticks, far too short to be felt
// as lag on the control itself.
#define ROTARY_COALESCE_MS    (40.0)

// ── Sample Dump Standard transfer ────────────────────────────────────────────
// How long to wait after the DUMP HEADER before concluding nobody is handshaking. The standard sets
// this at two seconds: the receiver needs that long to decide whether it has the memory, and if
// nothing comes back the sender must assume an open loop and dump regardless.
#define SDS_HANDSHAKE_TIMEOUT_MS    (2500.0)

// Open-loop pacing. With no acknowledgements there is nothing to pace against, and CoreMIDI would
// happily accept thousands of packets we cannot possibly have sent yet. A 127-byte packet occupies
// 127 * 10 / 31250 = 40.6 ms of wire, so this sends just slower than the link drains. Closed loop
// needs none of this — the ACK is the pacing.
#define SDS_OPEN_LOOP_PACE_MS    (45.0)

// How long to wait for an acknowledgement before giving up on the whole transfer. Generous: the
// receiver is entitled to send WAIT and go off to do housekeeping for a while.
#define SDS_ACK_TIMEOUT_MS    (30000.0)

// Grace period once only a PARTIAL packet's worth of the declared length is still outstanding.
//
// A real E5000 declares its length in words but sends only whole packets: asked for a 13681-word
// sample it sent 342 packets — 13680 words — and stopped, one word short of its own header. Waiting
// out SDS_ACK_TIMEOUT_MS for a packet that is never coming wastes half a minute and reports failure
// for a transfer that actually succeeded, so the tail gets a short grace instead.
#define SDS_TAIL_GRACE_MS    (2500.0)

// Extra words appended to every outgoing sample to absorb what this sampler discards.
//
// An E5000 stores THREE FEWER samples than it is sent, every time. Measured 2026-08-20 by sending
// ramps with a unique value per sample and reading the device's own Sample Manage > Info page:
//   sent 400  -> "Length: 397 samples"
//   sent 2000 -> "Length: 1997 samples"
//   sent 4000 -> "Length: 3997 samples"
// Constant across a tenfold length range and across 26000 and 44100 Hz, and the surviving audio is
// contiguous — so it is a fixed edge effect, not a proportional loss and not a dropout.
//
// Compensated by padding the tail, so the three the device throws away are padding rather than the
// end of the sample. The pad repeats the final sample value rather than using zero: a sample ending
// away from zero would otherwise get a step discontinuity — a click — welded onto its end.
//
// Only the tail is fixable. Reading a sample BACK loses one sample off the FRONT (and the device
// pads the length up to a multiple of 40 with 0x8000 words), which nothing at this end can recover,
// since we cannot ask it to start earlier. At 26-44 kHz that is 23-38 microseconds.
#define SDS_EMU_TAIL_PAD    (3)


// ── Computer-keyboard note entry ─────────────────────────────────────────────
// Notes go out as ordinary MIDI, not PEPTALK: PEPTALK drives the front panel, and a note is not a
// front-panel event. The sampler plays them on its own basic channel, so this must match whatever
// that is set to (MASTER > MIDI on the device). Channel 1 is the factory default, and 0 here is
// the wire value for it — MIDI channels are 1-based on a front panel and 0-based on the wire.
#define NOTE_ENTRY_MIDI_CHANNEL       (0)
#define NOTE_ENTRY_VELOCITY           (100)
#define NOTE_ENTRY_FIRST_NOTE         (48)     // C3 — the note the 'a' key plays before any octave shift
#define NOTE_ENTRY_MAX_NOTE           (127)

#define MIDI_NOTE_OFF                 (0x80)
#define MIDI_NOTE_ON                  (0x90)

// MIDI identity request (Universal SysEx)
#define MIDI_SYSEX_START              (0xF0)
#define MIDI_SYSEX_END                (0xF7)
#define MIDI_NON_REALTIME             (0x7E)
#define MIDI_DEVICE_INQUIRY           (0x7F)          // all-call
#define MIDI_IDENTITY_REQUEST_SUB1    (0x06)
#define MIDI_IDENTITY_REQUEST_SUB2    (0x01)
#define MIDI_IDENTITY_REPLY_SUB2      (0x02)

// ── Graphics / layout constants (used by synthlibDefs / utilsGraphics) ───────
#define MENU_BAR_HEIGHT               (24.0)

// ── Colour macros ─────────────────────────────────────────────────────────────

#endif // __DEFS_H__
