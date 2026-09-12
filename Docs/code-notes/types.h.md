# types.h notes

The longer comments from `types.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

Incremented on each update; the render thread compares it against its own last-seen value to
decide whether to re-upload the texture. ATOMIC because that comparison is the ONE read of
this struct made without gLcdMutex - render_lcd() and the backdoor's STATE both read it bare,
deliberately, since taking the mutex just to test a dirty flag would put the render thread
behind the CoreMIDI thread on every frame. The pixels still need the mutex; this counter does
not, but it does need to not be a plain int that two threads touch.
