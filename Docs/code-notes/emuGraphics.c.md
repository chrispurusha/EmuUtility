# emuGraphics.c notes

The longer comments from `emuGraphics.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

NO GRAPHICS HEADER. The LCD texture was the last thing in this file that named OpenGL —
it is created and uploaded through utilsGraphics.h's render_backend_texture_* calls now.
Losing the include is the part a compiler enforces: this file can no longer reacquire a
dependency on a particular graphics API by accident.

## 2. `LCD_SOFTKEY_X`

── LCD soft keys ─────────────────────────────────────────────────────────────
The six boxes the sampler draws along the bottom of its own display are its soft keys, and F1..F6
are the buttons that press them. These are their positions WITHIN the 240x64 bitmap, in device
pixels, read straight off a live E5000 (2026-08-19) with the LCDDUMP backdoor command in
graphics.c, which prints the raw bitmap as an ASCII grid for exactly this purpose.

The device divides the full 240-pixel width into six exact 40-pixel cells at x = 0, 40, 80, 120,
160, 200, draws a 39-pixel rounded box in each (one pixel of gutter between neighbours), and the
row occupies y = 51..63 — flush with the bottom edge of the display. The click target is the
whole 40-pixel cell rather than the 39-pixel box, so the gutters aren't dead pixels between two
live keys.

EMU_SOFTKEY_COUNT itself lives in emuGraphics.h — graphics.c's backdoor walks the boxes too.

## 3. `LCD_EXIT_ZONE_H`

How far down the display the click-to-Exit zone reaches, in device pixels.

Deliberately NOT "everything above the soft keys". The sampler stacks things there: Utils raises a
second row of boxes, and a popup such as Sample Info puts an OK button well above the normal band.
Treating that whole area as Exit would fire Exit at a button the user was aiming for. The top half
is title and value text on every screen seen so far, and rows 32-50 are left as a dead margin
rather than assumed safe.

## 4. `LCD_SCALE_X`

The LCD is placed and sized FROM the F-key geometry rather than independently, so each soft-key
box lands directly above the button that presses it and stays there if either the button grid or
the measured box geometry is ever adjusted. Scaling the bitmap so one box spans exactly one
button pitch fixes the width; centring box 0 on F1 then fixes the left edge.

## 5. in `init_lcd_texture()`

NULL: the texels are left undefined and filled by the first update_lcd_texture(). They can
never be sampled undefined — render_lcd() updates before it draws on any frame where
gLcd.refresh differs from gLastRefresh, and those differ on the very first frame (0 against
the 0xFFFFFFFF gLastRefresh starts at). The texture is additionally not drawn at all until
a session is open.
Nearest filtering and edge clamping are what the backend gives every texture — which is
what this wanted anyway, since an LCD pixel is meant to look like a pixel.
Nearest: an LCD pixel is meant to look like a pixel, and this blits one texel per pixel.

## 6. in `update_lcd_texture()`

Snapshot the shared LCD buffer under gLcdMutex before expanding it. The
CoreMIDI callback thread mutates gLcd.pixels in place (full memcpy or an
in-place XOR delta), so reading it directly across the whole expansion
loop could tear a frame. Copy out fast, then expand + upload the local
copy with the lock released (no reason to hold it over the GL call).

## 7. `softkey_click_handler()`

Clicking a box on the display is exactly the same event as clicking the F-key beneath it, so it
goes through the same handler — including that handler's press/release semantics and its
"always ask for a full LCD dump afterwards" rule. userData is the index, not a pointer, because
the boxes are not objects: they are regions computed from the layout each frame.

## 8. in `render_lcd()`

The soft-key boxes are only live while a session is open — with no display content there is
nothing on them to press.
Registered BEFORE the soft keys so those sit on top of it — the body is the fallback for the
part of the display with nothing under it.

## 9. in `dial_nudge_by_angle()`

Rotate at the same angular rate as the mouse (1° of mouse rotation ==
1° of visual dial rotation), without pinning the indicator to the raw
mouse angle — matches how a real endless encoder is driven, rather than
a bounded pot that snaps to wherever you click.

## 10. `emu_button_press()`

Buttons act on both press and release (unlike the dial, which only arms on
press) — mirrors the "btn->pressed = pressed" line previously in
handle_mouse_button()'s inline hit-test (removed from mouseHandle.c).

Split out of the click handler so the soft-key boxes drawn on the LCD, and the backdoor's BUTTON
command, raise exactly the same event a click on the button itself does — including lighting the
on-screen button, so pressing a box on the display visibly presses the F-key below it.

## 11. in `emu_button_press()`

Stamped BEFORE the button event is posted, not after.

Only the PRESS counts as activity — the release is the tail of the same gesture and changes
nothing further, but stamping it restarts the settle timer AND makes every reply in flight
look superseded.

The ORDER matters because this is also what restarts the idle probe clock (see
eMsgCmdUiActivity). The MIDI thread can drain the queue and reach its polling decisions
between any two posts, so posting the button event first left a window in which an idle probe
went out on top of a press — and the whole frame the press actually wanted then had to queue
behind that probe's reply. Restarting the clock first closes the window: by the time the
button event reaches the wire, the poll has already been told to stand down.

## 12. in `emu_button_press()`

A WHOLE FRAME, not a delta and not a probe.

Polling exists to answer "has the screen moved?". We just pressed a button, so it has —
there is nothing to find out, and the only open question is what it now shows, which only a
whole frame is allowed to answer (see LCD_USE_DELTAS). Asking for a delta here meant that
after LCD_RESYNC_IDLE_MS of quiet — an ordinary isolated press — the request went out as a
probe whose payload is deliberately discarded, with the frame we actually wanted following
behind it: two round trips on a 31250-baud link for one press, the second unable to start
until the first had finished.
