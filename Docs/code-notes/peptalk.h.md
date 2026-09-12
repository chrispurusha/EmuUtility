# peptalk.h notes

The longer comments from `peptalk.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `peptalk_apply_lcd_delta()`

Apply a delta (RLE XOR) update to gLcd.pixels.
Applies an RLE-XOR delta to gLcd.pixels, ALL OR NOTHING. Returns false if the delta ran off the end
of the frame — meaning it was computed against a base we no longer hold — and in that case the
display is left exactly as it was, so a bad delta is never briefly painted and then corrected. The
caller must then fetch a full frame; nothing short of one can put the picture right. Caller holds
gLcdMutex.
`commit` false makes this a pure test: it works out whether the delta WOULD change anything and
reports that in changedOut, without touching the display. That is how a delta is used as a change
detector without ever trusting its content — see LCD_PROBE_WHEN_IDLE.

## 2. `peptalk_send_raw()`

Send an arbitrary PEPTALK message. Exists ONLY for protocol exploration from the backdoor — the
message types this app understands are a small subset of what the device implements, and the gaps
in the numbering (0x41, 0x42, 0x44 sit between BUTTON 0x40 and ROTARY 0x43) are the obvious place
to look for anything else. Nothing in the app proper should call this.
