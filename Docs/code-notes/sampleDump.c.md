# sampleDump.c notes

The longer comments from `sampleDump.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `tpdf_dither()`

Triangular (TPDF) dither, +/-1 LSB, from a small deterministic generator.

Deterministic on purpose: the same file converts to the same bytes every time, so a transfer can
be diffed against a previous one. A hardware RNG would make every send of the same sample differ.

## 2. `sample_to_16()`

One sample of any supported width, normalised to signed 16-bit.

The guiding rule is that anything ALREADY in the target format passes through untouched — a 16-bit
file is sent bit for bit, with no rounding, no dither and no resampling to go wrong. Conversion
happens only where the source genuinely is not what the wire carries.

## 3. in `sample_to_16()`

Reducing depth, so round to nearest with dither rather than truncating. Truncation
biases every sample toward zero and turns quantisation error into harmonic
distortion that correlates with the signal; dithered rounding turns it into a steady
low-level hiss instead, which is the standard trade and much easier on the ear.
