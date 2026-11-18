# Prompt Log — decode-orc → tbc-tools vectorscope advanced mode — 2026-11-14

Session: build decode-orc, verify its composite (measurement) vectorscope on
test CVBS, then port the engine into tbc-analyse: burst positions on the
standard scope + a composite Advanced mode. All work done on
tbc-tools `main` @ e10e6f9 (up to date with harrypm/tbc-tools at start).

Related: decode-orc-latest session log at
`/home/harry/decode-orc-latest/prompt_log_2026-11-14_vectorscope-port.md`.

## Part A — decode-orc-latest (source side, /home/harry/decode-orc-latest @ a76b81f9)

- `git pull` b4d53105 → a76b81f9 (1555 files, tags v2.0.0–v2.11.1).
- Repaired `docs-tech/` submodules (both dirs contained only a `.git` cloning
  the MAIN repo — user ran `rm -rf` both dirs + `git submodule update --init
  docs-tech`; both now checked out at the correct spec-repo SHAs).
- Nix build OK (`nix develop "path:..." --command cmake/ninja`,
  BUILD_UNIT_TESTS=OFF). `orc-gui` verified against videosynth-generated PAL +
  NTSC bars CVBS:
  - `build/videosynth-manual/pal_patterns/videosynth_pal_patterns.cvbs`
    (12 frames: 4x 75% bars, ramps, multiburst; generated via
    `videosynth --project orc-tests/videosynth/projects/decoders/pal_patterns.yaml
    --output-root ...`)
  - `build/videosynth-manual/ntsc_patterns/videosynth_ntsc_patterns.cvbs`
  - Minimal PAL project used: `build/videosynth-manual/pal_patterns_vectorscope.orcprj`
    (PAL_CVBS_Source only; `orc-cli nodes list` → green,
    STANDARD_STABLE_LOCKED, sequence continuous).
  - Note: machine's OpenGL probe fails → orc-gui runs its documented CPU
    render path.
- Engine read in full for the port:
  `orc/core/analysis/vectorscope/composite_vectorscope.{h,cpp}`,
  `orc/sdk/include/orc/stage/preview/orc_vectorscope.h`,
  `orc/gui/preview/vectorscope_geometry.h`,
  `orc/sdk/include/orc/stage/cvbs_signal_constants.h`.

## Part B — tbc-tools port (/home/harry/tbc-tools @ e10e6f9)

Pre-work state handling (user decision): theme-persist WIP stashed as
`stash@{0}` ("theme-persist WIP (stashed 2026-11-14 before vectorscope
advanced-mode port)" — tracked changes only; untracked AppImage/release
leftovers left on disk), `git pull` confirmed up to date.

### Phase 1 — Engine port (compiles + tested)
New files:
- `src/tbc-analyse/vectorscope_data.h` — data contract ported from decode-orc's
  `orc_vectorscope.h` (acquisition mode, sample class, line phase, sample
  window enums; `UVSample` with class/phase/line_number;
  `VectorscopeMeasurements`; `VectorscopeData`). Namespace `tbc::cvbs`.
- `src/tbc-analyse/composite_vectorscope.{h,cpp}` — faithful port of the
  engine with TBC adaptations: flat field-sequential U10 buffer with uniform
  line width (no CVBS-container bridge samples), per-field line counts,
  `CompositeScopeParameters` built from `TbcMetaData::VideoParameters`
  (`make_composite_scope_parameters`, -1 metadata falls back to system
  constants), `assemble_composite_frame()` (TBC u16 → U10 via
  `tbc_to_cvbs` + `resolve_blanking_16b`). SECAM/MESECAM rejected (no linear
  subcarrier).
- `src/tbc-analyse/testcompositevectorscope/` — 36-check deterministic unit
  test (synthetic PAL/NTSC frames, no file I/O), registered in root
  `CMakeLists.txt` under BUILD_TESTING.
- CMake: `composite_vectorscope.cpp/.h`, `vectorscope_data.h` added to
  `tbc-analyse_SOURCES`.

Test results:
- `testcompositevectorscope`: 36/36 checks. PAL: burst 21.43 IRE (100.0%),
  jitter 0.13°, V-switch split −0.0007°, chroma/burst 1.995 (synthesised 2.0),
  stride 1 under the 800k ceiling / stride 8 at 100k ceiling, 25,040 burst
  samples classified, phases 312/314 lines per swing parity. NTSC: 20.0 IRE,
  no V-switch phases. SECAM rejected. Params/assembly fallbacks verified
  (blanking 16384→256, white 54016→844; degenerate levels refused).
- Full suite: `ctest --test-dir build` → 38/38 pass (all pre-existing chroma/
  decode/regression tests included).

### Phase 2 — Standard scope
- Graticule burst boxes: PAL dual at 135°/225°, NTSC/PAL_M single at 180°
  (ITU-R BT.470-6 Table 2 item 2.16 / SMPTE 170M-2004 §8.4). Box radius =
  burst fraction × system U10 level range (PAL 126 px, NTSC 112 px). Drawn in
  `drawDecodeOrcStyleGraticule` for every graticule mode; SECAM/MESECAM skip.
- The measured-burst overlay that was initially added to the standard scope
  was REMOVED again after user feedback (white dots unwanted there); burst
  measurement display lives in the Advanced tab only.

### Phase 3 — Advanced tab (UI iteration history — user feedback driven)
1. First attempt added controls into the standard control panel → REJECTED:
   the dialog is a fixed 520x300 layout, the extra groups pushed the original
   options off-screen ("modes are missing options I wanted preserved").
2. Reworked to the oscilloscope dialog's Basic/Advanced `QTabWidget` pattern
   (`setupAdvancedScopeTab()`): original scope reparented to the Basic tab
   byte-faithful, composite scope on the Advanced tab; shared right frame
   swaps standard-only controls (Scope area group, Blend) vs the composite
   panel. `setAdvancedTabSelected()/isAdvancedTabSelected()` API like the
   line-scope's.
3. Composite panel still overflowed the frame → wrapped in a `QScrollArea`
   (`advancedScrollArea`) inserted at the Scope-area group's slot; frame
   minimum width widened for the panel.
4. "Point plot is a mess" → implemented the measurement-scope dwell-brightness
   law (ported concept from decode-orc): density mode accumulates per-class
   hit grids (picture field-1 yellow / field-2 cyan / burst white / blanking
   dim grey, each normalised on its own max) over the 1024² canvas and composes
   additively; brightness = (hits/max)^(1/kTraceGain), kTraceGain fixed at 5.
   Point mode = semi-transparent additive scatter (alpha 110, blanking 55) so
   long-dwell vectors build up and one-line transits stay dim.
5. Mode group (Point/Density) is shared and selectable on both tabs.
6. Line view / Field view radio stacks → `QComboBox` dropdowns (Active line /
   Whole line / Burst only; Active field / Whole field / Selected line(s) with
   start/end spinboxes, 1-based interlaced frame lines).
7. Gain slider + "Trace" group box REMOVED (user: "get rid of the sliders").
   Draw-trace-lines checkbox sits directly in the panel.
- mainwindow `updateVectorscopeDialogue()`: Advanced tab →
  `assemble_composite_frame` + `make_composite_scope_parameters` +
  `extract_composite_vectorscope` (options from `compositeOptions()`) →
  `showCompositeTrace`; Basic tab → original path unchanged.

### Domain/scale notes (for future work)
- Composite samples ±32767 over blanking→white; canvas mapping
  `compositeToPixels = levelRangeU10 / 32767` where levelRangeU10 =
  cvbs_white − cvbs_blanking (PAL 588, NTSC/PAL_M 560) — the graticule maps
  one U10 chroma unit to one pixel.
- Composite (measurement) graticule: PAL/PAL_M dual mirrored target sets
  (upper/lower case), NTSC/PAL_M targets ×0.925 (SMPTE 170M-2004 §10).
- Engine core preserved from decode-orc: boxcar⊛raised-cosine band-pass,
  burst-locked flywheel (±32-line straight-line fit, PAL swing unwrapping,
  per-field tracking), −U-axis rotation, V-switch classification,
  stride subsampling, instrument readouts.

### Phase 4 — Advanced panel layout fix (session 2, 2026-11-14)
User feedback addressed: "wasted dead space and still these boxes have extra
boxes around them".
- Diagnosed with an offscreen screenshot harness (scratch only, not in repo:
  tbc-analyse's own ninja compile/link lines with a substitute `main` that
  builds `VectorscopeDialog`, feeds synthetic `VectorscopeData`, and grabs
  Basic/Advanced at 520x300, 760x520, 1000x720).
- Root cause of dead space: the .ui's two 40 px *Expanding* vertical spacers
  (`verticalSpacer`, `verticalSpacer_2`) soaked up the spare height while the
  composite scroll area sat at its small size hint and scrolled.
- Fixes in `setupAdvancedScopeTab()` / `updateMeasurementReadout()`:
  - Line view / Field view / Measurements `QGroupBox` frames → flat bold
    caption labels (`addCaption` lambda); panel spacing 6 → 2 with 6 px gaps
    between sections; spinbox row margins 0, spacing 4, spinboxes stretch.
  - Scroll area: vertical `Expanding` + stretch 1 in the frame layout.
  - Spacers closed to 6 px Fixed on the Advanced tab, restored to the .ui's
    20x40 Expanding on the Basic tab (`QSpacerItem::changeSize` + invalidate).
  - Readouts one per line (`\n` join instead of wrapped `  ·  ` run);
    acquisition info "Whole line, stride 2 / Frame lines 23–622", hidden when
    there is no data.
- Basic tab verified pixel-identical layout (spacers/groups untouched; the
  Mode and Scope area group boxes are pre-existing HEAD code, left as is).
- At 1000x720 the panel fits with no scrolling; at the 520x300 minimum it
  still scrolls (expected — the column holds more than 300 px of controls).
- Build OK; `ctest --test-dir build` → 38/38 pass.
- Incident: decode-orc-latest's `.claude/settings.json` PostToolUse hook runs
  `clang-format -i` on any C++ file Claude edits — including tbc-tools files,
  which have no `.clang-format`, so it reformatted vectorscopedialog.cpp to
  2-space LLVM style. Reverted by restoring the file from
  `RESTOREPOINT_VECTORSCOPE_ADVANCED_TAB_20261114.zip` and replaying the edits
  by script. When working on tbc-tools from a decode-orc session, edit C++ by
  script or add a tbc-tools `.clang-format`/scope the hook.

### Phase 5 — Mode as a dropdown (user: "mode elements need to be a drop down")
- Shared Mode control (both tabs) changed from a framed `QGroupBox` of
  Point plot / Density plot radio buttons to a plain "Mode:" label +
  `renderModeComboBox` (index 0 point, 1 density), in a margin-less container
  `renderModePanel` (was `renderModeGroupBox`). Slot
  `on_renderModeButtonGroup_buttonClicked` → `renderModeComboBoxChanged(int)`.
- `setAdvancedRenderModeSelected()` / `isAdvancedRenderModeSelected()` keep
  their API (mainwindow export presets unaffected); both render paths read
  `isAdvancedRenderModeSelected()`.
- Screenshots: Advanced fits without scrolling at 1000x720; Basic at 760x520
  shows Mode dropdown above the (unchanged) Scope area group.
- `ctest --test-dir build` → 38/38 pass.

### Phase 6 — Beam-trace rendering (2026-11-15; user: "it should show lines not blobs")
- Diagnosis on `test-data/pal/kagemusha-leadout-cbar.tbc` (real-fixture
  harness: TbcSource load → same assemble/params/extract path as mainwindow):
  engine numbers are honest — per-line burst phase residual 6.9–9.5° rms,
  white (lag-1 corr ≈ 0); bar chroma line-to-line sd 6–9°, burst/chroma
  correlation only ~0.2, so per-line burst lock would ADD noise (rejected).
  Clean fixtures (jason-testpattern, ve-snw-cut, issue176) give 1.1–1.7°
  jitter and tight on-target vectors.
- Renderer (`getCompositeTraceImage`) rewritten: both modes accumulate dwell
  into per-class float grids; with "Draw trace lines" (now default ON) each
  sample period is rasterised as the beam path from the previous sample of
  the same line/field, its dwell spread along the segment (fast transits dim,
  vectors bright). Point plot keeps the old brightness (110/255 per dwell,
  blanking half, saturating); density plot keeps (dwell/max)^(1/5). Old flat
  grey line overlay removed.
- Trial (reverted): kChromaBandwidthHz 1.18 MHz → 500 kHz tightens clusters
  and makes transits clean lines on noisy discs, but changes measurements —
  left for user decision.
- `ctest` 38/38.

### Phase 7 — Intensity, line averaging, Full-frame default (2026-11-15)
User: "I need an intensity slider for both modes"; "they still show as
blobs over the clean view of the basic scope"; "now the burst plots are
added make full-frame the stock mode".
- Intensity slider (`traceIntensitySlider`, 1..10, default 2) under the trace
  checkbox; render-only (reuses `traceLinesCheckBoxClicked` re-render).
  Density: exponent 1/intensity. Point: brightness per dwell
  (110/255)·2^((i−5)/2) — 5 equals the old fixed look.
- Engine option `CompositeVectorscopeOptions::vertical_average` (default
  false): display U/V = [1,2,1]/4 over the line and same-field neighbours with
  the same V-switch state (±2 field lines PAL/PAL_M, ±1 NTSC), each rotated by
  its own reference; neighbours need burst present + matching line phase.
  Raw values still feed chroma/burst; burst survey/jitter untouched.
  Dialog checkbox "Line averaging" (default ON) → `compositeOptions()`;
  toggling re-acquires (`scopeChanged`).
- Finding: Advanced clusters were the same size as Basic's; the "blob" look
  was additive saturation at the old fixed intensity. Intensity 1–3 + line
  averaging gives compact vectors with faint transitions.
- Basic Scope area default → Full frame (also the fallback when leaving
  Custom). NOTE: in the offscreen harness the Basic plot showed no burst in
  Full frame (identical to Active video) — not verified in the live app yet.
- Tests: testcompositevectorscope 42 checks (+6: averaging keeps sample
  count, measurements identical, static vectors within 0.5% — PAL worst 21.2,
  NTSC 0); ctest 38/38.
- Harness gotcha: recompile the harness object after header member changes
  (stale object → segfault in compositeOptions()).

### Phase 8 — Basic-tab intensity slider (2026-11-15; user: "add intensity slider to the basic page")
- `basicIntensityPanel` ("Intensity:" + `basicIntensitySlider`, 1..10,
  default 5 = original look) inserted under Mode in the shared frame; hidden
  on the Advanced tab (which keeps its own slider). Change → `scopeChanged`.
- `getTraceImage`: scale = 2^((i−5)/1.5). Density alpha = 14·scale (1..255).
  Point: i<5 translucent additive (alpha 255·scale), i>5 additive 2 px
  points; i=5 byte-identical to before (opaque 1 px, RGB888 unless blending).
  Image is ARGB32 whenever drawing is additive.
- Verified on jason-testpattern at 2/5/8 in both modes; ctest 38/38.

## Current state / OUTSTANDING (next session picks up here)
- Panel layout fix done — awaiting user confirmation in the real app.
- Docs pending: `src/tbc-analyse/README.md` (scopes menu description) +
  `docs/Tools/tbc-analyse.md` (Vectorscope section: Basic/Advanced tabs, burst
  boxes, composite acquisition + readouts, dropdowns).
- GitHub Actions validation pending (repo hard rule) — needs a push, only
  with user go-ahead; nothing committed yet.
- Theme-persist WIP still in `git stash` (`stash@{0}`).

## Files changed (uncommitted, on top of e10e6f9)
- Modified: `CMakeLists.txt`, `src/tbc-analyse/CMakeLists.txt`,
  `src/tbc-analyse/mainwindow.cpp`,
  `src/tbc-analyse/vectorscopedialog.{h,cpp}`
- New: `src/tbc-analyse/composite_vectorscope.{h,cpp}`,
  `src/tbc-analyse/vectorscope_data.h`,
  `src/tbc-analyse/testcompositevectorscope/{CMakeLists.txt,testcompositevectorscope.cpp}`

## Key commands
```
# build + test (tbc-tools)
nix develop "path:/home/harry/tbc-tools" --command ninja -C /home/harry/tbc-tools/build tbc-analyse
nix develop "path:/home/harry/tbc-tools" --command ctest --test-dir /home/harry/tbc-tools/build

# run on PAL bars fixture
nix develop "path:/home/harry/tbc-tools" --command bash -c \
  'setsid nohup .../build/bin/tbc-analyse .../test-data/pal/kagemusha-leadout-cbar.tbc ... &'

# engine unit test only
ctest --test-dir build -R testcompositevectorscope --output-on-failure
```

## Restore point
`RESTOREPOINT_VECTORSCOPE_ADVANCED_TAB_20261114.zip` (repo root) — this log +
all changed/new files above, per repo convention.


## Phase 9 - Playback rate fix (branch fix/tbc-analyse-playback-rate, uncommitted)
- Pacing: playback ticks scheduled against a QElapsedTimer deadline (resync if >2 intervals late) instead of "interval after render finished".
- Removed playbackUseFastMode (halved interval in mono = ~2x speed). SECAM/MESECAM now 25 fps. Field view steps every field (was +2).
- Measured mono: PAL 24.91 fps / 49.99 fields/s, NTSC 29.95 fps / 59.99 fields/s.

## Phase 10 - Colour playback decode-ahead (same branch)
- Profiled: decode 53-66 ms (PAL Transform 2D), 51-61 ms (NTSC 2D comb), 128 ms SECAM; RGB conversion 8-13 ms; field load 0.2 ms.
- TbcSource: new ChromaDecoders struct (pal/secam/secam-predemod/comb/mono + routing) with decode(); TbcSource keeps one set for the displayed frame and, while playing, one per worker (QThreadPool, idealThreadCount-1 capped at 6).
- Decoders configured (FFTW planning) and TBC fields read on the main thread; only decode() runs on workers. prefetchGeneration invalidates results on any setting change (invalidateImageCache except frame moves, configureChromaDecoder, setCombine). NN Transform 3D excluded (shared ORT session, already internally threaded).
- MainWindow::setPlaybackRunning -> tbcSource.setPlaybackPrefetch().
- Verified decode-ahead frames bit-identical to serial decode (PAL 22/22, NTSC 27/27, SECAM 8/8). Throughput 14 ms/frame. Colour playback steady state: PAL ~41 ms/frame (24.4 fps), NTSC ~33.7 ms (29.7 fps), was ~10 fps. Peak RSS 344 MB colour vs 150 MB mono. ctest 37/37.

## Phase 11 - VBI defaults for tape sources (same working tree)
- TbcMetaData::VideoParameters::isTapeSource(): tapeFormat non-empty and not LD/LaserDisc.
- tbc-process-vbi CLI: tape sources default VBI core + NTSC-specific off; new --vbi-core / --ntsc force on; --no-* still wins. Verified on MESECAM VHS test file and NTSC LD-style file.
- tbc-analyse Process VBI dialog: tape sources open with both unticked + note; LD preferences in config not overwritten; ticking passes the force flags.
- tbc-process-vbi GUI: reads metadata when input changes, sets defaults, passes force flags when ticked. resolveDefaultMetadataFilename moved out of main.cpp's anonymous namespace.
- Limitation: older vhs-decode captures with no tapeFormat look like LD.

## Phase 12 - CUDA (nnTransform3D) plugin detection
- Root cause on this machine: ~/.config/tbc-analyse.ini [cudaPlugin] holds the real record in lower-case keys (installedversion/installpath/releasetag) while the camelCase keys the code reads are empty -> Plugin Manager showed "Not installed" although the decoder loads the plugin (verified: tbc-chroma-decoder -f nntransform3d -> ONNX provider CUDA on GTX 1070 Ti).
- Configuration falls back to lower-case keys; CudaPluginManager::detectedInstallDirectory() treats provider lib present in default dir as installed (version "unknown") in the dialog/isInstalled.
- Verified via harness against the real ini (read back 1.0.0 / path / tag; ini untouched).

## Phase 13 - Teletext (started)
- --teletext runs end to end (OpenCL deconvolve, squash, HTML) on ggv-mb-1khz and kagemusha; both produce 0 packets (no teletext on these samples). Needs a teletext-bearing capture for a real inspection.

## Phase 13b - Teletext on Tape 06 (VHS PAL SP, 512,716 fields)
- 1,000-field segment (field 30,000+): full tbc-process-vbi --teletext path, OpenCL via cuda-lite plugin -> 390 HTML pages, 7 magazines (Channel 4 Teletext / Yorkshire, Jan 27 ~2pm). ~11.5 of 16 teletext lines per field yield packets.
- Character errors (TELETE8T, Gre8hound, p for 0): squash runs with --min-duplicates 1 (vhs-teletext default 3), so pages come from one noisy copy. 20 s too short to compare thresholds (d=2: 572 packets, d=3: 187).
- Root cause of the empty full-tape HTML folder (11:10 run): deconvolution was killed after a fixed 180 s (TELETEXT_DECONV_TIMEOUT_SEC default), both GPU attempt and CPU fallback.
- vhs-teletext --start/--limit count useful lines (16 per field), not tbc lines.

## Phase 14 - VBI processing never stops early
- User: "When a VBI processing option is enabled it should not ever stop till end of file is reached".
- teletextintegration.cpp: deconvolution has no timeout by default; TELETEXT_DECONV_TIMEOUT_SEC opts in to a no-progress limit (restarts whenever the raw .t42 grows). Audited: VBI DecoderPool runs to lastFieldNumber; squash/HTML have no timeout; tbc-analyse runExternalToolWithProgress and tbc-process-vbi GUI runProcessStep only kill on Cancel.
- Teletext tape profile auto-selected from metadata tapeFormat when --teletext-tape-format isn't given (BETA* -> betamax, *2X4* -> grundig_2x4, else vhs).

## Phase 15 - Disk full / Nix GC
- / hit 0 bytes free mid-session (2.6 GB free before; my scratch use was <2 GB and removed). User asked to purge old nix store data: nix-collect-garbage -d -> 1006 GB free.
- GC removed the dev shell closure; rebuilt via nix develop --profile /home/harry/tbc-tools/.nix-devshell-profile (GC root; excluded in .git/info/exclude).

## Phase 16 - Audio Monitor checkboxes (Export tab)
- exportdialog.ui: audio1..4MonitorCheckBox after each Browse button.
- ExportDialog::startAudioMonitor(seconds)/stopAudioMonitor(): ffmpeg decodes + amix the ticked tracks (raw .pcm as s16le 44.1k stereo, like the export trim) -> nut over pipe -> ffplay -nodisp -autoexit. resolveFfmpegPath(toolName) finds ffplay like ffmpeg. Toggling Monitor while playing restarts at the current position.
- MainWindow::setPlaybackRunning starts at (frame-1)/fps or (field-1)/(2*fps), stops on stop/end.
- CI: Linux AppImage + release, Windows, macOS bundle ffplay when the ffmpeg build has it (optional). ci/check_ci_contracts.py passed.
- Verified: harness (start -ss 0.16 at frame 5, untick stops, retick restarts at 0.47 s, stop leaves no processes); pipe plays 1 s of audio in 0.999 s. ctest 37/37.
- Not done: seeking during playback doesn't move the audio.

## Phase 17 - Teletext 5-minute verification + accuracy study (Tape 06)
- 15,000-field segment through tbc-process-vbi --teletext: deconvolution 4m22s (> old 180 s cap), completed, 442 HTML pages. Log confirms tape defaults (VBI core/NTSC off) and vhs profile from metadata.
- Squash threshold: d=1/2/3/5 -> 442/404/337/202 pages; page 100 text identical at d=1 and d=3 -> errors are systematic, not noise.
- Raw bytes: wrong chars all pass odd parity (e.g. '0' 0xB0 decoded as 'p' 0x70 = top two bits swapped); at header pos 15 wrong 'p' outnumbers '0' 24:18, so majority voting picks the wrong value. Deconvolver output has 0.00% parity failures -> it always snaps to a parity-valid byte.
- Profile sweep, 6,000 fields, all magazine headers ("Teletext"/"Jan27" chars): vhs 68.5%, betamax 73.1%, grundig_2x4 73.9%; OpenCL == CPU. Profile choice helps ~5 points only.
- Conclusion: the vhs-teletext pattern profiles don't fit vhs-decode TBC luma well; a real fix likely needs patterns trained on vhs-decode output (open question). min-duplicates default left at 1 (raising it drops pages without fixing these errors).

## Phase 18 - Audio sync + reset on new files
- User: playback rate off for audio sync; must be exact 29.97/25 "in-spec"; new TBC or audio file breaks audio -> must reset internally on every load.
- Rates were already exact (25 / 30000/1001); the drift came from stepping one frame per tick and resetting the schedule after late frames. Playback now follows the wall clock: playbackStartNumber + elapsed/interval; next tick due at (shown - start + 1) * interval; late renders skip frames instead of shifting the timeline (playbackNextDueMs removed).
- Measured (colour on): PAL 25.00000 fps / 50 fields/s, NTSC 29.97003 / 59.94006; last frame 22-28 ms after exact due time (one render, non-accumulating); start-up skips 3-11 frames while decoders initialise.
- ExportDialog::setSource stops/resets the monitor; changing any audio track file (or Monitor tick) while playing restarts at the current position with the new files. on_finishedLoading/loadTbcFile already stop playback.
- Harness: swap audio mid-play -> new file used; load second TBC -> monitor reset, play restarts audio; end of clip stops audio; no leftover ffmpeg/ffplay.

## Phase 19 - Advanced scope gaps vs decode-orc (2026-11-16)
- User: "stuff is missing from the advanced scope view". Merge check: Advanced tab code on main intact (338061d). Compared decode-orc's vectorscope_dialog.cpp feature set with the port.
- Added: Graticule "Both" (75% + 100% target sets and zones, labels on the outer set; drawDecodeOrcStyleGraticule takes the list of bar percentages, graticuleBarPercents()); "Burst"/"burst" labels beside the burst target boxes (PAL +V/-V, NTSC/PAL-M one), 40 px offset like decode-orc; "No chroma present" overlay on the Advanced plot when all samples are within 1000 of the centre (decode-orc's threshold).
- Already equivalent: Colorize = Multi-colour, Gain = Intensity, "No burst lock" = "No burst reference recovered", line/field view options, start/end line.
- Verified by render on kagemusha-leadout-cbar (Both, labels, no-chroma via scaled samples). ctest 38/38.

## Phase 20 - Advanced scope empty on Y/C (separate chroma) captures
- User's live window (NTSC VHS Tape_03, vhs-decode .tbc + _chroma.tbc): graticule present but trace collapsed at the centre, "No burst reference recovered".
- Cause: updateVectorscopeDialogue fed the composite engine firstFieldComposite only, which for BOTH_SOURCES is the luma TBC (no burst/chroma). The chroma fields were already in FieldTimingData (firstFieldChroma/secondFieldChroma).
- Fix: rebuild composite as luma + chroma - 32767 (as the decoder's combine mode) when chroma fields are present.
- Verified on Tape_03 frame 1000: before valid=0; after burst lock valid, 18.22 IRE (91.1%), 11.03 deg jitter, 484 lines; burst trace sits in the Burst box.
- Restore point vs commit check: 338061d contains everything in the restore point (more lines); graticule code identical. NTSC draws one target set (as decode-orc); PAL's mirrored second set is V-switch only.

## Phase 21 - Advanced scope matched to decode-orc's look; Monitor ticks persist (2026-11-16)
- User compared with decode-orc's scope screenshot (PAL test signal, Colorize, Gain 3, second field, active line).
- Point plot now uses decode-orc's composite renderer 1:1: hit + transit (Bresenham) counts, Gaussian beam spot sigma = canvas/410 (radius <= 15), dwell anchor = min(8 samples/line x lines / stride x spot0^2, half-dwell histogram point), brightness = min(1, dwell x gain/anchor + transit x 0.04 x gain x stride / (lines x spot0^2)), graticule underneath with the trace added, Colorize = BT.601 inverse at Y=0.5 normalised to the max component, plain trace green. Density mode keeps the earlier law. Gain slider default 3.
- Composite graticule strokes as decode-orc: 4 px outer circle, 2 px axes (Basic tab unchanged).
- decode-orc's title line above the Advanced plot: "Frame N - composite - X samples (lines a-b, window[, 1 sample in n]) - fields", "No vectorscope data" when empty.
- Audio Monitor ticks persist: [audioMonitor] tracks = "0101"-style string; ExportDialog::setAudioMonitorTracks/audioMonitorTracksChanged, saved by MainWindow. Verified across three launches with an isolated config (0000 -> 0101 -> 0001).
- ctest 38/38, CI contract checks passed.

## Phase 22 - Advanced graticule: same boxes on NTSC as PAL; composite target scale
- User (S-Video VHS NTSC Tape_03): "It is missing boxes", "PAL/NTSC should not be diff for the extra boxes for the vectorscope".
- The mirrored (-V, lower-case) target set is now drawn in the Advanced view for every system, so NTSC shows the same 12 boxes as PAL (decode-orc draws it for PAL/PAL-M only; on NTSC it is a reference).
- Bug fix: Advanced targets were scaled from the metadata IRE range while samples use the system's blanking-to-white range (decode-orc uses the latter for both). NTSC metadata range includes setup: 626 vs 560 -> targets ~12% too far out. PAL 593 vs 588. Composite targets now use cvbs_white - cvbs_blanking.
- Verified with the app's own update path on Tape_03 (NTSC Y/C): 12 colour boxes + Burst, burst locked 18.22 IRE; Tape_06 (PAL Y/C) locks (653,760 samples, valid). ctest 38/38.

## Phase 24 - Revert panel change; decode-orc graticule geometry; Trace box; title line removed
- User: panel layout was already settled (reverted 4f0ba50); "remove added text line from top"; graticule present but "off slightly"; "add a trace control box".
- Geometry now decode-orc's: full scale (+/-32768) at a 496 px radius (1024 canvas, 16 px padding) for samples, colour targets (U x radius, NTSC x0.925), burst boxes (fraction x radius), circle and axes. Checked against the user's decode-orc screenshot: R box 0.476 vs 0.48 of radius, burst 0.215 vs 0.217.
- Title line removed. Trace group box (Draw trace lines, Line averaging, Gain + value) added; rest of the panel unchanged. ctest 38/38.

## Phase 25 - Plain captions, unboxed trace controls, Blend colours on Advanced
- User: "Text labels should be de-bolded, and debox the trace line controls to save some space"; "add blend colours to the advanced panel too".
- Advanced captions (Line view, Field view, Measurements) plain; Trace group box removed (Draw trace lines, Line averaging, Gain row sit in the panel); panel now fits without scrolling at 720 px.
- Blend colors shown on the Advanced tab. Point plot counts hits/transits per field; Blend tints field 1 yellow, field 2 cyan, added; a single selected field takes its colour; otherwise decode-orc green; Multi-colour still overrides. Anchor from both fields together.
- ctest 38/38.

## Phase 26 - LaserDisc VBI modes back on by default
- User: "re-enable the ld vbi modes by stock".
- Removed the tape-source defaulting (Phase 11): tbc-process-vbi no longer turns VBI core / NTSC-specific off for tapes; --vbi-core / --ntsc force flags removed; tbc-analyse Process VBI dialog and the tbc-process-vbi GUI no longer untick them or show the tape note. --no-vbi-core / --no-ntsc unchanged. isTapeSource() kept for the teletext tape profile; resolveDefaultMetadataFilename file-local again.
- Verified on the MESECAM VHS sample: all decoders run, no tape message. ctest 38/38, CI contract checks passed.

## Phase 27 - Heavy dropout correction by default for tape sources
- User: "set dropout to heavy for tape sources for tbc-video-export dropout correct and inside the GUI".
- tbc-video-export had no --overcorrect, so the GUI's Heavy fell back to basic. Added --overcorrect / --no-overcorrect (argparse BooleanOptionalAction, default auto): auto = on when metadata tapeFormat is set (not LD/LaserDisc), passed to tbc-dropout-correct for luma and chroma. TBCJsonHelper.is_tape_source + test (11/11 in test_tbc_json.py).
- Export tab: Dropout mode defaults to Heavy for tape sources and Basic for LaserDisc on each new source; Basic passes --no-overcorrect so tapes can still export basic.
- Verified: dry-run matrix (VHS MESECAM auto/--overcorrect -> 2 dropout-correct runs with --overcorrect, --no-overcorrect -> 0; Kagemusha LD auto -> 0, --overcorrect -> 1); GUI default heavy for both VHS tapes, basic for Kagemusha. ctest 38/38; tbc-video-export pytest 140 passed both before and after (104 pre-existing errors: pytest-mock missing; test_output.py needs pymediainfo).

## Phase 28 - Export speed (2026-11-17)
- User: keep FFV1 slices (bigger files); add in/out range to dropout correct; try thread balance, then pipe buffers; exports slow down after a few hours; add export completion time.
- Range: tbc-dropout-correct -s/-l; tbc-chroma-decoder --input-start-frame (SourceVideo::setStreamStartField); tbc-video-export runs dropout correction from 8 frames before start to 8 after end. Byte-identical decode (mono, NTSC 2D/3D); 300-frame export from frame 12000 in 6.4 s. (commit 2ec8a86)
- Threads: 1500-frame Tape_03 export, all 18 threads 79.3 fps vs best split 85.3 fps (noise level); pipeline is capped by FFV1 4-slice encode (79 fps alone). Pipe buffers not pursued: pipes aren't the limit here.
- Disk: Tape_06 (PAL S-Video, 2 x 364 GB on a 20 TB NTFS/ntfs-3g HDD). One file 245 MB/s; luma+chroma read concurrently 44-63 MB/s (15-22 fps); one reader alternating 64 MB chunks 232 MB/s (82 fps). Fix: SourceVideo::setChunkedReads - 64 MB blocks read under a per-disk QLockFile (temp dir) so concurrent tbc-tools readers take turns; on in single-source tbc-dropout-correct and tbc-chroma-decoder file input. Two dropout-correct processes: ~60 fps pair rate (was 15-22). Export at frame 150000 decoded MD5 identical to baseline; 1500-frame export 23.1 -> 32.7 fps (short runs dominated by ~30 s/process JSON metadata load/write).
- GUI: Export tab kept the whole tbc-video-export output (progress redraws) in memory for the run; now keeps the last 512k chars per stream (main + parallel proxy).
- Completion time: tbc-video-export footer "Remaining: h:mm:ss  Finishes: HH:MM" from ffmpeg frames vs total over a 2-minute window; Export tab status line "Export: X remaining, finishes at HH:MM".
- ctest 38/38; pytest 141 passed (104 pre-existing pytest-mock errors); CI contract checks passed.

## Phase 29 - Export segments, stage 1 (metadata + tbc-video-export)
- User: "segment 1 start/end, segment 2 start/end ..." - an ordered list of segments exported and joined into one uniform file (gaps = cuts), each with its own framing adjustment; recorded in the final metadata.
- Metadata: VideoParameters.userSegmentsJson ("UserSegmentsJson" in JSON, user_segments_json column in .db, added to existing .db files automatically). Format: [{"start", "end", "offsetX", "offsetY"}], offsets in samples / frame lines from the global framing. Round trip JSON -> .db -> JSON verified via tbc-dropout-correct metadata write.
- tbc-chroma-decoder: --active-video-start/--active-video-end overrides.
- tbc-video-export: reads UserSegmentsJson (--ignore-segments to skip); per segment copies opts (range, framing, temp output) and runs the pipeline; joins with ffmpeg concat stream copy, attaches the json once, tags tbc_segments. Offsets: inside the picture -> decoder window moved; past the edge -> global window + ffmpeg crop/pad shift (black fill). Vertical offsets must be even (odd swaps field order). Output size identical by construction.
- Verified on Tape_03 (NTSC Y/C) 3 segments (1000-1299 0/0, 3000-3149 +8/+4, 6000-6099 -6/-2): one 760x488 file, 550 frames, tag + attachment, temp dir removed; each segment framemd5-identical to a standalone reference export with the equivalent explicit framing. ctest 38/38, pytest 143 passed, CI contracts passed.

## Phase 30 - Export segments, stage 2 (Edit tab)
- User: "Add an edit tab for now rather than making the Export tab crowded" - "Viewer / Edit / Export".
- New EditSegmentsWidget (editsegmentswidget.cpp/.h) as the Edit tab between Viewer and Export: numbered rows (Segment 1, 2, ...) with Start/End/X offset/Y offset spin boxes (Y in steps of 2, odd values corrected), Add segment from in/out (ExportDialog::inPoint/outPoint), Remove, Move up/down, Go to start, summary line. Changes write VideoParameters.userSegmentsJson via tbcSource.setVideoParameters (Save Metadata enabled); loaded from metadata on source load, cleared on unload.
- Verified in a real MainWindow on Tape_03: tabs Viewer/Edit/Export; adds, offsets (5 -> 4), move up, go to start (frame 3000), reload all correct. Export button with 2 segments -> one 150-frame 720x486 file with tbc_segments tag; status showed "Joining 2 segments", "Export complete." (Audio: the Export tab passes untrimmed tracks; tbc-video-export trims per segment.)
- Docs: tbc-analyse.md "Edit tab (export segments)". ctest 38/38, CI contract checks passed.

## Phase 31 - Edit tab: picture + segment timeline
- User: needs to see the picture in Edit and make segments / visual adjustments like the Viewer, with a horizontal timeline showing each segment (not a table).
- EditSegmentsWidget rewritten: viewer slot (the Viewer's scroll area moves between Viewer and Edit on tab switch), SegmentTimeline (numbered blocks across the source, cut gaps dark, pending-start marker, playhead; click/drag seeks), Segment start / Segment end at the current frame, Remove, Start/End/X/Y fields for the segment under the playhead; segments kept in timeline (= export) order.
- MainWindow: isViewerTabActive includes Edit; getActiveVideoRects applies the playhead segment's offsets in Edit; the export-boundary edge drag inside a segment sets that segment's offsets (left/right -> X, top -> Y, even); boundary always shown inside a segment in Edit; timeline playhead follows updateImage. Removed the unused ExportDialog in/out getters.
- Verified on Tape_03: picture moves to Edit and back; segments 1000-1299 and 3000-3149 made via start/end; offsets 8/5 -> box (134,40)->(142,44), 5 -> 4; box back to global between segments and on the Viewer tab. ctest 38/38.

## Phase 32 - Segment editor moved to branch tbc-seg-edit (2026-11-18)
- User: commit/push live playback + monitor features with bug fixes; put the editor mode on a branch "tbc-seg-edit".
- Branch tbc-seg-edit = main @ 81ce461 + 045e964 (WIP: X/Y fields removed in favour of dragging / Video Parameters dialog editing the segment under the playhead; Start/End re-sort fix). Pending there: decide on X/Y fields; hybrid active-area preview inside a segment follows the global framing.
- main: single revert of 7e66058, 0424341, 81ce461 (segments metadata field, Edit tab, decoder --active-video-start/end, tbc-video-export segments). Playback, audio monitor, export speed/ETA/dropout work stays on main.
