## Next
- HiFi (22 GB) full align deferred (slow). Optional later run.
- CI verifier smoke fixes (stray quote in fixture JSON; ffmpeg -nostdin</dev/null
  or python3 s24le synthesis) TABLED per user.
- Bundled AppImage Qt xcb ABI mismatch (system Qt 6.10 plugin vs Nix Qt 6.8.3
  core) TABLED — packaging issue, not AAA.

## AppImage vendoring fix (2026-08-28): AAA "not found in path or vendored locations"
Root cause (proven via /proc/self/exe probe + minimal Qt applicationDirPath probe):
- The AppImage ld-analyse bash wrapper launches the real ELF via the bundled
  glibc loader: exec <usr/lib/ld-linux-x86-64.so.2> --library-path usr/lib
  <usr/bin/.ld-analyse.real>. When a binary is launched via ld-linux,
  /proc/self/exe (and Qt's QCoreApplication::applicationDirPath()) points at
  the LOADER's dir (usr/lib), NOT the target's dir (usr/bin).
- resolveAudioAlignExecutablePath probed applicationDirPath()/vendor/... =
  usr/lib/vendor/... (absent), so the bundled AAA at usr/bin/vendor/... was
  never found -> "not found in path or vendored locations". The AAA AppImage
  itself was correctly bundled (show-build-info runs, v1.0.2).
Fix (audioalignmentutil.cpp resolveAudioAlignExecutablePath + resolveBundledOrPathTool):
- Probe ../bin relative to appDir (AppImage loader case: usr/lib -> usr/bin)
  and ../bin/vendor/vhs_decode_auto_audio_align, so the bundled AAA is found
  when applicationDirPath() reports the loader's dir. Also honors an explicit
  TBC_TOOLS_APP_BIN_DIR env override (highest priority) the launcher may export.
- Same ../bin + env override added to resolveBundledOrPathTool (ffmpeg/ffprobe)
  for defense-in-depth.
Test: new aaa-detect-aaa-envbin ctest stages a fake bundled AppImage under a
 temp dir's vendor path, exports TBC_TOOLS_APP_BIN_DIR, and asserts
 resolvedAudioAlignPath returns it. ctest 30/30 pass; CI contract checks pass.
Verification: headless loader-wrapper reproduction (staged usr/lib loader +
 usr/bin/vendor/AAA) -> resolver found usr/lib/../bin/vendor/.../AAA and
 launched it (rc=0). Real GUI confirmation with the fixed ld-analyse swapped
 into the extracted Desktop AppImage (run via its own loader wrapper, so
 applicationDirPath=usr/lib): Tools -> Auto Audio Align opens with NO
 "not found" error and Align runs the bundled AAA (user-confirmed, twice).
Note: earlier staged test passed only because it used the Nix wrapper (execs
 .ld-analyse-wrapped directly, no loader) so applicationDirPath was correct.
 The real AppImage uses the loader wrapper — that was the gap.

## Follow-on change (2026-08-28): tighten auto file loading + RF source-rate provision
Plan: e8884ad6. Implemented + built (nix build .#) + ctest 29/29 pass
(incl. new aaa-detect-inputs, aaa-detect-rf-source-rate).
- audioalignmentutil.cpp: hasExcludedAutoInputKeyword now also excludes
  "video"/"chroma" (RF dumps in audio containers); appendDerivedRootCandidates
  strips a trailing -video/_video so linear/hifi match the capture stem.
- New AudioAlignmentUtil::detectRfSourceSampleRateFromJson reads an explicit
  videoParameters.rfSourceSampleRate (aliases rfSourceSampleRateHz/rfSourceFreq/
  rfSampleRate); NEVER falls back to decoded sampleRate (boundary rule enforced
  + tested). Dialog auto-sets RF rate via applyRfSourceRateFromJson on
  setDefaultJson + browse; CLI --rf-video-sample-rate-hz still wins.
- GUI confirmation (bundled AAA, not PATH; staged /tmp/aaa-bundled-stage):
  * Hifi auto-fill -> -hifi.flac (correct).
  * Linear: with a TBC already loaded from a folder with NO -linear.flac,
    the weighted fallback filled -hifi.flac (the only track) — and did NOT
    target the RF -video.flac (main fix holds even in fallback).
  * RF rate auto-set: synthetic JSON /tmp/aaa-rfsource-test with
    rfSourceSampleRate=20000000 switched the preset to 20 MHz (confirmed by
    user). Default stays 40 MHz when no rfSourceSampleRate field (confirmed).
- Tightened (per user decision): autoDetectLinearInputAudioFile and
  autoDetectHifiInputAudioFile no longer cross-fill the other track type.
  When no -linear.flac exists, Linear stays EMPTY (does not fill -hifi.flac);
  same for Hifi. Each field fills only with its own track type. Verified by
  expanded aaa-detect-inputs test (hifi-only and linear-only cases) + full
  ctest 29/29 + CI contract checks pass.

# Prompt README — AAA GUI link test (2026-08-28)

## Goal
Confirm the AAA (Auto Audio Align) link shows up inside ld-analyse and is
callable on **real** user-provided data (not just detection/smoke tests).
CI verifier work is TABLED for later.

## Boundary rule (USER-PROVIDED — do not violate)
**The metadata JSON's `videoParameters.sampleRate` records the rate of the
decoded `.tbc` data (the format sample rate). It is NOT the source RF
timebase that AAA actually uses for alignment of real data.**

Consequence: `--rf-video-sample-rate-hz` (the Audio Alignment dialog's "RF
Video Sample Rate" field) MUST remain a **user-provided** value. It must NOT
be auto-populated from the JSON `sampleRate`. A 4Fsc PAL capture whose
decoded .tbc rate happens to equal its source rate is a coincidence of a
no-resampling pipeline, not a rule — do not assume the JSON rate is the
source timebase.

This is why the "auto-read RF rate from JSON" dialog fix was REJECTED: it
would silently feed the decoded format rate as the source timebase and break
alignment on resampled captures.

## Environment
- Repo: /home/harry/tbc-tools (branch main)
- Build workspace: /tmp/tbc-appimage-build (AppDir, squashfs-root, release/)
- Display: :0 (X11 reachable)
- Native nix build: /home/harry/tbc-tools/result -> /nix/store/brj5f1krn83a0v6rf1j3kbgichn8d9ny-tbc-tools-3.2.8 (Qt 6.8.3, working GUI)
- Proven AAA AppImage: /tmp/tbc-appimage-build/squashfs-root/usr/bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage

## Real target data (user-provided)
/media/harry/20TB HDD1/Hugo_UK_2025/VHS_Tape_01
- JSON: ...-video.tbc.json (PAL, JSON sampleRate=177344475/4Fsc decoded rate, 125500 fields, ~2h)
- Linear: ...-linear.flac (46875 Hz, 2ch, 951 MB)
- HiFi:   ...-hifi.flac   (10000 Hz, 1ch, 22 GB)  -- full align slow; deferred

NOTE: JSON sampleRate 17734475 is the DECODED .tbc rate. The source RF
timebase for the dialog's RF field is USER-PROVIDED (see boundary rule).

## Commands run and results
1. verify_linux_bundle.sh x86-appimage (local AppImage) -> FAILED at AAA
   stream-align smoke: "ffmpeg could not synthesize input audio, exit=124"
   (CI verifier tabled per user.)
2. Root cause of smoke fixture bugs (TABLED, not applied):
   - Stray `"` in fixture JSON after fieldWidth:1135 -> invalid JSON.
   - ffmpeg synthesis hang flake under non-interactive context; `-nostdin
     </dev/null` fixes it; python3-generated s24le removes the ffmpeg
     dependency entirely.
3. Bundled AppImage GUI launch -> ABORTS: Qt xcb plugin ABI mismatch:
   `libqxcb.so: libQt6Core.so.6: version Qt_6.10 not found` (plugin pulled
   from system Qt 6.10, core is Nix Qt 6.8.3). Packaging issue, TABLED.
4. Native result/bin/ld-analyse launched with proven AAA AppImage on PATH
   (symlink /tmp/aaa-gui-link/bin/vhs-decode-aaa.AppImage). GUI ALIVE.
5. User confirmed in GUI: Tools -> Auto Audio Align... IS visible; dialog
   OPENS. Link shows up inside analyse. CONFIRMED.
6. Real data verified: JSON valid (python json.load); ffprobe linear
   46875Hz/2ch flac; ffprobe hifi 10000Hz/1ch flac; ld-analyse still alive.
7. Manual AAA stream-align on synthetic fixture (corrected JSON +
   `-nostdin </dev/null`) -> rc=0, 34560-byte aligned output. Proves AAA
   codepath produces non-empty output when plumbing is correct.

## Dialog RF rate behavior (confirmed from source)
- AudioAlignmentDialog does NOT auto-read JSON sampleRate; defaults to
  preset index 0 = 40,000,000 Hz. Presets: 40M / 20M / 16M / Custom.
- currentRfVideoSampleRateHz() returns the preset/custom value and passes
  it to AudioAlignmentUtil::runStreamAlign -> AAA --rf-video-sample-rate-hz.
- mainwindow on_actionAuto_Audio_Align_triggered sets JSON but does NOT set
  the RF rate. Correct per boundary rule (rate is user-provided).

## Source timebase (USER-PROVIDED)
VHS_Tape_01 source RF timebase = 40,000,000 Hz (cxadc fixed / resampled to
40 Msps). NOTE this differs from the JSON decoded .tbc rate (17,734,475 Hz,
4Fsc PAL) — confirms the boundary rule: do NOT derive RF rate from JSON.
Dialog default preset (40 MHz) was already correct; no change needed.

## Result — real Linear Align via GUI (VERIFIED on disk)
- User ran Tools -> Auto Audio Align... in native ld-analyse, RF=40MHz,
  JSON=...-video.tbc.json, linear input=...-linear.flac, hifi left empty.
- Output: ...-linear_aligned.flac, 764 MB (763,934,083 bytes), STABLE.
- ffprobe: flac, 48000 Hz, 2 channels, duration 4001.75 s (~66.7 min).
  AAA resampled 46875 Hz linear -> 48 kHz output, stereo preserved.
- ld-analyse stayed alive throughout (pid 3686946).
- CONCLUSION: AAA link shows up inside analyse AND is callable + usable on
  real user-provided data (not merely detectable). Confirmed by hard data
  AND user real-world confirmation: dialog status label showed
  "Alignment complete (success)". TEST COMPLETE.

## Next
- HiFi (22 GB) full align deferred (slow). Optional later run.
- CI verifier smoke fixes (stray quote in fixture JSON; ffmpeg -nostdin</dev/null
  or python3 s24le synthesis) TABLED per user.
- Bundled AppImage Qt xcb ABI mismatch (system Qt 6.10 plugin vs Nix Qt 6.8.3
  core) TABLED — packaging issue, not AAA.
