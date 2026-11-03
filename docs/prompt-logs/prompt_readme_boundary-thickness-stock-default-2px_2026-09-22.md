# Prompt README: Export boundary thickness stock default 4 px -> 2 px
Date: 2026-09-22
Working dir: /home/harry/tbc-tools (branch: main)

## User prompt (input)
"set stock boundry adjusmtnet for framing to 2x pix for stock, this should apply to all new updated copys untill users manually change it (unless I already fixed this value?)"

## Investigation (commands run)
- `grep` for boundary/framing across repo -> found the setting is the "Boundary thickness (px)" control in the ld-analyse Video Parameters (framing) dialogue.
- Read `src/ld-analyse/configuration.cpp/.h`, `videoparametersdialog.cpp/.h/.ui`, `mainwindow.cpp` (initialisation + signal handlers), `development-logs/log_note_video_parameters_fixed_2026-03-01.md`.
- `git --no-pager log` on the three files -> feature added in commit b1b5e55 "Add export boundary thickness control" with stock default 4 px. Value already persisted on manual change (confirmed by the 2026-03-01 log note), but stock default was never 2.
- Answer to user question: NO, the value had not already been fixed; stock default was 4 px.
- `grep` in `ci/check_ci_contracts.py` for boundary/configuration -> no contract pins this code; `exportarguments.*` is unrelated (export framing-mode helper).

## Changes made (edit_files)
- `src/ld-analyse/configuration.h`: added `bool exportBoundaryThicknessUserSet;` to `ViewOptions`.
- `src/ld-analyse/configuration.cpp`:
  - `readConfiguration()`: read fallback for `exportBoundaryThickness` changed 4 -> 2 (new stock); reads new additive key `exportBoundaryThicknessUserSet` (default false); migration: if flag false AND stored value == 4 (old stock default, i.e. never manually changed) -> adopt new stock value 2. Clamps 1-8 unchanged.
  - `writeConfiguration()`: writes `exportBoundaryThicknessUserSet`.
  - `setDefault()`: stock thickness 4 -> 2, userSet = false.
  - `setExportBoundaryThickness()`: also sets userSet = true (only caller is the manual-change signal handler `MainWindow::exportBoundaryThicknessChangedSignalHandler`, mainwindow.cpp:7730).
- No SETTINGSVERSION bump (additive-key pattern, same as updateCheck/cudaPlugin/vbiProcessing groups; a bump would wipe all user settings).

## Behaviour after change
- Fresh install / no INI: 2 px.
- Updated copy, user never touched thickness (stored 4, no flag): migrates to 2 px.
- User manually changed thickness (any value != 4, or flag true): their value is kept, including a deliberate 4.

## Build verification (commands run)
- `ninja -C build ld-analyse` (outside Nix) -> FAILED: missing QtWidgets headers (environment issue, build dir is Nix-configured; not caused by the change).
- `nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && nix develop -c ninja -C build ld-analyse` -> SUCCESS: `[13/13] Linking CXX executable bin/ld-analyse`.
- Full CI (GitHub Actions) not yet run; not treated as fully verified per repo rules.

## Real-world confirmation (pending)
Awaiting user GUI confirmation (rule: real-world confirmation for user-interactable changes): open ld-analyse, Video Parameters -> boundary should render at 2 px (or user's own saved value if previously changed).
