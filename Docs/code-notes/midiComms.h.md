# midiComms.h notes

The longer comments from `midiComms.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `midi_request_reconnect()`

── Commands (safe to call from ANY thread) ──────────────────────────────────
Each posts to gToMidiThread and wakes the MIDI thread's CFRunLoop, so the work runs on the one
thread that owns the connection state. Named to match SynthEdit's midi_request_reconnect(), which
solves the same problem there.

## 2. `midi_lcd_reply_suspect()`

Should this reply be refused rather than applied? True only when the timeout has left more than
one request outstanding AND this reply's sequence id is not the one we are waiting for — a
mismatch alone means nothing, because the device also speaks unprompted. MIDI thread owns the
state; the callback thread only reads it.

## 3. `midi_lcd_is_quiet()`

Has the display stopped moving? True when nothing is on the wire, nothing is wanted, and the last
reply came back reporting no change — i.e. the device itself has said "nothing has changed since
you last asked". This is the only sound moment to compare our frame against a fetched one: before
it, a difference may simply be the device still working through its own backlog rather than
anything wrong on our side.
