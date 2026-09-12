# noteEntry.c notes

The longer comments from `noteEntry.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `gNoteKeys`

The key map, in semitone order from the leftmost key. The home row is the white notes and the row
above holds the blacks — a = C, w = C#, s = D, e = D#, d = E, f = F and so on, with k, o, l and p
carrying on into the octave above where the home row runs out. Identical to G2-Edit's
note_offset_for_key() (src/virtualKeyboard.c), deliberately: the same fingers should play the
same notes in both applications.

A table rather than a switch because the release path needs the reverse lookup too — which slot a
key occupies — so that a key can be remembered by slot and released with the note it actually
started, not the note its position would produce now (see gKeyNote below).
