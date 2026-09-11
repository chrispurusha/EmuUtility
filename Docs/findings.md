EmuUtility findings

Completed work, hardware measurements, and the traps that cost real time.
One entry per finding, newest first.

2026-09-02 - Nothing reached the GUI: the app was talking to the wrong MIDI destination
  Symptom: comms visible to and from the E5000, but no LCD, no LEDs and no button ever reaching
  the panel. STATE said "session=closed connected=yes", which is the whole bug in six words.
  Cause: a scan broadcasts the identity request to EVERY destination, so the reply proves only
  which SOURCE the device speaks on. handle_identity_reply() then INFERRED the destination from
  that source's entity. On this rig the E5000's Out is on interface port "Port 2" while its In is
  on "MIDI 2" - different entities entirely - so every PEPTALK message after the identity reply
  went nowhere. Silent: the device answers the broadcast identity request and then never answers
  anything again.
  Fix: the entity's destination is now a guess that must be proved. It is tried first; if no
  session status answers it, the remaining destinations are tried in turn until one does.
  The session status ECHOES THE SEQUENCE ID OF THE SESSION OPEN IT ANSWERS, IN BYTE 3 - not byte 4
  where every other PEPTALK reply carries it. Measured twice on the E5000 against opens sent with
  deliberately different ids (0x15 and 0x5C). That is what makes the probe exact rather than a
  race: probes may overlap on the wire and the reply still names the destination that earned it,
  so the interval is a pacing figure and not a deadline.
  Also fixed alongside: gSessionOpen was never cleared anywhere. After a rescan - a hub replug
  fires one on its own - the app went on believing a session was open on a destination it no
  longer had, and the panel went on saying "open".
  Verified on the real E5000: session opens on "MIDI 2", full frame in 716ms, deltas in 61ms,
  and BUTTON Master/Preset move the sampler's own display.

2026-09-02  RACE CONDITION REVIEW - ONE FIX, AND A DESIGN THAT HOLDS UP
------------------------------------------------------------
Same method as the GenBridge, G2-Edit and SynthEdit reviews. Three threads: the GLFW render thread,
the MIDI thread (pthread, CFRunLoop-driven), and the CoreMIDI read callback thread, which dispatches
inline - so an incoming PEPTALK message mutates shared state on the callback thread while the render
thread reads it.

FIXED
  - gLcd.refresh was a plain uint32_t read WITHOUT gLcdMutex by render_lcd() (emuGraphics.c:223) and
    by the backdoor's STATE (graphics.c:661), while peptalk_handle_message() increments it under the
    lock on the CoreMIDI thread. Now _Atomic. Reading it bare is the right call - taking the mutex
    just to test a dirty flag would put the render thread behind the CoreMIDI thread every frame -
    but a plain int touched by two threads is a data race whatever its practical consequence.

CHECKED, AND SOUND - recorded so nobody "fixes" them:
  - THE LCD ITSELF IS PROPERLY GUARDED. update_lcd_texture() copies gLcd.pixels out under the mutex
    and does the expansion and the GL upload with it released; peptalk_handle_message() holds it
    across every write. peptalk_apply_lcd_delta() documents "Caller holds gLcdMutex" and its callers
    do - which also serialises its file-static scratch buffer, so the one thing that looks alarming
    in it is safe for the same reason.
  - gDevice.model[32] and gDevice.firmware[8] are char buffers shared across threads and looked like
    the raced buffers found in GenBridge and SynthEdit. They are NEVER WRITTEN - dead fields. Worth
    deleting some day, but not a race.
  - The destination probe added earlier today (gProbeSeqDest, gDestProbeActive, gDestProbeNext) is
    MIDI-thread-private throughout, and gSessionOpen is atomic.

LEFT, AND JUDGED NOT WORTH RESTRUCTURING: gDevice's scalars (connected, id, family, member) are
written as a group by handle_identity_reply() on the MIDI thread and read by the render thread for
the status line. With no barrier between them the reader can in principle see connected == true
before id lands, and draw a connected device with a stale id for one frame. Cosmetic, and the
alternative - publishing the four as one atomic snapshot - is more machinery than the symptom earns.
Noted rather than done.

WHY THIS APP CAME OUT WELL: the thread ownership here was worked out deliberately (see the
LCD-thread-ownership and "minimise shared thread flags" notes above), and it shows. The same was true
of SynthEdit. GenBridge, which grew its worker around a processor that began single-threaded, needed
seven fixes.

2026-09-09  METAL ONLY ON macOS - THE WHOLE PROJECT, NOT JUST THE PLUG-IN

SynthLib's renderBackendSelect.h now defines SYNTHLIB_NO_GL_BACKEND on every Apple target, so the
OpenGL backend is left out of the build entirely: renderBackendGL.c compiles to nothing (the guard is
inside the file, because SynthLib/src is a synchronized folder in the Xcode projects and a build
script cannot exclude it), renderBackend.c does not declare its table, and gfx_backend_available()
answers false for it. The default backend follows the platform: Metal on Apple, OpenGL elsewhere.

THE MENU ITEM IS GONE with it. "Use Metal Renderer (on restart)" / "Use OpenGL Renderer (on restart)"
offered a switch to a backend that is no longer linked. Only the greyed "Renderer: <name>" readout
stays, so there is still a way to see what drew the window.

THE WAY BACK IS A REBUILD, not a preference, and that is a deliberate step down in convenience:
prefs.txt's renderBackend key cannot select a backend that is not in the binary. SYNTHLIB_ALLOW_GL_ON_APPLE
restores it. That switch also answers renderBackendGL.c's own argument for staying alive on macOS -
running the two renderers against each other on one machine is the cheap way to prove the Metal port
moved no pixel, and it is still possible.

NOTHING IN renderBackendGL.c WAS DELETED and nothing should be. It is the whole renderer for the
Windows and Linux versions to come, it is OpenGL 1.1 with no platform in it, and it is what #else
selects everywhere that is not Apple.

AND THE MENU THAT HELD IT WENT TOO (2026-09-09). The Experimental menu existed for one thing - the
OpenGL/Metal choice - so once macOS became Metal only there was a whole top-level menu carrying a
single greyed "Renderer: <name>" line. Both are gone; the About box has printed the renderer all
along (synthlib_about_text()), which is where that information belongs and where it now lives alone.
