# Prompt log — ld-lds-converter: one-by-one input queueing + minimize button

Date: 2026-07-05
Working dir: /home/harry/tbc-tools

## User input (request)
"fix ld-lds-converter to allow for multiple added separate inputs (i.g 1 by 1
instead of all at the same time from one source directory) and add a minimise
button."

Pre-commands run by user before prompt: `cd '..'`, `cd '..'`, `cd 'tbc-tools'`.

## Problem found (from inspecting source)
- `src/ld-lds-converter/converterdialog.cpp` `setInputQueue()` **replaced**
  `queuedInputFiles` on every Browse/drop, so files could only be added all at
  once from one directory; adding from a second directory wiped the first set.
- The `inputLineEdit` `textChanged` handler also rebuilt the queue from the
  line-edit text, so typing wiped any queued files.
- The dialog is a `QDialog` with no minimize button (default window flags).

## Changes made
Files changed:
- `src/ld-lds-converter/converterdialog.h`
  - Added slots: `on_removeQueuedButton_clicked()`, `on_clearQueuedButton_clicked()`.
  - Added helpers: `appendToInputQueue(QStringList)`, `refreshOutputPreview(bool)`.
- `src/ld-lds-converter/converterdialog.ui`
  - `inputBrowseButton` text "Browse..." -> "Add..." + tooltip.
  - `inputLineEdit` placeholder -> "Type/paste an .lds path and press Enter to add".
  - Hint label rewritten to describe add-one-by-one + Remove/Clear.
  - Queued table `selectionMode` `NoSelection` -> `ExtendedSelection` (rows).
  - Added `removeQueuedButton` ("Remove selected") and `clearQueuedButton`
    ("Clear all") in a new `queuedButtonsLayout` under the queue table.
- `src/ld-lds-converter/converterdialog.cpp`
  - Constructor: promoted the dialog to a top-level `Qt::Window` with
    `Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint |
    Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint` so the title bar
    shows a working minimize button on Linux WMs (a `Qt::Dialog`-typed window
    omits it even with the hint). `exec()` still keeps the window modal.
  - `inputLineEdit` `textChanged` no longer wipes the queue (only refreshes
    output preview when queue empty); added `returnPressed` to append typed
    path(s) to the queue.
  - `on_inputBrowseButton_clicked()` and `dropEvent()` now call
    `appendToInputQueue()` (merge with dedup) instead of `setInputQueue()`.
    Both guarded against running during an active conversion.
  - Added `appendToInputQueue()`: dedups against existing queue, refreshes
    table + output preview, updates `sourceDirectory` to last-added file's dir
    so the next Add... starts nearby.
  - Added `refreshOutputPreview()`: queue size 1 -> derive output for that
    file; queue >1 -> leave output alone (batch derives per-file at convert
    time); queue empty -> fall back to line-edit-based preview.
  - Added `on_removeQueuedButton_clicked()`: removes selected table rows from
    the queue.
  - Added `on_clearQueuedButton_clicked()`: clears the whole queue + line edit.
  - `setInputQueue()` (CLI default population) now uses `refreshOutputPreview(true)`.
  - `on_outputFormatComboBox_currentIndexChanged` uses `refreshOutputPreview(false)`.
  - `on_outputBrowseButton_clicked` suggestion base falls back to first queued
    file when the line edit is empty.
  - `setConversionControlsEnabled()` also disables/enables Remove + Clear
    buttons during conversions.
- `src/ld-lds-converter/README.md`
  - GUI section updated to describe adding files one-by-one, Remove/Clear, and
    the minimize button.

## Commands run + results
1. `ninja -C /home/harry/tbc-tools/build ld-lds-converter` (outside nix shell)
   -> FAILED: Qt headers not found (expected; needs nix develop).
2. `cd /home/harry/tbc-tools && nix develop -c ninja -C build ld-lds-converter`
   -> SUCCESS: 5/5 steps compiled + linked, no errors/warnings.
   Output: `build/bin/ld-lds-converter` (308544 bytes).
3. `nix develop -c ctest --test-dir build -R ld-lds-converter --output-on-failure`
   -> 3/3 tests PASSED (help, invalid-format, invalid-compression-level).
4. `build/bin/ld-lds-converter --help` -> prints usage (CLI path intact).
5. (after `Qt::Window` minimize fix) `nix develop -c ninja -C build ld-lds-converter`
   -> SUCCESS (3/3 steps: autogen/moc, converterdialog.cpp.o, link).

## Verification status
- Compile: verified (clean build inside `nix develop`, two builds).
- CLI tests: verified (3/3 pass).
- GUI behavior — user-verified 2026-07-05:
  - Add.../drag-drop/type+Enter appends to the queue (one-by-one, across
    multiple directories) instead of replacing: CONFIRMED.
  - Remove selected / Clear all: CONFIRMED.
  - Minimize button visible and working (after promoting to `Qt::Window`; the
    initial `Qt::Dialog` + hint approach showed no button on this Linux WM):
    CONFIRMED.

## Restore point
Created: `ld-lds-converter-queue-minimize-validated_2026-07-05.md` +
`ld-lds-converter-queue-minimize-validated_2026-07-05.zip` (changed source
files + this prompt log + the validation log note).
