# misc.h notes

The longer comments from `misc.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `register_sleep_wake_notifications()`

register_sleep_wake_notifications() and setup_main_menu() are implemented in misc.mm — the only
two things left in this codebase that genuinely need Objective-C/Cocoa. Everything else
declared below is plain C: the Device/Controls menus live in appMenuBar.c, settings persistence
(backed by SynthLib's cross-platform prefs.h rather than NSUserDefaults) lives in persistence.c.

## 2. `emu_temp_dir()`

This app's own container tmp directory, with a trailing '/'. The App Sandbox
(com.apple.security.app-sandbox) makes a hardcoded "/tmp/..." path silently unreachable —
fopen() just returns NULL, no error — so the backdoor command channel in graphics.c builds its
paths on top of this instead. Same helper, same reason, as SynthEdit's synth_temp_dir().
