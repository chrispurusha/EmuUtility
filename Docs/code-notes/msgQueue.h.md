# msgQueue.h notes

The longer comments from `msgQueue.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `eMsgCmd`

gToMidiThread is the MIDI thread's command queue. Every other thread (the UI/render thread, the
CoreMIDI read callback thread, the NSWorkspace sleep/wake block) posts here instead of touching
the connection state or sending to the device itself, so gDevice / gMidiSource / gMidiDest and the
CoreMIDI port objects have exactly ONE owner. That ownership rule is the whole point: before this,
midi_scan_devices() ran on the UI thread from the Scan Devices menu item and from the wake
notification, concurrently rewriting the very state the MIDI thread's own loop was using.
SynthEdit hit and fixed the same class of bug (see midi_request_reconnect() there); this is the
same fix expressed with the shared SynthLib queue instead of a bespoke flag.

There is deliberately NO reverse (MIDI -> UI) queue here yet. Everything this app reports upward
today is a *coalescing* dirty-bit (gNeedLcdFull/gNeedLcdDelta, gLcd.refresh, gLeds), and those must
stay flags — N rapid device updates have to collapse into one redraw, where a queue would enqueue
N. See reverse-queue-design.md ("What belongs on the queue — and what doesn't"). Add gToGuiThread
when there is a first genuine discrete result to carry.

## 2. `tSessionStatusData`

Posted by the CoreMIDI read callback when the device answers a session open. Carries the sequence
id the reply echoed, which is what identifies WHICH session open it is answering — and therefore
which destination the device is listening on. Only the MIDI thread may act on that, because it
owns gMidiDest and the destination probe.

## 3. `tSdsStartData`

Posted by the CoreMIDI read callback once it has done the part it owns — validating the reply and
writing the pixels. Everything the reply implies for the REQUEST state (what is still in flight,
whether another transfer is owed) is decided by the MIDI thread from this message, because that
thread owns it. Mirrors how tIdentityReplyData splits the same way.
Ownership of dump.samples MOVES to the MIDI thread with this message: it is the thread that will
spend the next several minutes reading it, and the only one that knows when it is finished.
