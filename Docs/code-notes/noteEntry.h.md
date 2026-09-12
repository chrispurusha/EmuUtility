# noteEntry.h notes

The longer comments from `noteEntry.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `handle_note_entry_key()`

The computer keyboard as a music keyboard: the home row plays the white notes and the row above
holds the blacks, the layout every tracker and DAW uses. Ported from G2-Edit's
handle_note_entry_key() (src/virtualKeyboard.c there), with the same key map and the same Z/X
octave shift, so muscle memory carries between the two applications.

Routed from handle_key() BEFORE its front-panel button mapping. Returns true when it consumed the
key, so a key that plays a note can never also be a panel shortcut.

## 2. `note_entry_set_enabled()`

Whether the computer keyboard plays notes at all.

Turned off while editing text on the device — a name, say — where the letter keys are wanted for
the sampler's own purposes and playing a note on every keystroke is worse than useless. Switching
off releases anything currently sounding, so a key held across the switch cannot stick.

## 3. `note_entry_set_enabled()`

Whether the computer keyboard plays notes at all.

Turned off while editing text on the device — a name, say — where the letter keys are wanted for
the sampler's own purposes and playing a note on every keystroke is worse than useless. Switching
off releases anything currently sounding, so a key held at the moment of the switch cannot stick.
