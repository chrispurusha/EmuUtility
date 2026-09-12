# graphics.c notes

The longer comments from `graphics.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `wake_glfw()`

The shims that used to sit here — fetch the cursor, scale it, decode the button, call the handler,
request a redraw — are SynthLib's now, written once for all three editors. See
tSynthLibInputHandlers in synthlibWindow.h. They also do something this app's own versions never
did: update the modifier state from the GLFW mods before the handler runs.

## 2. in `init_graphics()`

THE WINDOW, ITS SCALE AND ITS INPUT WIRING ARE SYNTHLIB'S — see synthlibWindow.h. The six
callbacks that only ever called back into SynthLib (error, framebuffer size, content scale,
window size, window position, window close) went with it; the ones below are this app's own.

NOTE what is no longer possible here. This app used to register a focus callback that its own
close handler never unregistered — the handler was copied from the other two before the focus
callback existed, and nothing kept the two lists in step. synthlib_window_close() unregisters
exactly what synthlib_window_create() registered, from the same table, so that particular
drift cannot happen again.

A character callback IS registered now. The app itself still takes no text — handle_character()
does nothing but hand the codepoint to the popup coordinator, which is where the only text
field this app can put on screen lives (the file browser's filename box).
The coordinator needs the menu bar before the first frame — see synthlibPopups.h.

## 3. in `render_frame()`

The BAR stays here — it is chrome, and anything that floats does so above it. Everything that
pops UP goes through the coordinator, ordered by layer rather than by the order of these calls.
This app only has the context menu today; it gets the browsers and the alert dialog for free if
it ever opens one. See synthlibPopups.h.

## 4. `backdoor_enabled()`

── Backdoor command channel (testing only) ───────────────────────────────────
A tiny file-driven command channel so a scripted test can drive this app and read back what it
drew — press a front-panel button, nudge the dial, capture the window, or dump the raw LCD
bitmap — without a real mouse click. Ported from G2-Edit's and SynthEdit's proven mechanism.

GATED behind the EMU_UTILITY_BACKDOOR environment variable: unset (the owner's normal
double-click launch) => completely inert, and the idle loop keeps its normal cadence. Set (a test
launch from a shell) => a command file is honoured each tick. The gate matters more here than in
SynthEdit, because BUTTON and DIAL send real PEPTALK events to a real connected sampler.

This app is sandboxed (com.apple.security.app-sandbox), so a hardcoded "/tmp/..." path would be
silently unreachable — fopen() on one just returns NULL, no error, no crash. The paths are built
on emu_temp_dir() (misc.h), this app's own container tmp folder, instead.

Command file (<container tmp>/emuutil_cmd.txt): one command per file, first line only,
"<COMMAND> <arg>". The result ("OK\n" / "ERROR: ...\n", or the command's own text) is written to
<container tmp>/emuutil_result.txt and the command file is deleted, so a caller polls for the
command file's disappearance to know it is done.
```
  MENU <bar>[/<item>[/<sub>]] — run a menu item by label (matched anywhere in it,
                      case-insensitively, levels separated by '/'). OMIT the last level and that
                      menu is LISTED instead of clicked, which is how a test finds what is there.
                      Ported from G2-Edit 2026-08-29 so the renderer toggle could be exercised.
  SCREENSHOT <path>  — synchronous render_frame() then glReadPixels + PNG of the whole window
  SCREENGRAB <path>  — capture what is CURRENTLY PAINTED, with no render first. SCREENSHOT renders
                       before capturing, which repairs a stale window and so cannot be used to
                       detect one. Comparing this against LCDDUMP separates "our pixel buffer is
                       wrong" from "the buffer is right but the window was never repainted".
  LCDDUMP [path]     — the raw 240x64 LCD bitmap as an ASCII grid ('#' lit, '.' unlit) in the
                       result file, and, if a path is given, also as a 1:1 240x64 PNG. This is
                       what lets the soft-key box geometry be measured in DEVICE pixels rather
                       than guessed off a scaled screenshot.
  BUTTON <label|code> [down|up] — a front-panel button, by its on-screen label ("F1", "Prs Edit",
                       case-insensitive) or by its raw PEPTALK key code. With no third word it
                       does a press AND a release, which is what a real click does.
  DIAL <delta>       — turn the data wheel by that many detents (+ve clockwise)
  KEY <char> [down|up] — deliver a key to handle_key() as GLFW would, so the computer-keyboard
                       note entry and the keyboard-to-panel shortcuts can be exercised headlessly.
                       A single character ('a', 'z') or a raw GLFW key code. With no third word it
                       does a press AND a release.
  BURST <label> <n> <gapMs> — press AND release a button n times with a real gap between each
                       edge, on the render thread, exactly as rapid mouse clicking does. Exists
                       because BUTTON fires press and release in one dispatch, which is the one
                       thing a real click never does — and the lost-update bugs only show up when
                       the edges are separated in time.
  SPIN <steps> <gapMs> — nudge the data wheel `steps` times with a real gap between each, on the
                       render thread, exactly as a mouse drag's cursor callbacks do. Lets the
                       encoder coalescing be measured: N nudges in, fewer PEPTALK events out, and
                       the deltas summing to N.
  SAMPLEINFO <path>  — parse a .wav and report what would be sent, or why it cannot be. Runs every
                       check without touching the sampler, so the limits can be exercised safely.
  SENDSAMPLE <path> <n> — load a .wav and send it to the device as sample number n over SDS. Long:
                       minutes for anything but a short one. Poll SDSPROGRESS to watch it.
  SDSDRYRUN <path> <out> — build the whole SDS stream for a .wav and write it to `out` WITHOUT
                       sending anything. Lets the wire format be checked byte for byte before a
                       transfer overwrites a sample on real hardware.
  GETSAMPLE <n> <out.wav> — ask the device to SEND US sample n, written out as a .wav. This is the
                       non-destructive direction: nothing on the sampler changes. An empty slot
                       simply never answers, which the standard defines as the response to a
                       request for a sample that is not there.
  SDSPROGRESS        — packets sent / total, and whether the receiver is handshaking
  SDSCANCEL          — abandon a transfer in progress
  PEPTALK <type> [hex bytes...] — send an arbitrary PEPTALK message, for protocol exploration.
                       e.g. "PEPTALK 41 41" tries message type 0x41 with one payload byte 0x41.
  REFRESH            — ask the device for a full LCD dump on the next poll
  STATE              — session/device state plus the on-screen geometry: the LCD rectangle, the
                       six soft-key rectangles, and every button's rectangle
```

## 5. `backdoor_menu()`

MENU <bar>[/<item>[/<subitem>]] — runs a menu item by label without going near the mouse. Labels
are matched case-insensitively anywhere in the label and separated by '/', so
'MENU Exp/Use Metal' reaches an item by a fragment of its text. OMIT THE LAST LEVEL and the
deepest menu reached is LISTED rather than clicked, which is how a test discovers what is there.
Ported from G2-Edit's backdoor, which has had this since the menu bar moved in-window; driving
menus by screen coordinate meant re-deriving them every time a window moved.

## 6. in `backdoor_dump_state()`

The pid, because the backdoor command file lives in the app's CONTAINER and is therefore SHARED
by every running instance. Two of them (say one from Xcode and one from a script) both poll it,
so a command can be answered by whichever gets there first — and a test that dumps the frame
from one instance and refreshes the other compares two unrelated things. That silently wrecked
a whole run of measurements on 2026-08-20; reporting the pid makes the cross-talk detectable.

## 7. in `do_graphics_loop()`

Polled every tick (not just on cursor move) so a hover-dwell timer elapses even while
the mouse sits still, and so switching from one open top-level menu-bar label to
another happens on hover, not just a second click — matches G2-Edit/graphics.cpp and
SynthEdit/graphics.cpp. Needs glfwWaitEventsTimeout() below rather than
glfwWaitEvents()'s indefinite block for the same reason.
Every registered popup's hover/dwell update in one call, so the host cannot forget one.

## 8. in `do_graphics_loop()`

Cheap no-op access() check per iteration, and skipped entirely (returns immediately)
unless EMU_UTILITY_BACKDOOR is set — see the backdoor block's own header comment.
A gesture whose release went missing never survives a frame — see its own comment for why
that matters far more here than a stuck cursor would.
