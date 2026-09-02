# Prompt README — AAA mono-input fix + Convert-mono-to-stereo checkbox

**Date:** 2026-09-02
**Repo:** `/home/harry/tbc-tools` (branch: `main`)
**Model:** glm 5.2 (Oz)

## User input (requests, in order)

1. "There is somthing very wrong with the currnet tbc-tools v3.2.8 on this
   system" — pasted an ffmpeg failure:
   `input channel 'FL' not available from input layout 'mono'`; noted the input
   is a simple mono file (`VTC5000_..._Mono_Linear_audio_ch1.wav`) that "should
   not have snaged".
2. "but this error is from auto audio align not tbc-video-export" — redirected
   the investigation from the export wrapper to the AAA path.
3. "Map mono to stereo then and commit/push this fix after doing a test run"
4. "build so I can run though locally"
5. "okay add a convert mono to stero checkbox that is enabled by defult with a
   linear only mono file loaded for AAA GUI"
6. "Commit and push changes update internal docs"

## Root cause

`AudioAlignmentUtil::runStreamAlign` (src/audio-align/audioalignmentutil.cpp)
decoded the input to interleaved stereo s24le for AAA using a hardcoded
`channelmap=map=FL-FL|FR-FR` filter — a stereo identity remap that requires the
input to already expose `FL`/`FR`. A mono input has no `FL`/`FR` labels, so
ffmpeg aborted with `input channel 'FL' not available from input layout 'mono'`.
AAA always consumes interleaved s24le where one "sample" is
`--sample-size-bytes = 24-bit * channels`; the legacy code assumed stereo (6
bytes) and had no mono path.

## Design summary

### Commit 1 — mono-aware decode (already pushed as `9d6e17e`)
- Added `detectAudioChannelCount()` (ffprobe `stream=channels`), mirroring the
  existing `detectAudioSampleRateHz()`.
- Decode filter is built from the detected channel count: mono uses
  `pan=stereo|FL=c0|FR=c0` (up-mix, layout-agnostic `c0` index into both FL/FR);
  multi-channel keeps the identity `channelmap=map=FL-FL|FR-FR`. Output stays
  interleaved stereo s24le; `--sample-size-bytes 6` and the encode step unchanged.

### Commit 2 — Convert-mono-to-stereo checkbox (this commit)
- `runStreamAlign` gained a trailing `bool convertMonoToStereo = true` param
  (defaults preserve existing callers, incl. the ctest runtime harness).
- Mono path is now selectable:
  - **convert (default):** mono -> stereo s24le (2ch, `--sample-size-bytes 6`),
    aligned output is a stereo FLAC.
  - **keep-mono:** mono passes through unchanged (no channel filter, 1ch,
    `--sample-size-bytes 3`), aligned output is a mono FLAC.
  - `outputChannels` and `sampleSizeBytes` are derived once and reused by the
    decode, AAA `--sample-size-bytes`, and encode `-ac` so all three stay
    consistent. No effect on stereo/multi-channel or HiFi inputs.
- AAA dialog (`audioalignmentdialog.ui`) gained a "Mono Conversion /
  Convert mono to stereo (linear/baseband)" checkbox, checked by default,
  always interactive (disabled only while an alignment run is in progress),
  with a tooltip. It applies to the **Linear/Baseband** track only (HiFi always
  up-mixes). Wired through `AlignmentTrackRequest::convertMonoToStereo` ->
  `runStreamAlign`'s 9th arg.

## Files modified

### Commit 1 (`9d6e17e`, pushed)
- `src/audio-align/audioalignmentutil.cpp` — `detectAudioChannelCount()`;
  `runStreamAlign` detects channel count and selects the decode filter.

### Commit 2 (this commit)
- `src/audio-align/audioalignmentutil.h` — `runStreamAlign` gains trailing
  `bool convertMonoToStereo = true`.
- `src/audio-align/audioalignmentutil.cpp` — `runStreamAlign` signature; mono
  path selects `outputChannels`/`sampleSizeBytes`/filter (keep-mono = no filter,
  1ch, 3 bytes; convert = `pan=stereo|FL=c0|FR=c0`, 2ch, 6 bytes); AAA
  `--sample-size-bytes` and encode `-ac` use the derived channel count.
- `src/audio-align/audioalignmentdialog.ui` — new row 9: `monoConversionLabel`
  + `convertMonoToStereoCheckBox` (checked by default).
- `src/audio-align/audioalignmentdialog.h` — `AlignmentTrackRequest` gains
  `bool convertMonoToStereo = true`.
- `src/audio-align/audioalignmentdialog.cpp` — checkbox default+tooltip in ctor;
  added to `setAlignmentUiBusy`; `on_alignButton_clicked` sets
  `request.convertMonoToStereo` from the checkbox and passes it as the 9th arg
  to `runStreamAlign`.
- `development-logs/prompt_readme_aaa-mono-upmix-checkbox_2026-09-02.md` — this
  file.

## Commands run

1. Reads/grep of tbc-video-export wrapper + opts (ruled out as the source after
   the user corrected the path to AAA), then `src/audio-align/*`.
2. ffprobe on the real mono WAV → `channels=1`, `sample_rate=78125` (matches the
   failure: 78125 Hz mono pcm_s24le).
3. Reproduced the failure with the OLD filter on a 5s slice; verified the NEW
   `pan=stereo|FL=c0|FR=c0` filter decodes to byte-exact 2,343,750-byte
   interleaved stereo s24le (5s x 78125 x 2ch x 3 bytes), no errors.
4. `git commit` (9d6e17e) + `git push origin main` (rebased onto origin/main
   after stashing the unrelated pre-existing `ci/*` working changes, then
   restored the stash).
5. `nix develop -c ninja -C build ld-analyse` → Success (incremental relink;
   `build/bin/ld-analyse`).
6. Validated both AAA codepaths' ffmpeg invocations against the real mono WAV:
   - Convert (checkbox ON): raw 2,343,750 B; output FLAC 48000 Hz / 2ch.
   - Keep-mono (checkbox OFF): raw 1,171,875 B (5s x 78125 x 1 x 3); output FLAC
     48000 Hz / 1ch.
7. `nix develop -c ninja -C build ld-analyse` (after the checkbox UI change) →
   Success; UIC regenerated `ui_audioalignmentdialog.h` with the new
   `convertMonoToStereoCheckBox`/`monoConversionLabel`; relinked.

## Validation status

- [x] Clean compile + link of `ld-analyse` (both rebuilds).
- [x] UIC generated the new checkbox/label widgets from the edited `.ui`.
- [x] ffprobe confirms the real input is mono (channels=1) — the bug trigger.
- [x] OLD `channelmap` filter reproduced the exact reported failure on the real
      file; NEW filters decode it with no errors.
- [x] Both AAA ffmpeg codepaths (convert + keep-mono) byte-verified against the
      real mono WAV (decode + encode), producing stereo vs mono FLAC as
      intended; `--sample-size-bytes` is consistent with the channel count.
- [ ] **Runtime GUI confirmation — not yet reported back.** The checkbox was
      built and the ffmpeg paths it drives were byte-verified, but the
      end-to-end AAA dialog run through `ld-analyse` against the mono Jupiter
      capture has not been confirmed by the user. Build to run locally:
      `/home/harry/tbc-tools/build/bin/ld-analyse` (load the Jupiter TBC +
      `.tbc.json`, open Auto Audio Align, confirm the checkbox is ticked by
      default and Align succeeds; then untick and confirm a mono aligned output).

## Notes

- Commit 1 (`9d6e17e`) is already on `origin/main`. Commit 2 (checkbox + this
  readme) is staged for this push.
- `runStreamAlign`'s new param is trailing with a default, so the ctest runtime
  harness (`testaudioalignmentruntime.cpp`) and other callers compile unchanged.
- The pre-existing `ci/check_ci_contracts.py` + `ci/verify_linux_bundle.sh`
  working-tree modifications (the AAA `stream-align` smoke-test additions) were
  NOT touched by this work; they remain unstaged in the working tree as before.
- No restore-point zip (created only when the user states a change is
  "fully fixed/working" — not yet stated).
- No child agents; edits tightly coupled across util + dialog, done sequentially.
