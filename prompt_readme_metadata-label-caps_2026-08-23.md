# Prompt README — Metadata Status / Metadata Conversion label capitalization

Date: 2026-08-23
Branch: main
Base commit: ade6c7a6 (origin/main)

## Request (input)
> Tidy up Metadata Status and Metadata Conversion (caps on second word start)

Make the second word of the "Metadata Status" and "Metadata Conversion"
menu items / dialog titles start with a capital letter, for consistency
with the rest of the Tools menu (e.g. "Metadata Editor...").

## Investigation
Grepped `src/` for `Metadata [Ss]tatus` / `Metadata [Cc]onversion`.

User-facing UI strings found:
- `src/ld-analyse/mainwindow.ui:888`  action `actionMetadata_Conversion` text = `Metadata conversion...`  (lowercase c)
- `src/ld-analyse/mainwindow.ui:893`  action `actionMetadata_Status` text     = `Metadata status...`      (lowercase s)
- `src/ld-analyse/metadatastatusdialog.ui:6`   windowTitle = `Metadata status`        (lowercase s)
- `src/ld-analyse/metadataconversiondialog.ui:14` windowTitle = `Metadata Conversion` (ALREADY correct)

Non-UI / mid-sentence usages left unchanged (lowercase is grammatically
correct in sentence context):
- `src/ld-analyse/metadataconverterutil.cpp:195`   error message "Could not determine metadata conversion direction..."
- `src/tbc-metadata-converter/main.cpp:261`        qInfo "Beginning metadata conversion (...)"
- `src/tbc-metadata-converter/metadataconversiondialog.ui:14` windowTitle = `Metadata Conversion` (already correct)
- `src/tbc-metadata-converter/metadataconverterutil.cpp:177`  error message

Verified no programmatic `setWindowTitle` override in
`metadatastatusdialog.cpp` / `metadataconversiondialog.cpp` (grep for
`setWindowTitle` did not match those files), so the .ui titles are
authoritative.

## Edits applied
1. `src/ld-analyse/mainwindow.ui` (lines 886-895):
   - `Metadata conversion...` -> `Metadata Conversion...`
   - `Metadata status...`      -> `Metadata Status...`
2. `src/ld-analyse/metadatastatusdialog.ui` (line 6):
   - windowTitle `Metadata status` -> `Metadata Status`

(`metadataconversiondialog.ui` was already `Metadata Conversion` — no change.)

## Commands run
- `git status --short` (review changed files)
- `git grep` for `Metadata [Ss]tatus` / `Metadata [Cc]onversion` / `setWindowTitle`
- Process check: `Get-Process ld-analyse` (PID 16548 running -> user closed it)
- Build: `cmake --build "C:\Users\Harry\tbc-tools\build" --config Release --target ld-analyse`
  - Result: clean. `metadatastatusdialog.cpp` recompiled (uic regenerated
    header with new windowTitle); `ld-analyse.exe` linked.

## Verification (real-world GUI confirmation)
User relaunched ld-analyse and confirmed:
- Tools menu: both items now read "Metadata Status..." and "Metadata
  Conversion..." (capital second word).  -> YES
- Dialog title bars: "Metadata Status" and "Metadata Conversion".  -> YES

## Restore point
Changes are git-tracked (3 string edits across 2 .ui files). git history is
the rollback; no separate .zip restore point created (no .zip restore points
are stored in this repo tree per existing convention).

## Status
Changes built and GUI-verified. Not yet committed (awaiting user instruction
to commit & push to origin/main).
