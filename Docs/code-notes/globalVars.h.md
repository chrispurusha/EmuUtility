# globalVars.h notes

The longer comments from `globalVars.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `gDevice`

── MIDI / device ────────────────────────────────────────────────────────────
All six of these are owned by the MIDI thread (midiComms.c) — only it writes them, and only from
its own loop or from a command drained off gToMidiThread. Other threads read gDevice for display
and post commands; they must not call into the scan/connect path directly. See msgQueue.h.

## 2. `gLcdBaseTrusted`

The LCD REQUEST state that used to live here — gNeedLcdFull, gNeedLcdDelta, gLcdPending,
gNeedLeds, gLcdInFlight, gLcdReqMs, gLcdSettled — is gone. It was written by both the MIDI thread
and the CoreMIDI read callback, and although each was _Atomic the read-modify-write SEQUENCES were
not: a want-bit set between one thread's test and the other's clear was silently dropped, and a
pending flag cleared while a request was being issued let two transfers overlap, which corrupts
the frame. It is now private to midiComms.c and everything else posts (midi_post_lcd_refresh /
midi_post_led_refresh / midi_post_lcd_reply). See that file for the ownership rule.

## 3. `gDialDragActive`

Throttled LCD refresh while a dial drag is held: gDialDragActive is true
for the whole press-to-release span, and the MIDI poll thread requests one
delta dump every DIAL_LCD_POLL_INTERVAL_MS while it's set — regardless of
whether new encoder ticks are currently being sent — so the screen keeps
refreshing during a long continuous drag and during a held-but-paused
drag alike, without requesting faster than the throttle interval. Never
causes a new value to be sent — that stays solely driven by dial_nudge().

## 4. `gLastUiEventMs`

When the user last did anything that could change the display. Written only by UI threads and read
only by the MIDI thread — ONE writer, which is why this one may stay a plain atomic. The matching
"has the trailing delta been taken yet" bit lives inside midiComms.c, because both threads would
otherwise write it. See LCD_SETTLE_MS in defs.h.
