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
