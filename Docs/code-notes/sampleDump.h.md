# sampleDump.h notes

The longer comments from `sampleDump.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `SDS_PACKET_DATA_BYTES`

MIDI Sample Dump Standard (MMA/JMSC, January 1986) — the published, non-proprietary way to move
sample data over MIDI. It is the only route into this sampler over the link we already have: SMDI
is faster but needs SCSI and the Emulator is a slave that cannot initiate, and the E-mu
editor/librarian SysEx was never published.

Wire format, for reference (cc = channel, ss ss = sample number LSB first, pp = packet number):
```
  DUMP REQUEST  F0 7E cc 03 ss ss F7
  DUMP HEADER   F0 7E cc 01 ss ss ee ff ff ff gg gg gg hh hh hh ii ii ii jj F7
  DATA PACKET   F0 7E cc 02 pp <120 data bytes> <checksum> F7      (127 bytes total)
  ACK  7F / NAK 7E / CANCEL 7D / WAIT 7C, each F0 7E cc <type> pp F7

```
Every multi-byte value is three 7-bit bytes, LSB first. Sample data is left-justified in each
7-bit byte, MSB first across the bytes of a word. The checksum is the running XOR of everything
after the F0 up to but excluding the checksum itself.
