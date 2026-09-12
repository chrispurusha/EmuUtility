# misc.mm notes

The longer comments from `misc.mm`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

Everything that doesn't strictly need Objective-C/Cocoa has moved out of this file — the
Device/Controls menus live in appMenuBar.c, and settings persistence lives in persistence.c
(backed by SynthLib's cross-platform prefs.h rather than NSUserDefaults). What's left here is
genuinely Mac-only: the minimal native app menu Cocoa itself requires, and sleep/wake
notifications (NSWorkspace has no cross-platform equivalent in this codebase).

## 2. `setup_main_menu()`

Sets up the minimal native Cocoa app menu (Quit/About/Hide/Services — GLFW's Cocoa backend
already populates these at index 0), then restores window/dial-mode state from the prefs file
(see load_saved_settings() in persistence.c; settings used to live in NSUserDefaults, now a
plain text file via SynthLib's prefs.h so the same mechanism can work on Windows/Linux too).
Device/Controls menus used to be constructed here too; they're now the in-window bar built in
src/appMenuBar.c on top of SynthLib's menuBar engine.
