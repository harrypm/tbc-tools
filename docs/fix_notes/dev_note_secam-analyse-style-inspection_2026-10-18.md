# Dev note — analyse-style SECAM decoder inspection (2026-10-18)

## The method (how to eyeball SECAM decoder changes correctly)

The only visually meaningful way to compare SECAM decoder changes is the
**analyse-style view**: real luma from the luma TBC + chroma decoded from the
split `_chroma.tbc`, rendered with tbc-analyse's OutputWriter matrix —
exactly what tbc-analyse produces when it opens the luma+chroma pair
(BOTH_SOURCES) and exports.

CLI single-input renders do **not** show this:

- Decoding the `_chroma.tbc` alone renders the chroma TBC's own signal as
  luma (the decoder's composite passthrough), i.e. the FM block riding
  mid-scale as a noisy grey — the colours ride on garbage luma and every
  such PNG looks broken even when the U/V data is correct.
- Decoding a **combined** luma+chroma `.tbc` is a per-row coin toss in
  every decoder tried (old and new): in-band luma (3.9-4.756 MHz) is
  spectrally identical to chroma, the per-row demod swings +-25k (measured
  on the etienne yellow bar: rows read +14k / -16k / -20k / -25k / +16k),
  and per-bar means come out flat. This is the documented 2026-09-13
  limitation; the split `_chroma.tbc` is the only clean input.
- Also note: the etienne capture was re-decoded after 2026-09-13, and the
  re-decode suppresses the chroma block in the combined `.tbc` ~52x
  (yellow carrier mag 3,151 vs 168,032 in the pre-re-decode file, 343,948
  in the `_chroma.tbc`). The pre-re-decode combined file survives only as
  `Desktop/SECAM/snippets/secam_etienne_colorbars_50f.tbc`.

## Recipe (reproduces an analyse export without the GUI)

1. Decode the chroma: `tbc-chroma-decoder --input-metadata X_chroma.tbc.db
   --decoder secam -p yuv X_chroma.tbc out.yuv` (YUV444P16, active area).
2. Take Y from the luma `.tbc`: output row y <-> field (y%2) row (y/2),
   columns `[activeVideoStart, activeVideoEnd)`.
3. Render with the OutputWriter matrix:
   `R = Y + 1.139883*(V-32768)`, `G = Y - 0.394642*(U-32768) - 0.580622*(V-32768)`,
   `B = Y + 2.032062*(U-32768)` (U/V deltas are on the composite yRange
   scale; see secamdecoder.cpp's DR_TO_V/DB_TO_U notes).
4. Render old vs new side by side (swap the decoder via the restore-point
   zip / `git checkout 4e91a5c -- src/tbc-chroma-decoder/secamdecoder.*`,
   rebuild, decode, restore, rebuild).

Reference outputs from 2026-10-18 (in /tmp/secam-ab/): analyse-style-{old,new}
-{etienne,tape275}.png, built from test-data/secam fixtures + the pre-port
decoder at 4e91a5c vs the ported decoder.

The ground-truth check remains tbc-analyse itself: load the luma+`_chroma`
pair in `build/bin/tbc-analyse` and export as usual — the built binary
carries whichever decoder is in the working tree.

## Vectorscope inspection (offset correction)

The U/V scatter (vectorscope) shows the carrier-offset correction directly:
an uncorrected FM block offset (MESECAM LO arithmetic lands ~+105 kHz on
tape275) leaks into D'B/D'R as a constant colour-difference cast — every
cluster displaced by the same vector — while a corrected decode lands the
clusters on the 100%-saturation graticule boxes:
white/black at the origin, yellow (-0.436, +0.100), cyan (+0.147, -0.615),
green (-0.289, -0.515), magenta (+0.289, +0.515), red (-0.147, +0.615),
blue (+0.436, -0.100) — normalized U = (B-Y)/2.032062, V = (R-Y)/1.139883,
BT.601 luma. See the vectorscope PNGs produced alongside this note
(vectorscope-{old,new}-{etienne,tape275}.png, /tmp/secam-ab/).

Measured (2026-10-18, etienne `_chroma.tbc`, per-bar centroids vs the
graticule):

- OLD: neutral bars off-centre (white at (-0.030, -0.021)), colour bars
  displaced 0.05-0.28 from their boxes, mean colour-bar displacement (the
  constant offset cast) dU -0.024 dV -0.008.
- NEW: neutral bars on the origin (white (-0.004, -0.009), black
  (+0.002, +0.008)), mean colour-bar displacement dU +0.000 dV +0.001 --
  the constant cast is gone. What remains per bar is RADIAL shortness
  (vectors landing ~60-80% of the way to their boxes), identical in both
decoders -- chroma LEVEL, not offset (the capture's restored-chroma
  amplitude), so it is gain territory, not calibration territory.

## The vectorscope-derived correction (what to set in the analyse GUI)

Least-squares fit of the measured colour-bar vectors to the graticule boxes
(complex fit: c = sum(w*conj(z))/sum(|z|^2), the same thing the vectorscope
+ chroma gain/phase sliders do by eye):

- etienne colorbars: **gain 1.425, phase +0.19 degrees** -- essentially pure
  gain; the phase is negligible because the measured-carrier calibration
  already nailed the rotation. Mean per-bar residual 0.178 -> 0.099 with
  `--chroma-gain 1.425` applied (verified).
- What remains after the gain is an ELLIPSE, not a circle: the Db-axis bars
  (yellow/blue) land at ~0.85 of their boxes, the Dr-axis bars
  (cyan/green/red/magenta) at ~0.60-0.67. The Dr carrier sits 156 kHz above
  the Db carrier, so the capture's HF channel response compresses the Dr
  deviation excursions more -- identical in the old and new decoders, so it
  is the capture, not the demodulator. A uniform chroma gain cannot split the
  axes (that would need per-axis gain or channel equalization); 1.425 is
  the least-squares optimum, leaving the Db bars ~20% over and the Dr bars
  ~16% under.

GUI workflow: load the luma + `_chroma.tbc` pair in `build/bin/tbc-analyse`
(the working-tree decoder), open Scopes > Vectorscope, and set Video
decoder configuration > chroma gain to ~1.43 (phase stays 0). The
vectorscope graticule is the same box set used above. Reference renders:
`/tmp/secam-ab/analyse-style-new-etienne-g1425.png` and
`vectorscope-new-etienne-g1425.png`.

To make the correction permanent for the source, store `chromaGain: 1.425`
in the capture's `.tbc.json` videoParameters (tbc-analyse applies metadata
gain on load) -- a per-source calibration decision.
