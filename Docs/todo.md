EmUtility TODO

Things to do. ONE LINE PER ITEM - keep it that way.
Measurements, reasoning and completed-work narrative go in findings.md, NOT here.
Built-but-unchecked work goes in to-test.md.

Bugs

- Device > MIDI Ports... (SynthLib's midiPortDialog, 2026-09-11) replaced Scan Devices alone - built, NOT yet opened on screen or tried against the sampler; check a chosen input/output connects and a missing one says "Waiting for"
- No MIDI channel choice yet, deliberately deferred: the computer-keyboard notes are fixed to channel 1 (NOTE_ENTRY_MIDI_CHANNEL) and are silent if the sampler's basic channel differs - add Automatic/1-16 to the MIDI Ports dialogue if that bites
- do-release exists now, but no V* tag does - the first release needs an explicit version (./do-release 0.1.0-beta.1) before 'beta' can bump anything
