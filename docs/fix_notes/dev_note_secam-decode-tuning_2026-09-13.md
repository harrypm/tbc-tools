# Dev Note — SECAM chroma decode tuning (2026-09-13)

Session that took the SECAM FM chroma decoder from "flickering / blue cast /
mono default" to a working, tunable state. All changes are committed on `main`
and pushed to `origin` (harrypm/tbc-tools).

## Commits (chronological)

- `a3da10c` — Fix SECAM full-frame chroma flicker and revert bbf977b decoder regressions
  - Root cause: `SecamDecoder::configure` rest-carrier window collapsed onto the
    sync tip when `applyFullFrameDecodeBounds` widened `activeVideoStart` to the
    horizontal margin (`restEnd = activeVideoStart-10` underflowed past
    `restStart = colourBurstStart`, fallback to `activeVideoStart/4../2` =
    samples 4-8 = sync noise). The per-field D'R/D'B parity vote then read
    sync-tip frequency (~4.65 MHz) instead of the back-porch rest carrier
    (~4.26 MHz) and flipped erratically frame to frame.
  - Fix: when the activeVideoStart-anchored window underflows, fall back to a
    window just past `colourBurstEnd` (back-porch rest carrier) instead of the
    sync. `colourBurstStart/End` are untouched by full-frame widening.
  - Also reverted the bbf977b decoder-side changes that regressed hybrid/full-
    frame chroma extent (decoderpool `applyFullFrameDecodeBounds`, outputwriter
    `chromaInActiveArea` gate, secamdecoder `fillChannel` V-interval re-zero).
  - Verified: full-frame parity 0 anomalies/24 frames; per-frame Cb/Cr stable.

- `76e78aa` — Fix MESECAM blue cast, add SECAM chroma-phase, default SECAM to SECAM decoder
  - MESECAM blue cast: rest carriers globally offset ~+90 kHz above nominal
    (redRestMed ~4.488 MHz vs f_R 4.40625M; blueRestMed ~4.347 MHz vs f_B
    4.250M). One offset, two symptoms: parity vote broke (fixed-block-centre
    threshold) + blue cast (60 kHz REST_TOLERANCE rejected the offset, fell
    back to nominal, offset leaked into D'B). Fixed with cluster-midpoint
    parity threshold + per-identity median reference fallback.
  - `--chroma-phase`: hue rotation of (D'B, D'R) in decodeFrames.
  - Default SECAM/MESECAM to the SECAM decoder (was mono): root cause was
    `tbcmetadata.cpp` VideoParameters::read defaulting empty chromaDecoder to
    "mono". Changed to "secam"; aligned CLI auto-select + ld-analyse.
  - Verified: MESECAM Cb 42000-47800 -> 33990-34380; parity 0 anomalies.

- `8c7d00e` — Tighten SECAM demod lowpass to chroma block to cut combined-source luma dots
  - DEMOD_LOWPASS_HZ 600kHz -> 450kHz (bandpass 3.878-4.778 MHz, covers the
    +-428 kHz block with margin, rejects out-of-block luma HF).
  - Etienne (combined) flat-bar Ustd 1560 -> 684 (-56%).
  - Documented: in-band luma (3.9-4.756 MHz) is not removable by any filter;
    split chroma input is the fully clean path.

- `d7c8ffb` — Enable SECAM chroma-phase slider and add post-demod median for luma dots
  - ld-analyse: chroma-phase slider was disabled for SECAM; enabled for the
    secam case (kept disabled for mono/secam-predemod). Wired chromaPhase
    through TbcSource::configureChromaDecoder + applyChromaSettingsFromMetadata.
  - secamdecoder: 3-tap horizontal median on D'B/D'R after the one-line hold
    (clips impulsive spikes). Modest gain; the residual is broadband in-band.

- `a514b7b` — Add SECAM FM click concealment, deviation rail, and V-interval neutralization
  - Restructured decodeField into a staged pipeline (ported from libchromadec
    SecamDecoder): raw demod -> click concealment -> deviation rail ->
    de-emphasis -> one-line hold.
  - FM click concealment ("SECAM fire"): detect envelope collapse (dip dB under
    row median) or deviation overshoot past BT.470 Table 4 maxima; conceal
    short spans by interpolation, wide spans by borrowing the previous
    same-component line. New `--secam-click-nr` level (default 1.0, 0 bypass).
  - Deviation rail: clamp to Table 4 max deviations (asymmetric D'B
    [-350,+506]kHz, D'R [-506,+350]kHz) before de-emphasis.
  - V-interval neutralization: `nominalFirstActiveFieldLine` threaded from
    the original metadata before full-frame widening; rows below it (bottles /
    test-signal lines, which carry real carrier but are not picture) have
    chroma output zeroed. Fixed the "chroma garbage outside active area" in
    full-frame CLI mode (V-interval rows 0-40 Ustd 13562 -> 0.00).
  - Bandpass reverted to 450kHz (the 800kHz experiment let sync/V-interval
    noise through on the combined etienne source; the libchromadec reference's
    wide band assumes a split chroma input).

## Reference consulted

Cloned and read `vapoursynth-analog` + its `libchromadec` subproject
(`https://github.com/JustinTArthur/libchromadec.git`, v0.1.0):
`src/decoders/secam/secam_decoder.cpp`. That is the source of the click-
concealment + deviation-rail design (and the FFT bell-network + baseband
discriminator, which tbc-tools does not yet have).

## Test data

- `/home/harry/Desktop/SECAM/etienne_Quadruplex_colorbars-20260824_SECAM_MISRC2.5_16bit_40mhz.tbc`
  (+ .tbc.json) — SECAM Quadruplex colorbars, **combined** luma+chroma (no
  _chroma split). 330 fields / 165 frames, 1135x313, 17.7345 MHz.
- `/home/harry/Desktop/SECAM/snippets/` — 50-frame snippets of the above plus
  `mesecam_tape275_testcard_50f` (+ _chroma.tbc + .tbc.json, MESECAM split).

## Remaining caveat (honest)

The dots still visible on the etienne combined source are **in-band luma**
(luma HF inside 3.9-4.756 MHz), spectrally identical to chroma. No filter or
concealment removes those without desaturating real chroma. The fully clean
path for a combined source is a split chroma input (`_chroma.tbc`, as
vhs-decode's MESECAM/VHS path produces). The bigger remaining work is the
FFT bell-network (cloche) + baseband-discrimination rewrite from libchromadec
— noted but not started; even that does not remove in-band luma.

## Tests

ctest 30/30, `ci/check_ci_contracts.py` pass, tbc-video-export pytest 73/73
(run during the earlier commit cycle).

## Diagnostics retained

SECAM parity diagnostics (`SECAMCFG` / `SECAMPROBE` / `SECAMDIAG` stderr
prints + `FieldWork` rest-carrier stats) are still in `secamdecoder.cpp` /
`.h` for ongoing SECAM tuning. They write to stderr only; no effect on output.
