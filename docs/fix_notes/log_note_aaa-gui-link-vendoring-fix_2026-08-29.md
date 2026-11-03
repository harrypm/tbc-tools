# Log note — AAA GUI link + AppImage vendoring fix validated (2026-08-29)

User-confirmed fixes. Two distinct issues are now fixed, built, tested, and
verified end-to-end (including real GUI confirmation). Both commits are on
origin/main.

## Commits on origin/main
1. `0a39dda` — aaa: tighten auto file loading + provision RF source-rate from metadata
2. `20950a1` — aaa: fix bundled AAA not found in AppImage (loader-wrapper applicationDirPath)

## Issue A — auto file loading targeted RF files + RF rate never auto-set (commit 0a39dda)
### Root cause (proven against real data)
For the standard vhs-decode naming (`<stem>-video.tbc.json` +
`<stem>-linear.flac` + `<stem>-hifi.flac` + `<stem>-video.flac`), the JSON
root derived to `<stem>-video`, so the linear/hifi stems did NOT relate to it
under the prefix-match rules. Preferred-detect returned None and the `Any`
fallback picked the RF `<stem>-video.flac` (60 GB) for BOTH tracks. Verified
by reproducing the exact scoring algorithm against the real
`/media/harry/20TB HDD1/Hugo_UK_2025/VHS_Tape_01` directory:
Linear→None, Hifi→None, Any→`...-video.flac` (score 1318). Separately, the
dialog's RF Video Sample Rate (AAA `--rf-video-sample-rate-hz`, default 40
Msps) was never auto-set; ld-analyse's MainWindow never passed it.

### Boundary rule (USER-PROVIDED — enforced in code + test)
The JSON `videoParameters.sampleRate` is the DECODED `.tbc` format rate, NOT
the source RF capture timebase AAA aligns against. `VHS_Tape_01` proves they
differ: decoded 17,734,475 Hz (4Fsc PAL) vs source 40,000,000 Hz (cxadc 40
Msps). The RF-rate provision must ONLY read an explicit RF-source field and
must NEVER fall back to `videoParameters.sampleRate`.

### Fix (src/audio-align)
- `hasExcludedAutoInputKeyword`: also excludes `video`/`chroma` substrings so
  RF/video dumps in audio containers (`<stem>-video.flac`) are never selected.
- `appendDerivedRootCandidates`: strips a single trailing `-video`/`_video`
  so the capture `<stem>` is a root candidate; linear/hifi then match.
- `autoDetectLinearInputAudioFile` / `autoDetectHifiInputAudioFile`: no longer
  cross-fill the other track type. A field stays EMPTY when its own track is
  absent (each field fills only with its own track type).
- New `AudioAlignmentUtil::detectRfSourceSampleRateFromJson`: reads an
  explicit `videoParameters.rfSourceSampleRate` (aliases
  `rfSourceSampleRateHz` / `rfSourceFreq` / `rfSampleRate`); returns 0 when
  absent — never falls back to decoded `sampleRate`.
- Dialog auto-sets RF rate via `applyRfSourceRateFromJson` on
  `setDefaultJson` + browse; keeps 40 MHz default when no explicit field;
  CLI `--rf-video-sample-rate-hz` still wins (applied after setDefaultJson).

### Tests
- `aaa-detect-inputs` (standard naming + no-cross-fill hifi-only/linear-only
  cases) and `aaa-detect-rf-source-rate` (explicit field returned; no-field
  returns 0). ctest pass; CI contract checks pass.

### Verification
- ctest 29/29 (then 30/30 after Issue B's test) pass; CI contract checks pass.
- Real GUI (bundled AAA, not PATH): Hifi→`-hifi.flac`; Linear never targets
  RF `...-video.flac` (when a folder has no `-linear.flac`, Linear stays
  empty per the tightening). RF rate auto-switched to 20 MHz with a synthetic
  `rfSourceSampleRate` JSON; stayed 40 MHz default without it. User-confirmed.

## Issue B — "VhsDecodeAutoAudioAlign executable not found in path or vendored locations" in the real AppImage (commit 20950a1)
### Root cause (proven via /proc/self/exe + minimal Qt applicationDirPath probe)
The AppImage ld-analyse bash wrapper launches the real ELF via the bundled
glibc loader:
`exec <usr/lib/ld-linux-x86-64.so.2> --library-path usr/lib <usr/bin/.ld-analyse.real>`.
When a binary is launched via `ld-linux`, `/proc/self/exe` AND Qt's
`QCoreApplication::applicationDirPath()` resolve to the LOADER's directory
(`usr/lib`), NOT the target's (`usr/bin`). Probed directly:
- `/lib64/ld-linux-x86-64.so.2 /bin/readlink /proc/self/exe` →
  `/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2` (the loader, not readlink).
- A minimal Qt console probe: run directly → applicationDirPath = its own
  dir; run via the loader → applicationDirPath = the loader's dir (both with
  and without `--argv0`).

So `resolveAudioAlignExecutablePath` probed
`applicationDirPath()/vendor/...` = `usr/lib/vendor/...` (absent) and never
found the bundled AAA at `usr/bin/vendor/...`. The bundled AAA AppImage
itself was correctly placed and runnable (`show-build-info` → v1.0.2, rc=0).

Gap in earlier verification: the staged test used the Nix wrapper (execs
`.ld-analyse-wrapped` directly, no loader), so applicationDirPath was
correct and the failure never reproduced locally. The real AppImage uses the
loader wrapper — that condition was not reproduced until today.

### Fix (src/audio-align/audioalignmentutil.cpp)
- `resolveAudioAlignExecutablePath`: now also probes `../bin` and
  `../bin/vendor/vhs_decode_auto_audio_align` relative to
  `applicationDirPath()` (AppImage loader case: `usr/lib` → `usr/bin`). Honors
  an explicit `TBC_TOOLS_APP_BIN_DIR` env override (HIGHEST priority) the
  launcher may export to declare the real binary directory.
- `resolveBundledOrPathTool` (ffmpeg/ffprobe): same `../bin` candidate +
  `TBC_TOOLS_APP_BIN_DIR` override for defense-in-depth.

### Test
- New `aaa-detect-aaa-envbin` ctest: stages a fake bundled AppImage under a
  temp dir's vendor path, exports `TBC_TOOLS_APP_BIN_DIR`, and asserts
  `resolvedAudioAlignPath` returns it (the override takes priority over the
  build-tree `AUDIO_ALIGN_VENDOR_DIR` .exe candidate). ctest 30/30 pass; CI
  contract checks pass.

### Verification (real AppImage runtime)
- Headless loader-wrapper reproduction (staged `usr/lib` loader +
  `usr/bin/vendor/AAA`): resolver found
  `usr/lib/../bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage`
  and launched it (rc=0).
- Real GUI: the fixed ld-analyse ELF was swapped into the extracted Desktop
  AppImage (`/home/harry/Desktop/tbc-tools-x86_64.AppImage`) and run via its
  OWN loader wrapper (so applicationDirPath = `usr/lib`, the real AppImage
  condition). Tools → Auto Audio Align opens with NO "not found" error and
  Align runs the bundled AAA. User-confirmed twice. The extracted tree's
  original binary was restored and the test GUI stopped during cleanup.

## Note on the existing Desktop AppImage
The AppImage on the Desktop currently has the OLD ld-analyse binary. The fix
is in the resolver source, so the next CI-built AppImage picks it up
automatically. To get a fixed AppImage before the next CI build, rebuild
locally per `dev_note_local-appimage-build_2026-08-28.md` (mind Gotchas 1–3)
or wait for the next green CI artifact.

## Restore point
- Validated working tree state is origin/main @ `20950a1` (both fixes).
- A zip of the changed source at this commit is preserved alongside this
  note: `aaa-gui-link-vendoring-fix_2026-08-29.zip`.
- The detailed session readme is `prompt_readme_aaa-gui-link-test_2026-08-28.md`
  (committed in 20950a1) and supersedes the prior session notes for these
  two fixes; the earlier `log_note_aaa-linux-fix-validated_2026-08-28.md`
  remains valid for the OL8/arm64 build fix + eafcdb7 detection tests.
