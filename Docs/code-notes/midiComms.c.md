# midiComms.c notes

The longer comments from `midiComms.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `gMidiRunLoop`

The MIDI thread's own CFRunLoop, captured once the thread is up. Posting a command signals it so
the drain happens promptly instead of waiting out the current CFRunLoopRunInMode interval (up to
33ms when idle — enough to make an on-screen button press feel laggy). Written once by the MIDI
thread, read by posting threads; NULL until then, in which case the command still gets drained on
the first tick, just without the early wake.

## 2. `gLcdWantFull`

── LCD request state — MIDI THREAD ONLY ─────────────────────────────────────
Every one of these used to be an _Atomic global written by BOTH this thread and the CoreMIDI read
callback. Individually atomic, but the SEQUENCES were not, and two races fell out of that:

```
  * a want-bit set by the callback between this thread's read and its clear was silently dropped,
    leaving the display stale until the settle or the 5 s resync;
  * the callback's "gLcdPending = false" racing this thread's "gLcdPending = true" let a second
    request go out while the first was still on the wire — the very overlap that makes a stale
    delta land on a frame that has moved on, which is measurable corruption.

```
They are plain statics now, not atomics, because exactly one thread touches them. Everyone else
POSTS (midi_post_lcd_refresh / midi_post_lcd_reply) and this thread decides what it means — the
same split handle_identity_reply() already uses, and the ownership rule msgQueue.h sets out.
Coalescing still works: N refresh requests all set the same bit, so they collapse into one
transfer, but none of them can be lost.

## 3. `gRotaryTicksIn`

What gLastUiEventMs read when the outstanding request went out, and whether the answer that came
back can be trusted as a base for further deltas.

A delta describes the screen as it was when the DEVICE computed it. If the user keeps pressing
while that ~700 ms transfer is on the wire, the screen moves on underneath it, and the device's
idea of "what I last sent you" and ours drift apart by exactly the changes made during the
transfer. Measured: 383-447 of 1920 bytes wrong, persisting until a full frame. Deltas stay
correct for isolated input — verified bit-exact — so the answer is not to abandon them, but to
notice the one condition that spoils them and re-base once the burst is over.
Encoder ticks waiting to go out as one message — see ROTARY_COALESCE_MS.

## 4. `eSdsState`

── Sample Dump Standard transfer — MIDI THREAD ONLY ─────────────────────────
Minutes long, so it is a state machine ticked from the poll loop rather than a blocking loop: the
thread still has to service its run loop and its command queue throughout. The handshake arrives
on the CoreMIDI callback thread and is POSTED here, same ownership rule as everything else.

## 5. `gSdsRxActive`

── Receiving a sample FROM the device ───────────────────────────────────────
The safe direction: a DUMP REQUEST changes nothing on the sampler, where sending a sample TO it
overwrites whichever one is selected. Same thread rules — the callback hands frames over, this
thread verifies, assembles and answers.

## 6. `midi_rotary_counts()`

Is this reply one we should refuse to apply?

A sequence mismatch ALONE is not enough, and assuming it was cost real updates. The device also
speaks unprompted — the session-status message is one such, and LCD replies turn up carrying ids
we never sent — so a mismatched reply is usually perfectly good data that simply was not an answer
to our outstanding request. Discarding those left the display several presets behind the hardware
and stuck there, because the request stayed marked pending with nothing left to answer it.

A reply can only be genuinely STALE if more than one request is actually outstanding, which
happens solely when the timeout gave up on one and sent another. That is the discriminator: the
mismatch says WHICH reply is the old one, the count says whether an old one can exist at all.
See midiComms.h. Read from the render thread by the backdoor; the fields are this thread's, and a
slightly stale read only ever delays a test by one tick.

## 7. `midi_set_press_settle_ms()`

Does the reply now in hand describe a screen that has already moved on?

A transfer takes ~700 ms. If the user touched anything while it was in flight, what arrived is a
picture of a moment that has passed — and painting it puts an OLD value on screen over a newer
one. Measured directly: a full frame requested at mouse-down arrived 719 ms later still showing
P000 while the device had already moved to P011, so the display went P011 -> P000 -> P011.

Better to keep showing the last frame we believed than to paint a stale one. The caller asks
again, and the next reply — taken after the movement — is correct.

## 8. `SYSEX_BUF_SIZE`

SysEx reassembly — CoreMIDI fragments large messages across multiple packets, so the bytes have
to be gathered up until F7 before anything can be made of them.

ONE BUFFER PER SOURCE, and that is the whole point. midi_scan_devices() connects EVERY MIDI source
on the machine (it has to — an identity reply can come from any of them), so on a real rig this
callback sees a dozen devices' traffic interleaved. A single shared buffer therefore let one
device's bytes land in the middle of another's message. That was not theoretical: with a 2205-byte
LCD frame taking ~705 ms to arrive over DIN, a controller streaming CCs in running status spliced
its data bytes straight into the E-mu's payload — measured 2026-08-20, payloads arriving at 2363
to 2759 bytes instead of 2205 and unpacking to 1936 where a frame is exactly 1920. A CC WITH its
status byte was no better: it took the "any other status byte aborts" path and destroyed the
transfer outright. Both were visible as corruption or a stalled display.

No locking: CoreMIDI calls a port's read proc on one dedicated thread, and this app has a single
input port, so every callback for every source is serialised onto that one thread. The defect was
logical, not a race.

## 9. `gSysExAcceptSrc`

Once we have locked onto the E-mu there is nothing this app wants from any other device, so its
traffic is dropped at the door rather than merely kept in its own slot. Belt and braces over the
per-source buffers above: those make interleaving HARMLESS, this stops it being delivered at all,
and it keeps the CoreMIDI callback off the hot path for a dozen devices we do not care about.

Zero while no device is locked on, which is exactly the window a scan needs — midi_scan_devices()
clears it before sending identity requests, so replies from every source still get through, and
handle_identity_reply() sets it once a device answers.

Deliberately NOT a read of gMidiSource: that belongs to the MIDI thread (see globalVars.h), and
this is read on the CoreMIDI callback thread. Its own atomic, written by the owning thread.

## 10. in `midi_send_to()`

THE PACKING AND THE SEND ARE SHARED NOW — see SynthLib's synthlibMidi.h. Both editors carried
their own copy of this, character-identical apart from a log string, and differing only in the
two ways that had already caused a real fault here: a 512-byte stack buffer that silently
failed a whole-bank restore, and no return value for the caller to notice with.

## 11. `gDestProbeActive`

── Destination probe (MIDI thread only) ─────────────────────────────────────

WHICH DESTINATION THE SAMPLER IS ACTUALLY LISTENING ON.

A scan broadcasts an identity request to EVERY destination, so the reply proves only which
SOURCE the device speaks on. handle_identity_reply() then INFERS the destination from that
source's entity — the right answer whenever one port's In and Out are the same physical socket
pair, and wrong the moment they are not. On a multi-port interface the sampler's Out can be
patched to one port while its In hangs off another, and then every PEPTALK message after the
identity reply goes into a hole.

That failure was completely silent, and cost a whole debugging session: the device answers the
broadcast identity request, the app reports connected=yes, and nothing else ever happens — no
LCD, no LEDs, no button ever reaching the panel — because the session was never opened.

So the entity's destination is now a GUESS THAT MUST BE PROVED. It is tried first, and if no
session status comes back the remaining destinations are tried one at a time until one answers.
The reply is the proof: only the destination the sampler is really listening on can produce it.

WHICH probe a reply proves is read off the protocol rather than off the clock. Every session open
carries a sequence id, and the session status echoes it back (in byte 3 — see peptalk.c), so the
answer names the request that earned it. That is what makes the probe both fast and exact: the
opens may overlap on the wire without any risk of crediting the wrong destination, where a purely
time-based probe has to leave a gap longer than the worst round trip and still cannot be sure.

## 12. `handle_identity_reply()`

── Identity reply ────────────────────────────────────────────────────────────
Split across two threads on purpose. The CoreMIDI read callback only validates and unpacks the
reply (parse_identity_reply, below) then posts it; the MIDI thread does the entity/destination
lookup and the writes to gDevice/gMidiSource/gMidiDest, because it owns that state. Before the
split, the callback thread wrote all three while the MIDI thread's own scan/poll loop was reading
and rewriting them — the same unsynchronized-ownership bug SynthEdit found and fixed on its side
(see midi_request_reconnect()'s comment there).

## 13. in `handle_identity_reply()`

NAMED, not just numbered. An identity request goes to EVERY destination during a scan, so
a reply proves only which SOURCE the device speaks on — the destination is then INFERRED
from that source's entity, and an interface that does not pair them that way leaves us
talking to the wrong port. That failure is completely silent: the device answers the
broadcast identity request and then never answers anything again. Printing both names is
what makes it visible.

## 14. in `session_probe_tick()`

gMidiDest walks the candidates because midi_send() is where the destination lives, and there
is nothing else on the wire to disturb: until a session is open this app sends session opens
and nothing else. Which one was RIGHT is settled by the reply, not by where the walk stopped
— handle_session_status() sets it from the sequence id — so the walk may run ahead of the
replies without ever crediting the wrong port.

## 15. in `midi_notify_cb()`

This notification is delivered on the MIDI thread's own CFRunLoop (the client was created
there), so calling midi_scan_devices() directly here would in fact be safe. It still goes
through the queue: it keeps ONE rule — "the scan runs from the drain" — rather than one
safe direct caller plus a rule everyone else has to remember, and it means a setup change
arriving mid-drain queues behind the command already being serviced instead of re-entering
the scan from underneath it.

## 16. in `dispatch_sysex()`

Sample Dump Standard handshake: F0 7E cc <7C WAIT|7D CANCEL|7E NAK|7F ACK> pp F7. Universal
non-realtime like the identity reply, but a different sub-id, so there is no ambiguity. Caught
here because peptalk_handle_message() would reject it — the manufacturer byte is 7E, not E-mu's
18 — and the transfer would silently fall back to open loop.

## 17. in `midi_scan_devices()`

A SESSION BELONGS TO A CONNECTION, and this scan has just thrown the connection away. Nothing
cleared this before, so after a rescan — a hub replug fires one on its own, see midi_notify_cb
— the app went on believing a session was open on a destination it no longer had, the panel
went on claiming "open", and the destination probe below would have taken that stale flag as
proof that its first guess was right.

## 18. in `midi_scan_devices()`

A CHOSEN OUTPUT is the only one asked, and one that is not plugged in is waited for rather than
replaced by whatever else is on the rig: the setup-changed notification rescans when it
appears. Sources are still all connected above - a reply is filtered by the chosen input in
handle_identity_reply(), where the source it came from is known.

## 19. in `midi_scan_devices()`

Small stagger between each destination's identity request — ported
from SynthEdit's identical fix (midiComms.c, 2026-07-13), found
debugging a Korg Z1 that connected in under a second alone but
took 20+ seconds or hung entirely with 3 other synths (Moog
Minitaur, Waldorf Pulse, ASM Hydrasynth) sharing the same
interface. Blasting every destination's request back-to-back with
zero gap made all their replies land at nearly the same instant on
the interface's merged input, where they likely collide/corrupt
rather than interleave cleanly, rather than any bug in the
matching logic itself. Matters more here than in SynthEdit: this
function only runs once at startup (or on a CoreMIDI setup-change
notification, see midi_notify_cb above) with no periodic retry
loop behind it, so a single collision leaves the E-mu undetected
until something re-triggers a rescan, rather than quietly
succeeding a couple seconds later. CFRunLoopRunInMode, not
usleep/nanosleep — this thread is CFRunLoop-driven throughout (see
midi_thread()'s own comment above), not the platform-thread model
those assume.

## 20. in `post_to_midi_thread()`

Cut short the MIDI thread's current CFRunLoopRunInMode wait so the command is drained now
rather than up to one idle tick (33ms) later. CFRunLoopWakeUp is documented thread-safe.

The run loop is read AFTER the send, not before. Reading it first meant a message posted while
the MIDI thread was still starting up saw NULL and skipped the wake even though the thread was
running by the time the message actually landed — it then sat in the queue for a whole idle
tick. Reading after the send cannot go stale in the direction that matters: if the run loop
exists once the message is queued, we signal it.

## 21. `drain_midi_commands()`

── Command drain (MIDI thread) ──────────────────────────────────────────────
Poll-drained, NOT blocked on (eRcvPoll, not eRcvWait as G2-Edit's USB thread uses): this thread
has to keep driving its CFRunLoop so midi_notify_cb fires, and it has its own time-based polling
cadence (the LCD delta throttle and the idle tick below). Blocking in msg_receive would stall
both. Drains everything queued each tick — unlike the render loop's one-per-frame drain in
G2-Edit, nothing here is modal, so there is no reason to spread the work across ticks.

eMsgCmdScanDevices is COALESCED: however many arrived this tick, the scan runs at most once,
after the rest of the batch. A rescan is a request for a state ("be freshly scanned"), not a
discrete event, so N of them must collapse to one — the same rule reverse-queue-design.md gives
for gotPatchChangeIndication et al., applied to a command rather than a response. It matters
here: a single hub plug/unplug can fire several kMIDIMsgSetupChanged notifications, and each scan
walks every destination sending a staggered identity request (15ms apart, see midi_scan_devices),
so running the scan per message would multiply that up for no gain. Deferring it to the end of
the batch is also the right order — identity replies still in the queue belong to the PREVIOUS
scan and should be handled against the connection state that produced them.

## 22. in `drain_midi_commands()`

A delta was just applied, so the screen is already showing the new state — that is
the fast part, ~200 ms rather than ~715. Chase it immediately with a whole frame
rather than waiting for the idle resync: the delta bought the perceived latency,
and the frame behind it confirms the picture is actually right. Waiting up to
LCD_RESYNC_IDLE_MS instead meant a delta that had drifted stayed on screen for
seconds, which is exactly the fault this whole exercise started with.

## 23. in `drain_midi_commands()`

Did the user act while this was in flight? Then this delta describes a screen that
has already moved, and everything built on it inherits the gap. Correct it NOW
with a whole frame rather than deferring to the settle.

Deferring was worse in both directions. It left a visibly wrong screen up for
seconds — measured at ten — because the chase below kept requesting deltas, and
the settle cannot fire while a request is wanted, so the corrective frame queued
behind the very deltas that could not fix it. And it bought nothing: during a
burst these deltas measured 571, 820 and 1001 ms against 715 ms for a whole frame,
so the "cheap" option was not cheaper. Deltas earn their keep on isolated input,
where they are 62-250 ms; under rapid input a full frame is both faster and right.

## 24. in `drain_midi_commands()`

Restart the idle-poll clock too. A press already triggers its own refresh, so
letting the probe fire straight afterwards spends a round trip asking a question
we are in the middle of answering — and on a link this slow that queues behind the
very update the user is waiting for.

## 25. in `drain_midi_commands()`

An explicit Note Off (0x80) rather than the running-status "Note On, velocity 0"
shorthand: this app never uses running status, so the shorthand saves nothing,
and a device that treats a zero-velocity Note On as a real strike would be left
with a stuck note.

## 26. in `midi_thread()`

Poll: if session open, request LCD/LED updates as needed.

Every want-bit below is private to this thread (see their declarations). Other threads ASK
via midi_post_lcd_refresh(); nothing outside this loop sets or clears them, so a request
cannot be cleared before it was served and a reply cannot retire a request it did not
answer.
A sample transfer owns the link while it runs. An LCD frame is 2205 bytes — most of a
second — and interleaving one would stall the dump and blur the progress for no gain,
since the screen is not what the user is watching during a transfer.

## 27. in `midi_thread()`

While a dial drag is held, poll for an LCD delta on a steady
throttled cadence — independent of whether new encoder ticks
are currently being sent. This covers both a long continuous
drag (ticks never stop long enough to "go quiet") and a held
but paused drag (no ticks at all) alike. Never sends a new
encoder value itself — that's only ever driven by dial_nudge().

## 28. in `midi_thread()`

FULL frames while the wheel is moving, not deltas.

A delta describes the device's screen as it was when the device built it, and the
wheel keeps moving underneath the ~700 ms it takes to arrive — so what lands is a
picture of a moment that has passed, and the device then reports "nothing changed"
and never corrects it. That is how the display came to show a preset the hardware
never displayed at all (P099, owner-confirmed absent from the device).

It costs nothing to be right here: measured mid-burst, deltas took 516-1001 ms
against a flat 715 ms for a whole frame. The cheap option was not cheaper, only
wrong.

## 29. in `midi_thread()`

Re-base the delta stream once the display has gone quiet. Deltas are XORs against
the frame we hold, so a lost one would leave the display wrong indefinitely; this
bounds that to LCD_RESYNC_IDLE_MS without ever putting a ~705 ms full frame in front
of a user who is still pressing keys. Deliberately skipped while a dial drag is held
— that path is a continuous stream of deltas and is quiet only once the drag ends.
Keep asking even when we believe we are in sync: the device never reports front-panel
activity, so polling is the only way to see it. Cheap, because an unchanged screen
answers in 61 ms — see LCD_IDLE_PROBE_MS.
No separate idle test: gLcdLastProbeMs is restarted by every input and every reply, so
this interval IS the debounce. Borrowing the resync's 4 s here meant a front-panel
change made shortly after touching the app went unseen for up to four seconds — and
the probe is the ONLY channel by which such a change reaches us.

gLastUiEventMs is tested as well as gLcdLastProbeMs, and the two are not redundant.
The probe clock is restarted by the eMsgCmdUiActivity MESSAGE, which is only seen at
the next drain; gLastUiEventMs is stamped by the UI thread itself before it posts
anything. Without the second test this loop could reach the poll between an input and
its message and fire a probe on top of a press whose whole frame then has to wait out
the probe's round trip. Reading the stamp directly narrows that window from one probe
interval to the microseconds between the stamp and the post.

## 30. in `midi_thread()`

A full frame, not a delta, when the burst overlapped a transfer: that is the one
case where the delta stream cannot be trusted to have kept up, and the user has
just stopped, so it is the cheapest possible moment to spend ~715 ms putting the
picture beyond doubt.
A delta suffices here now that deltas are only applied for discrete events: the
trailing read just needs to catch the settled result. Drift is handled separately
and properly — applying any delta marks the base untrusted, and the resync then
fetches a whole frame once things have been quiet for LCD_RESYNC_IDLE_MS. That is
the "let the full refresh clean up" arrangement, with the delta never allowed to
be the thing that decides what is displayed for long.

## 31. in `midi_thread()`

Give the device a moment to finish redrawing before asking what it now shows.
Only applies to the discrete case: while streaming there is no settled state to
wait for. This used to be keyed on !gLcdWantFull, which meant the same thing back
when a discrete press asked for a delta; now that a press asks outright for the
frame, that test would have excluded the one case the setting exists to measure.

## 32. in `midi_thread()`

Drive this thread's CFRunLoop so the midi_notify_cb fires here.
Use a short interval when work is in progress, idle at ~30 Hz otherwise.
A transfer wants the fast tick throughout: every packet waits on this loop to come round
and acknowledge it, so an idle 33 ms tick would be added to the cost of all several
hundred of them.
