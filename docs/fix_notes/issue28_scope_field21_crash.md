# Issue #28 fix note — ld-analyse Line Scope Field 2:1 access violation

Date: 2026-08-22
GitHub: https://github.com/harrypm/tbc-tools/issues/28

## Symptom
ld-analyse crashes with 0xc0000005 (access violation) when the Line Scope
window is open and the view mode is cycled to Field 2:1, with Y/C separate
sources (BOTH_SOURCES). Opening the scope *after* switching to 2:1 does not
crash; only the live update during the switch crashes.

## Root cause
Use-after-free of cached plot series pointers in `OscilloscopeDialog`.

`PlotWidget::showNoDataMessage()` (src/ld-analyse/plotwidget.cpp) calls
`clearSeries()` + `clearMarkers()`, which `delete` every series and marker the
plot widget owns. `OscilloscopeDialog` caches pointers to the series it creates
(`advancedCompositeSeries`, `advancedYSeries`, `advancedCSeries`) and the sample
marker (`advancedSampleMarker`) but never forgot them when the plot widget
deleted them.

Crash sequence during the view switch:
1. Scope open. Cycling to Field 2:1 passes through Field 1:1, where the
   scope's current Y resolves to the unused area -> `getScanLineData` returns
   empty -> `showTraceImage`/`updateAdvancedScope` call
   `advancedPlotWidget->showNoDataMessage(...)` -> the series are deleted;
   the dialog's cached pointers are left dangling.
2. Field 2:1 update returns valid data -> `updateAdvancedScope`:
   `if (!advancedCompositeSeries)` is false (dangling non-null) -> it skips
   recreation -> `advancedCompositeSeries->setVisible(...)` dereferences freed
   memory -> access violation.

## Fix
In `src/ld-analyse/oscilloscopedialog.cpp`, after every call to
`advancedPlotWidget->showNoDataMessage(...)` (in both `showTraceImage` and
`updateAdvancedScope` no-data early returns), null out the cached pointers:
`advancedCompositeSeries`, `advancedYSeries`, `advancedCSeries`,
`advancedSampleMarker`. The existing `if (!advancedXSeries)` recreate blocks
in `updateAdvancedScope` then recreate fresh series on the next valid update.

## Additional hardening (kept)
`TbcSource::getScanLineData()` (src/ld-analyse/tbcsource.cpp) now bounds-checks
the field-relative line (`lineNumber.field0()`) against the actual
field-data/chroma-field-data line count before indexing. This is independent
defensive hardening for truncated fields / stale coordinates and does not by
itself fix #28 (the crash was in the render path, not getScanLineData), but it
prevents a separate class of out-of-bounds reads.

## Verification
- Built: `cmake --build C:/Users/Harry/tbc-tools/build --config Release --target ld-analyse` (links cleanly).
- Diagnosed via a temporary file-based step logger (`ld_analyse_scope_crash.log`)
  which was removed after the root cause was found; the last logged step before
  the fault was inside `updateAdvancedScope` between the points loop and
  `setData`, confirming the dangling-pointer dereference.
- Pending user GUI confirmation that switching to Field 2:1 with the Line
  Scope open no longer crashes.

## Files changed
- src/ld-analyse/oscilloscopedialog.cpp (fix + logging removal)
- src/ld-analyse/tbcsource.cpp (logging removal; bounds-check hardening kept)

## Restore point
A zip of the fixed source files should be preserved alongside this note as a
go-back restore point (per project rule: when a fix is reported as fully fixed,
preserve the data).
