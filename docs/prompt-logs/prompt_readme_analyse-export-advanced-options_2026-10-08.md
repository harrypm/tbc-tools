# Prompt README — tbc-analyse Export page cleanup + Advanced Options dialog (2026-10-08)

## User prompt
> "Lets clean up the export page inside analyse I want to remove 4 check boxes and add an advanced
> options sub-window/dialog like the VBI processing window, as it will make the UI less buggy when
> scaled smaller, also add a reverse field order export option and list anything else that is not
> directly exposed that could be added"

## Research performed (commands + sources read)
- Read `src/tbc-analyse/exportdialog.ui` (full): Export page layout. Options row (row 3,
  optionsLayout) holds: dropoutModeComboBox, dropoutFieldModeComboBox, 4 checkboxes
  (exportMetadataCheckBox "Include FFmpeg metadata + CC" [default on],
  disableMetadataEmbeddingCheckBox "Disable decode JSON attachment", letterboxCropCheckBox,
  forceAnamorphicCheckBox), FFV1 slices spinbox, ProRes variant, Web codec, AVC range, HEVC range
  combos — all in ONE horizontal row (the small-window scaling problem).
- Other checkboxes (bottom row, NOT part of the move): logProcessOutputCheckBox (--log-process-output),
  generateProxyCheckBox (drives proxy codec combo + stats split), exportProfileConfigCheckBox
  (drives external-profile load/eject row) — each has tightly-coupled inline controls.
- Read `vbiprocessingdialog.{h,cpp}` (the pattern to mirror): pure-code QDialog (no .ui), options
  struct (VbiProcessingOptions in configuration.h), constructed with current options + parent,
  exec() + selectedOptions(), persisted by MainWindow (getVbiProcessingOptions/setVbiProcessingOptions
  + writeConfiguration). Opened from mainwindow `on_actionProcess_VBI_triggered` with parent=this
  (MainWindow) → gets the centered-over-main-window Show filter for free.
- mainwindow.cpp:1515-1516: `exportDialog = new ExportDialog(this); ui->mainTabWidget->addTab(exportDialog,
  tr("Export"));` — the Export "page" is a tab; preference signals persisted via configuration
  (proxyGenerationPreferenceChanged / exportProfileConfigPreferenceChanged patterns at 1520-1531).
- exportdialog.cpp regions read: constructor combo/checkbox setup 1330-1559 (incl.
  letterbox/anamorphic defaults + toggled → updateProfileDependentControls);
  updateProfileDependentControls 2701-2814 (interlocks: letterbox only allowed with
  active_area framing; letterbox+anamorphic mutually exclusive; auto-uncheck on conflict);
  buildArguments 4893-5251 (checkbox → CLI wiring: --export-metadata, --no-attach-json,
  --letterbox, --force-anamorphic, --no-dropout-correct/--overcorrect/--dropout-interfield-correction/
  --intra/--innerfield/--interfield, --chroma-decoder/-gain/-phase/--luma-nr from videoParameters,
  ffll/lfll/ffrl/lfrl, --full-frame/--vbi, --ntsc-phase-comp, --transform-threshold,
  --d1/--append-video-filter derived, --input-tbc-json override).
- Read opts files (tbc-video-export CLI surface): opts_parser.py (general: --threads,
  --tbc-tools-appimage, --two-step, --video-system, --input-tbc-json, --overwrite, --dry-run,
  --config-file), opts_ldtools.py (decoder: -s/-l, --reverse [requires --no-dropout-correct],
  --output-padding, --vbi/--full-vertical/--letterbox mutually exclusive group, --full-frame,
  --standard/--d1, ffll/lfll/ffrl/lfrl, --chroma-decoder, --chroma-decoder-luma, --chroma-gain,
  --chroma-phase, --luma-nr, --transform-threshold(s); ntsc: --chroma-nr, --ntsc-phase-comp,
  --oftest; pal: --simple-pal; secam: --secam-first-line-is-red; dropout: --no-dropout-correct,
  --dropout-correct-threads, --dropout-interfield-correction; process-vbi: --process-vbi(+keep-going,
  +threads); process-efm (hidden); export-metadata: --export-metadata(+keep-going)),
  opts_ffmpeg.py (--ffmpeg-threads, --audio-track(-advanced), --metadata, --metadata-file,
  --field-order [default AUTO], --force-anamorphic, --force-black-level, --hwaccel-device,
  --no-attach-json, --thread-queue-size, --checksum), opts_profile.py (--profile, --list-profiles,
  --profile-container, --profile-add-filter, --append-video-filter, --append-other-filter, hwaccel/
  bitdepth/format/profile aliases).
- Read `exportarguments.{h,cpp}` + `testexportarguments/testexportarguments.cpp`: shared pure
  helpers (shouldDisableDropoutCorrection, isDefaultActiveAreaFraming, sanitizeOutputBasePath) +
  assert-based unit tests (ctest target testexportarguments).

## Key findings
- The 4 checkboxes to move are the Options-row ones (exportMetadata, disableMetadataEmbedding,
  letterboxCrop, forceAnamorphic); the other 3 checkboxes have tightly-coupled inline controls and
  are functional toggles, not options.
- --reverse requires --no-dropout-correct (CLI help) — the GUI must enforce that coupling.
- --field-order must NOT be exposed (hard rule: parity defaults AUTO, metadata-derived).
- New dialog gets centering for free if parented to the main window (top-level chain via window()).

## Status
Plan tool was dismissed by the user; proceeded directly with the documented interpretation
(4 Options-row checkboxes move; bottom-row checkboxes stay).

## Implementation (2026-10-08, all changes uncommitted)
- NEW advancedexportoptionsdialog.{h,cpp} (mirrors VbiProcessingDialog): code-built QDialog
  (no .ui), ExportAdvancedOptions checkboxes/spinboxes with tooltips, live letterbox↔anamorphic
  mutual exclusion (QSignalBlocker uncheck), letterbox disabled when framing not allowed,
  reverse-note label, Ok/Cancel, selectedOptions(); added to tbc-analyse CMakeLists.
- configuration.{h,cpp}: ExportAdvancedOptions struct (exportMetadata=true, disableMetadataEmbedding,
  letterboxCrop, forceAnamorphic, reverseFieldOrder, twoStep, outputPadding=0, threads=0) +
  get/setExportAdvancedOptions + Settings member + QSettings group "exportAdvancedOptions"
  (additive keys; defaults mirror CLI defaults; writeConfiguration/readConfiguration/setDefault).
- exportdialog.ui: the 4 Options-row checkboxes removed → advancedOptionsSummaryLabel +
  advancedOptionsButton (auto-connected slot). Row now: Dropout + Field combos + summary +
  Advanced button + spacer + profile-specific combos.
- exportdialog.{h,cpp}: advancedOptions member; setExportAdvancedOptions; advancedOptionsChanged
  signal; on_advancedOptionsButton_clicked (guards running processes; letterbox gate via
  ExportArguments::isDefaultActiveAreaFraming; dialog exec parented to dialogParentWidget(this)
  → main-window centered-over-parent filter; update member + summary + profile controls + emit);
  updateAdvancedOptionsSummary (non-default list + tooltip); updateProfileDependentControls
  rewritten tail (advanced button enable; reverse-aware dropout mode/field combo enables);
  setBusy (removed the 2 checkbox enables; advanced button enable; reverse handled via
  updateProfileDependentControls called at setBusy end); buildArguments rewired: the 4 options
  read from advancedOptions; --reverse appended inside the forced --no-dropout-correct branch
  (extended ExportArguments::shouldDisableDropoutCorrection with reverseFieldOrder);
  reverse capability error via unsupportedOptionError; --two-step/--output-padding/--threads
  appended capability-guarded (silent skip like --overcorrect) before the tail args.
- mainwindow.cpp: exportDialog->setExportAdvancedOptions(configuration.get...) + connect
  advancedOptionsChanged → persist (same pattern as proxyGenerationPreferenceChanged).
- exportarguments.{h,cpp} + testexportarguments.cpp: shouldDisableDropoutCorrection gained a
  defaulted reverseFieldOrder param (reverse forces disable); new test cases added.

## Validation (hard data)
- Stale-reference sweep: zero references to the 4 removed checkbox widget names in
  exportdialog.cpp (grep).
- cmake reconfigure (nix develop) OK (pre-existing ld-lds-converter rpath warning only);
  ninja build OK: 40/40 steps incl. MOC/UIC for the new dialog; bin/tbc-analyse linked.
- ctest targeted: video-export-runtime Passed (1.41s); testexportarguments Passed (0.01s).
- ctest full suite: 30/30 passed (61.49s).
- PENDING: user real-world GUI confirmation (see rules): run tbc-analyse, open Export tab,
  Advanced options... dialog opens centered over the main window; enable Reverse field order →
  dropout mode/field combos disable + summary shows "reverse field order"; export log
  "Command:" line carries --reverse --no-dropout-correct; Options row scales cleanly when the
  window is made small.
- Not committed; GitHub Actions verification required after push per AGENTS.md.
