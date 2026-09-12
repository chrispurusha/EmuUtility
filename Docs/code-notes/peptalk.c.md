# peptalk.c notes

The longer comments from `peptalk.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `gLastRequestSeq`

The sequence id the last LCD request went out with. Written and read on the MIDI thread only —
it is handed straight to that thread's outstanding-request state, which owns the question of what
is in flight. The device echoes the id back, so a reply carrying any other id belongs to a request
we already gave up on, and applying its delta would corrupt the frame exactly as a spliced payload
does. Matching on the protocol's own identifier rather than on arrival order means we are provably
answering the right question.

## 2. `apply_lcd_delta_to()`

Applies into `frame`, which is NOT the live buffer — see the call sites. A delta that turns out to
overrun is only recognisable part-way through XOR-ing it, so applying straight to the display would
paint a half-wrong picture and correct it a round trip later: visible, brief corruption. Working on
a scratch copy means a bad delta is simply never shown.

## 3. in `apply_lcd_delta_to()`

A delta that runs off the end of the frame was computed against a different
base than the one we hold — the clamp keeps it from corrupting memory, but the
picture is now definitely wrong and only a full frame can fix it. Reported
rather than silently swallowed: this is the one moment we can KNOW we are out
of step, instead of waiting for the idle resync to find out.

## 4. `lcd_reply_is_current()`

A reply has landed. Returns false when it belongs to a request we already gave up on — replies come
back in the order the requests went out, so if anything is STILL outstanding after accounting for
this one, this is the older reply and applying it would corrupt the frame.
Does this reply answer the request that is actually outstanding? Asked of the MIDI thread, which
owns that state; this thread only reads it. A reply carrying a different sequence id answers a
request already abandoned (the timeout sends a second one without recalling the first), and its
delta would XOR against a frame that has since moved on. Nothing here writes request state — the
outcome goes back as a message so the owning thread decides what it means.

## 5. in `peptalk_handle_message()`

THE SEQUENCE ID IS IN BYTE 3 HERE, not byte 4 where every other reply carries it —
measured on an E5000, twice, against session opens sent with deliberately different
ids. It is what says which of the session opens we sent this one is answering, and so
which destination the sampler is actually listening on.

Nothing is decided here any more. Opening the session settles the connection, and the
connection belongs to the MIDI thread — see msgQueue.h. This thread only reports what
arrived.

## 6. in `peptalk_handle_message()`

Longer than a whole frame, so it is neither: a full frame unpacks to EXACTLY
LCD_BYTES, and the device never sends a delta bigger than the frame it would
replace. The likeliest cause is a payload that arrived spliced — the SysEx
reassembly buffer in midiComms.c is shared by every connected MIDI source, so
traffic from another device on the rig lands in the middle of a transfer that
takes ~705 ms to arrive. Refetch rather than memcpy it over the pixels: this
branch used to be folded into the full-frame case by a `>=`, which is how
garbage reached the screen.

## 7. in `peptalk_handle_message()`

Full frame — replace pixels entirely. Hold gLcdMutex so the
UI thread can't snapshot a half-written buffer (torn frame).
Divergence check, and it costs nothing: we are about to overwrite the frame the
delta stream built, and here is the device's own copy of what that frame SHOULD
be. If they differ, the deltas got out of step — the exact failure that shows as
on-screen corruption sitting there until a full frame washes it away.

Only meaningful once the display has settled: gLcdBaseTrusted false means deltas
have been applied since the last full frame, and the idle resync only fires after
LCD_RESYNC_IDLE_MS of quiet, by which time the trailing delta has long landed. A
difference at THAT point is a real defect, not a legitimate pending change.
gLcdLastDeltaMs is still zero if no delta has ever been applied, which is the
state at launch — where the buffer is blank and "differs" from the first real
frame for entirely innocent reasons.

## 8. in `peptalk_handle_message()`

A PROBE never commits. Tried committing, 2026-08-20, and the "old, new, old, new"
bounce came straight back with it — on a build where probes were the only deltas
being applied at all, which is what isolates this as the cause.

Discarding is what makes it safe: a probe that reports change ALWAYS forces a whole
frame, and that frame re-bases us before any further delta is ever applied. So the
device's reference running ahead of ours never gets the chance to matter. Applying
the payload instead paints whatever the delta produces — and when its base is not
the frame we hold, that is a visibly wrong intermediate.

## 9. in `peptalk_handle_message()`

An empty delta changed nothing, so the base is as good as it was — that is the
common idle answer and it must not force a re-base.
A probe that saw movement leaves the device's reference ahead of ours precisely
because we threw its payload away — so the base is untrusted either way, and the
whole frame that follows is what puts it right.
