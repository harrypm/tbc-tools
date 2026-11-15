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
