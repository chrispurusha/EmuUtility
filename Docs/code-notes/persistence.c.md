# persistence.c notes

The longer comments from `persistence.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

Window/dial-mode settings persistence — goes through SynthLib's prefs.h (a plain "key=value"
text file under a per-OS standard config directory) instead of NSUserDefaults, so none of this
needs Objective-C/Cocoa any more. Same shape as G2-Edit's persistence.c, minus zoom/file-browser-
directory, which this app doesn't have.

## 2. in `load_saved_settings()`

Whether the computer keyboard plays notes. Remembered rather than defaulted, because which
setting is "right" depends on what the user is doing: playing the sampler wants it on, editing
a name on the device itself wants it off, and having to set it again every launch is the kind
of small friction that makes a preference worth storing at all. Defaults ON for a fresh
install, so the keyboard works without anyone having to find the menu first.
