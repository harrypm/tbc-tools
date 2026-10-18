# Dev log — SECAM chroma decoder: combined-input luma low-pass (dot-crawl fix) - 2026-10-18

Paired with tape-decode-rust's CVBS single-combined-.tbc feature
(see that repo's `CVBS_SINGLE_TBC_20261018.md` + `prompt_logs/PROMPT_cvbs_single_tbc_20261018.md`).
This note covers only the tbc-tools side.

## Context

tape-decode-rust now writes a single combined CVBS `.tbc` for the
direct-composite formats (SMPTE Type C / Quadruplex, including SECAM) - the
demodulated composite, chroma block included. The 2026-10-15 session's
conclusion ("the split _chroma.tbc is the only clean path for this capture")
was inverted on the decode side: the OLD combined output was dots-LPF'd
(3.2 MHz/order 12, block at ~1/17 of luma level, unusable for chroma decoding);
the NEW combined output is full-band - the block is at full level, so the
chroma decoder works from the single file. What remained was the luma dots.

## Root cause

`SecamDecoder::decodeFrames` wrote the output luma as a raw passthrough of the
input line ("composite passthrough ... keeps the decoder sane if it is ever run
on a combined source"). For split-chroma input that is correct (the input line
has no luma content; the real luma comes from the separate luma TBC). For a
combined input the SECAM FM block rode straight into the output video as luma
dots. The PAL decoder never had the problem (palcolour computes luma as
composite minus demodulated chroma).

## Fix (gated on the existing per-field `combinedInput` detection)

- `FieldWork` gains `maskLuma` (frequency-domain real gain, rebuilt with the
  chroma masks so it follows the measured block centre + carrier offset) and
  `lumaFiltered` (per-field samples).
- New `filterRowLuma()`: same block-FFT context scheme as
  `filterRowAnalytic` (2048-block, 456-sample margins, overlap-save), real
  symmetric gain, zero-phase result.
- `decodeField`: run `filterRowLuma` over all rows inside the pass loop when
  `combinedInput` (re-runs after a mask recentre).
- `decodeFrames` Y loop: combined input -> `outY = lumaFiltered`; split
  chroma -> unchanged passthrough.

## Tuning (two attempts, both measured)

- 3a: notch around the steady block (gain = 1 - bandGain, the combined band's
  +-580 kHz stop). Steady carriers died (4.1-4.6 MHz = 143 vs millions) but the
  user saw residual dot crawl ON COLOUR TRANSITIONS: at an edge the FM carrier
  sweeps the full deviation and its transient sidebands spread across the band
  skirts, which a steady-block notch cannot catch.
- 3b (shipped): steep luma low-pass below the FM block floor - the
  receiver-standard SECAM luma path. Gain 1 to 3.1 MHz, raised cosine to
  3.35 MHz, 0 above (anchored blockCentre + offset - 1.23 MHz / -0.98 MHz).
  Corner + steepness match the separated-mode decode the user validated on this
  tape (3.2 MHz order-12 luma LPF; the tape-decode profiles keep that shape for
  the S-Video path).

## Hard data (etienne colourbars combined cut, -p yuv 928x576 YUV444P16)

- `SECAMINPUT combined=1` on all 26 fields (detection unchanged).
- Output Y, mean over picture lines 100-160: block band 4.1-4.6 MHz =
  0.00002 of luma (raw combined input 0.264; old Y/C luma 0.0068);
  block+sideband above 3.35 MHz = 0.0009. 0-3 MHz luma untouched
  (5.75M/488k/551k bands unchanged).
- ctest: 33/33 passed (incl. decode-pretbc-secam-etienne-cbars, both MESECAM
  fixtures) after each change; guardrail contracts untouched.
- Frames for eyeballing: /tmp/cvbs_check/v4/lpf2_frame{0,6}.png,
  lpf2_frame0_full.png (notch-era: fixed_frame*.png for the A/B).

## Round 2 (2026-10-18): chroma accuracy - white bleed + vectorscope tightening

User: colour bleed into the whites, vectorscope should tighten much more (lines
straight). Measured baselines (per-bar U/V over the colourbars fixtures):

- SPLIT chroma fixture (healthy path): white U/V residual -330/-1479, per-bar
  std 296-954 - the capture's noise floor.
- The 10f COMBINED fixture is the OLD dots-LPF'd decode (block ~1/17 level,
  BARDIAG envelopes 16-190, ident falls back to content statistics) - the
  pathological input from the 10-15 session; NOT representative of the new
  full-band combined output. The real subject is the tape-decode v5 combined
  cut: white residual ~1290, per-bar std 335-1559, block envelopes 2000-6221.

Changes (all measured, all ctest-green):
1. Per-line smoothed rest correction: each valid row's porch median is that
   line's rest carrier; raw use injects the ~5 kHz porch noise (net worse), so
   the per-row rests are smoothed with a same-parity +-16-row moving average
   (~2 ms, tracks wow/flutter to a few hundred Hz, ~4x noise averaging).
   Between-line wander improved 225 -> 168 (beats the split path's 225);
   per-pixel U noise spectra are at PARITY with the split path.
2. Colour-difference bandwidth filter: post-de-emphasis raised-cosine LPF
   1.6 -> 1.95 MHz applied with the LF inverse. Measured neutral (the demod
   noise is not concentrated above 1.95 MHz) - kept: the D'B/D'R signal is
   band-limited by the encoding chain, the discriminator's out-of-band
   disturbance is not signal.
3. Rest-carrier separation lock: BT.470 fixes the pair at EXACTLY 10 x line
   rate (D'b 272 fH / D'r 282 fH = 156250 Hz on 625/50); the cluster medians
   locate the centre reliably but their split is skewed by porch ringing
   (measured 177.9 kHz on the combined input). The applied pair is now
   centre +- 78125 Hz.

Net: white residual ~1290 U-units (~2.3% of scale) - CLEANER than the split
path's own 1809 on the same measure; per-pixel U/V noise at parity with split;
between-line wander better than split.

## Colour-scale verdict: the capture, not the decoder

Direct FM measurement of the yellow-bar D'B carrier (spectral peak, 4 fields x
31 lines): combined v5 -145.6 kHz vs split _chroma fixture -145.9 kHz (ratio
0.998) - the combined .tbc is byte-faithful to the split chroma. Against the
bars' own luma (which matches 75% bars exactly: yellow Yn 0.666 vs 0.665,
magenta 0.312 vs 0.311, red 0.225 vs 0.225): D'B measures ~120-150% hot
(yellow -145.6 kHz vs the 75% theory -101.5 kHz) while D'R measures ~95%
(red -75.4 kHz effective vs -77.4 theory). The machine recorded the
blue-difference axis hot - capture character, not a decoder or pipeline bug.
The existing chromaGain knob (tbc-analyse / tbc-chroma-decoder) is the user
trim; a decoder-side auto-correction would bake one machine's level into the
tool.

## Physics limit (documented, deliberate)

Luma inside/around the FM block is spectrally identical to chroma in one
signal. Combined CVBS output always carries the block in the raw .tbc (that is
the colour); the decoded luma is clean only to the depth of the low-pass.
Zero dots + single file is not possible; zero dots + full colour = the split
Y/C two-file mode.

## Note for the 2026-10-15/18 port session owner

This commit lands `secamdecoder.cpp/.h` including the pending libchromadec
v0.1.0 parameter port (that session's uncommitted work rides in the same
files); the port's own log is
`development-logs/prompt_readme_secam-vsanalogue-param-port_2026-10-15.md`
(left uncommitted for that session's owner to dump).
