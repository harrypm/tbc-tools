# Prompt README - AAA temp/source-dir, OS theme follow, chroma decoder revert fixes

Date: 2026-11-01
Repo: /home/harry/tbc-tools (main @ 932ba5a, clean tree, pushed)

## User request (input)

1. AAA running inside the target source directory - temp files must not be copied to the
   boot drive on any platform.
2. Theme settings should follow OS-level light/dark modes.
3. Config bug: setting the chroma decoder to PAL 2D defaults back to Transform 2D in the
   metadata sent to the decoder (user clarified: noticed after cancelling an export).

User confirmations on the plan:
1. Chroma decoder selection must follow absolute user config - no auto changes.
2. AAA target source directory = the data files source (metadata JSON directory).
3. Follow-OS theme default confirmed.

## Research findings (verified against code)

- AAA pipeline `AudioAlignmentUtil::runStreamAlign` (src/audio-align/audioalignmentutil.cpp):
  - Line ~1075: default QTemporaryDir -> system temp (boot drive on Linux /tmp, Windows %TEMP%).
  - Lines ~1125/1156/1190: runProcess called with empty working directory.
  - Lines ~266-285: AppImage launched via `env APPIMAGE_EXTRACT_AND_RUN=1 <appimage>`;
    extraction goes to $TMPDIR (boot drive).
- Theme: ThemedApplication (src/library/tbc/uistyle.h) pins stock dark palette,
  setDesktopSettingsAware(false), re-asserts pinned palette on ApplicationPaletteChange.
  Applied dark-by-default in 7 GUI mains. tbc-analyse Themes menu has Dark/Light only.
- Chroma revert root cause: ChromaDecoderConfigDialog::updateDialog() `!isInit` block
  (chromadecoderconfigdialog.cpp:281-308) force-resets chromaFilter/dimensions to defaults
  (single source -> transform3D, split -> transform2D). MainWindow::updateGuiLoaded() passes
  isInit=false on every load (mainwindow.cpp:2445-2449) -> reset + emit
  chromaDecoderConfigChanged -> handler (mainwindow.cpp:8494-8535) writes the defaulted name
  into in-memory VideoParameters.chromaDecoder -> that value feeds
  ExportDialog::buildArguments --chroma-decoder (exportdialog.cpp:5139-5150) and the export
  metadata snapshot. SECAM self-heals (transform2d maps to no SECAM name -> string kept);
  PAL does not (transform2d overwrites pal2d).

## Changes implemented

1. AAA (src/audio-align/audioalignmentutil.cpp/.h):
   - runProcess gains an optional child-environment override.
   - New resolveAlignmentWorkspaceDirectory(): JSON dir -> output dir fallback, real
     QTemporaryDir probe, NEVER system temp; workspace created as
     <base>/.tbc-analyse-temp/tbc-analyse-aaa-XXXXXX.
   - runStreamAlign: workspace + workingDirectory=<source dir> on ffmpeg/AAA/encode
     children + TMPDIR (TMP/TEMP on Windows) pinned to the workspace.
   - testaudioalignmentruntime.cpp: new --workspace-dir mode; runtime mode now copies
     fixtures to a private dir and asserts the workspace root lands next to the JSON and
     no new tbc-analyse-aaa-* appears in the system temp.
   - src/audio-align/CMakeLists.txt: aaa-workspace-dir ctest registered.
2. Theme (src/library/tbc/uistyle.h + 7 GUI mains + tbc-analyse Themes menu):
   - systemPrefersDarkTheme() (Qt 6.5+ colorScheme; Unknown/older -> dark).
   - ThemedApplication: follow-system mode (applyStockThemeFollowingSystem), no
     setColorScheme pin while following, re-resolve on ApplicationPaletteChange,
     colorSchemeChanged live tracking on Qt 6.8+.
   - All GUI mains default to follow-system; --light-theme/--force-dark-theme pin.
   - Themes menu: System action (default checked) above Dark/Light.
3. Chroma (src/tbc-analyse):
   - mainwindow.cpp updateGuiLoaded: isInit = metadata chromaDecoder non-empty (user
     config absolute; default-selection init only for metadata without a decoder).
   - chromadecoderconfigdialog: removed mainwindow.h include + parent-cast; new explicit
     setTbcSource(); MainWindow wires it after construction.
   - New headless test src/tbc-analyse/testchromaconfigdialog/ (offscreen) covering
     PAL 2D / NTSC 1D / SECAM preservation, default selection, emitted config.

## Commands run (append-only log)

- g++ -fsyntax-only (system Qt6 headers) on audioalignmentutil.cpp + testaudioalignmentruntime.cpp: clean.
- g++ -fsyntax-only uistyle.h check TU: clean.
- nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release: configured OK
  (pre-existing ld-lds-converter linuxbrew RPATH warning only).
- nix develop -c ninja -C build: 62/62 OK, tbc-analyse + tests linked.
- ctest run 1: 33/35 - aaa-workspace-dir failed (test fixtures were inside the nix
  devShell TMPDIR=/tmp/nix-shell.* so the 'never system temp' assertion tripped on the
  fixture placement itself); testchromaconfigdialog failed (isInit=false branch reads
  getVideoParameters() which throws on an unloaded TbcSource - app only ever runs it
  with a loaded source). Both were test-environment issues, not product failures;
  aaa-runtime PASSED with the new source-local workspace assertions.
- Fixed tests: workspace-dir fixtures now created under the executable dir (outside
  system temp); chroma test exercises isInit=false without an attached source.
- ctest run 2 (targeted): aaa-workspace-dir + testchromaconfigdialog both Passed.
- ctest run 3 (full): 35/35 Passed (59.9s).

Verification probes (hard data):

- Probe A - product child env/CWD injection: staged a fake vhs-decode-aaa.AppImage (shell
  script writing CWD/TMPDIR/EXTRACT to a marker) via TBC_TOOLS_APP_BIN_DIR and drove the
  REAL runStreamAlign pipeline (runtime test mode, ffprobe + ffmpeg real). Marker shows
  the AAA child ran with CWD=<metadata JSON dir> and
  TMPDIR=<JSON dir>/.tbc-analyse-temp/tbc-analyse-aaa-XXXXXX, APPIMAGE_EXTRACT_AND_RUN=1.
  Pipeline then failed at encode as expected (fake produced no aligned output).
- Probe B - real AppImage honors TMPDIR: vhs-decode-aaa.AppImage (105 MB) run with
  APPIMAGE_EXTRACT_AND_RUN=1 + TMPDIR=<scratch> extracted to
  <scratch>/appimage_extracted_71d89a... and exited 0. NOTE: the AppImage runtime does
  NOT clean up its extraction dir - with the old default TMPDIR that littered the boot
  drive per run; now the extraction lands inside the QTemporaryDir workspace which is
  removed recursively on teardown.
- Probe C - ffprobe of the real aaa-runtime output (build/test-artifacts/aaa-runtime/
  aligned.flac): flac, 48000 Hz, 2 channels, duration 0.943771 s (matches 10-frame PAL
  fixture).
- Theme probe (Qt 6.8.3 from the nix shell, this Mint/Cinnamon session):
  platform=xcb colorScheme=Dark; with setDesktopSettingsAware(false) (product setting)
  still Dark; offscreen platform reports Unknown -> follow-system falls back to stock
  dark as designed.
- python3 ci/check_ci_contracts.py: passed.

Iteration 2 (user: fix must apply to ALL chroma decoder modes):

- Audit of the full mode list found real holes beyond the originally tested ones:
  - ntsc3dnoadapt was not mapped by TbcSource::applyChromaSettingsFromMetadata at all
    (dimensions stayed 2), so a load would have rewritten it to ntsc2d - selection lost.
  - The name derivation could not express 3D-with-adaptive-off, yet adaptive only rides
    in the decoder name once -f is passed (tbc-chroma-decoder then ignores the metadata
    ntscAdaptive field), so ntsc3dnoadapt must be derived from dimensions==3 + !adaptive.
- Fixes: tbcsource.cpp maps ntsc3dnoadapt (dimensions=3, nn=false, adaptive=false);
  chromaDecoderNameFromConfig moved from mainwindow.cpp into chromadecoderconfigdialog.h
  (shared, testable) and now derives ntsc3dnoadapt; nntsc3d alias canonicalises to
  nntransform3d (same decoder, both names valid everywhere).
- testchromaconfigdialog extended to enumerate every mode (PAL: pal2d/transform2d/
  transform3d/mono; SECAM: secam/secam-predemod/mono; NTSC: ntsc1d/ntsc2d/ntsc3d/
  ntsc3dnoadapt/nntransform3d/nntsc3d/mono) asserting dialog-config preservation AND the
  derived metadata/export decoder name.
- IMPORTANT: rewrote the test's assert() calls as explicit verify() checks - the test
  target builds Release (-DNDEBUG) where assert() is a no-op, so assert-based checks
  (including the pre-existing testexportarguments) pass vacuously in this build config.
- Rebuild + full ctest: 35/35 Passed (61.25s). Direct testchromaconfigdialog run shows all
  four test functions executing live.
- Side observation from the direct run: the session passes QT_STYLE_OVERRIDE=Adwaita-Dark
  (unsupported by the nix Qt; the existing normalizeUnsupportedStyleOverrideToFusion()
  guard handles it) - also confirms the desktop session is currently dark-themed,
  consistent with the colorScheme=Dark probe.

Pending (cannot be verified without the user / a push):
- Real-GUI confirmation of the theme follow (OS light/dark flip while tbc-analyse runs).
- Real-GUI confirmation of the chroma fix (PAL 2D survives reload; export Command log
  shows --chroma-decoder pal2d).
- GitHub Actions parity (project rule) - requires a push.
