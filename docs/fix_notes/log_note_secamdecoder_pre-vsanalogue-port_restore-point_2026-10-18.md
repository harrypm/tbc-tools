# Log note — secamdecoder pre-port restore point (2026-10-18)

## What this is

Restore point for the FM SECAM chroma decoder **before** the vapoursynth-analog
(libchromadec v0.1.0) demodulation-parameter port, taken at the user's request
before the rewrite begins.

- **Zip:** `development-logs/secamdecoder_pre-vsanalogue-port_20261018_020621.zip`
  containing `secamdecoder.cpp` + `secamdecoder.h` (unzip with `-j` paths are
  flat; restore to `src/tbc-chroma-decoder/`)
- **Git state:** commit `4e91a5c` (HEAD of main, both files clean in the tree —
  the zip bytes == the committed versions), so the code is also recoverable
  via `git checkout 4e91a5c -- src/tbc-chroma-decoder/secamdecoder.cpp src/tbc-chroma-decoder/secamdecoder.h`

## What this version is (current behaviour being replaced)

The staged pipeline as tuned on 2026-09-13
(`development-logs/dev_note_secam-decode-tuning_2026-09-13.md`):

- Quadrature demodulator: local oscillator at the block centre, 41-tap
  Hamming-windowed sinc lowpass at ±450 kHz (3.878–4.778 MHz), single-sample
  phase-difference discriminator
- Rest-carrier window `colourBurstStart .. activeVideoStart−10` with
  back-porch/blanking fallbacks (full-frame widening safe)
- Parity vote over all picture rows with cluster-midpoint threshold; per-row
  discriminator zero (measured rest carrier within 60 kHz of nominal, else
  per-identity field median clamped to 200 kHz)
- Colour killer 0.25× field median envelope; FM click concealment
  (dip 12−6·level dB, overshoot max(2.6−1.6·level, 1.15)); deviation rail
  (±350/±506 kHz asymmetric); bilinear-IIR de-emphasis (f1 = 85 kHz);
  one-line hold; hue rotation; OutputWriter U/V pre-scaling
- NO bell (cloche) network; NO per-field carrier calibration/recentring
  (MESECAM +90 kHz offset handled only by the median references)

## Measured baselines on the new fixtures (this version)

- `test-data/secam/secam-etienne-cbars-10f` (combined): `--decoder secam`
  10 frames; VITS bPSNR median 47.3 dB (min 45.9) — luma metric, sanity only
- `test-data/secam/mesecam-tape275-testcard-10f_chroma` (split chroma):
  `--decoder secam` 9 frames
- ctest floors wired in `CMakeLists.txt`: etienne bpsnr 45 / frames 10,
  tape275 chroma frames 9 (see the 2026-10-15 snips prompt readme for the
  per-frame A/B capture procedure)

## Why the restore point

The port rewrites the demodulation internals to the libchromadec pipeline
(FFT analytic + band×bell masks, baseband differentiating-FIR discriminator,
per-field calibration + mask recentring, noise-adaptive click concealment,
exact FFT de-emphasis). If the rewrite needs to be rolled back or compared
side-by-side, this zip + the git commit are the reference version.
