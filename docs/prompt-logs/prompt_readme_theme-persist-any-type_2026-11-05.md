# Prompt README - Keep theme choice persists the selection regardless of type

Date: 2026-11-05
Repo: /home/harry/tbc-tools (main @ e10e6f9 at start, working tree on top)

## User request (input)

Quick patch: user theme selection should persist regardless of type - the
Qt style overrides (Fusion, Windows, ...) in the Themes menu must survive
restarts like the stock presets (System/Dark/Light) already do, whenever
Keep theme choice is enabled.

## Previous behaviour

Keep theme choice only persisted the stock presets (theme.mode =
stock-*). Qt style actions applied QStyleFactory styles for the session
but stored nothing; the menu's final check logic also force-checked a
stock preset at startup, so even the checked state did not reflect an
active style override.

## Changes

- configuration.h/.cpp: new additive key theme.style (QString, empty =
  no override) with get/setThemeStyle; default empty; written/read in
  the theme group; setDefault clears it.
- main.cpp: after the palette resolution (CLI > persisted mode > follow
  OS), a persisted style is applied when Keep theme choice is on -
  QStyleFactory::create after validating the name against
  QStyleFactory::keys() (unknown/stale names are ignored so startup
  never fails). The Configuration instance was hoisted so both the
  palette and style resolution read one read-only instance.
- mainwindow.cpp populateThemesMenu:
  - Style action triggered: persists the style name (and resets the
    stored mode to stock-system, matching applyThemeStyle leaving the
    palette untouched) when Keep theme choice is on.
  - Keep toggle: persist-whatever-is-active - a checked stock preset
    stores its mode and clears the style; a checked style action stores
    the style name and resets the mode; unchecking clears both.
  - Final checked-state logic: if a Qt style action is checked (e.g. a
    persisted style applied at startup auto-checked via the existing
    currentStyleName match), it is left checked; only otherwise is the
    stock preset matching the active palette force-checked. This stops
    the old behaviour of unchecking a genuinely active style override.
  - Tooltip updated to say the choice persists regardless of type.
- testthemeconfiguration: +3 round-trip cases (mode+style together,
  style-only with follow-OS mode, cleared choice clears the style).
- Docs (docs/Tools/tbc-analyse.md Theme section, src/tbc-analyse/README
  Display Options): style overrides are no longer described as
  session-only.

## Commands run and results

- nix develop -c ninja -C build: clean.
- ctest -R testthemeconfiguration: Passed (new cases included).
- Full ctest: 37/37 Passed. python3 ci/check_ci_contracts.py: passed.

## Pending

- Real-GUI confirmation (user): tick Keep theme choice, pick a Qt style
  (e.g. Windows), restart -> the style should be restored and its menu
  entry checked; untick -> back to OS-following with no style.
- Commit/push only when the user asks.
- (Parallel) v3.3.1 release still blocked on the macOS arm64 pool
  capacity outage - retried separately.

## Follow-up 2026-11-15 - theme persistence audit (all options, all platforms)
User: "look into theme persistency mode across all options (even ones not available on linux etc) I think there might be a bug".
Applied the 2026-11-05 work (stash@{0}) onto main as fix/theme-persistence and fixed:
- Style names were never readable: tbc-analyse sets an application style sheet, so QApplication::style() is Qt's style-sheet proxy with an empty objectName()/name() (verified on Qt 6.8.3). Every style comparison (menu tick, applyThemeStyle's already-active check, persistence) silently saw "". New tbc::ui::setApplicationStyle()/applicationStyleName() in uistyle.h set the style by factory key and record the name; applyFusionStyleIfAvailable, applyThemeStyle and main.cpp use them.
- Menu at startup: presets run on Fusion, so the style loop ticked "Fusion" and the stash's final check then hid the preset; toggling Keep then saved Fusion as an override and reset the mode. Now a style entry is only the selection when the running style isn't Fusion.
- Presets now clear a stored style (they switch back to Fusion); a stale "Windows" no longer returns on top of Dark/Light after restart.
- Choosing a style keeps the palette mode (Dark + Windows restarts as Dark + Windows; it used to reset the mode to System). Keep toggle stores the running mode and style override (none for Fusion) independently.
- Styles from other platforms (windowsvista/windows11 on Windows, macOS on Mac) stored in the config are skipped at startup when not available; the preset is shown.
Verified with an offscreen harness (real ThemedApplication + MainWindow, isolated XDG config), 9 scenarios: Dark->Windows saved and restored with Windows ticked on the dark palette; Light clears the style and restarts with Light ticked; Keep off/on keeps light/no style; windowsvista+dark and macOS+light start on Fusion with the preset ticked; Windows->Fusion on dark restarts as Dark; System+Windows restarts following the OS with Windows. ctest 37/37, CI contract checks passed.
