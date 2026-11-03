# Prompt README - Themes menu "Keep theme choice" persistence

Date: 2026-11-02
Repo: /home/harry/tbc-tools (main @ a5692e3 at start, working tree on top)

## User request (input)

Add a "make theme user persistent" checkbox to the Themes menu (referred to as
the themes tab), so the user's selected theme survives restarts.

## Design (confirmed behaviour)

- Themes menu gains a non-exclusive checkable "Keep theme choice" item below
  Light (independent of the System/Dark/Light exclusive preset group).
- Checked: picking System/Dark/Light persists the preset
  (theme.keepChoice=true, theme.mode=stock-*) and the choice is restored on
  the next launch. Unchecked: the stored mode is cleared and tbc-analyse
  always starts following the OS light/dark scheme.
- Startup precedence in main.cpp: explicit CLI flags (--light-theme /
  --force-dark-theme) > persisted choice (when keepChoice) > follow OS.
- The menu checks the preset matching the ACTIVE theme (derived from
  ThemedApplication: isFollowingSystemTheme + isDarkTheme property) instead of
  always defaulting to System.
- Storage: additive Configuration keys (theme group: keepChoice + mode) with
  defaults, so no SETTINGSVERSION bump and existing configs are untouched.
  Mode strings match the menu action data (stock-system/stock-dark/
  stock-light) and are sanitised on read (anything else -> stock-system).
- Style overrides (the Qt QStyleFactory list) are NOT persisted - only the
  stock presets carry a stored mode (matches previous session-only behaviour).

## Changes

- src/tbc-analyse/configuration.h/.cpp: Theme struct (keepChoice, mode),
  get/set, read/write group with defaults + sanitisation, setDefault.
- src/tbc-analyse/main.cpp: startup theme precedence (CLI > persisted > OS)
  via a read-only Configuration instance before MainWindow construction.
- src/tbc-analyse/mainwindow.cpp populateThemesMenu: "Keep theme choice"
  action (tooltip, persistence wiring), per-persist save on preset selection,
  active-preset-derived checked state (both the normal and the no-styles
  placeholder paths).
- New headless test src/tbc-analyse/testthemeconfiguration/ (registered in
  root CMakeLists under BUILD_TESTING): round-trip of keepChoice/mode against
  Qt's test-mode config sandbox (QStandardPaths::setTestModeEnabled - never
  touches the real tbc-analyse.ini), defaults, clear-on-uncheck, and
  garbage-mode sanitisation. Release-proof verify() checks (no assert()).

## Commands run and results

- nix develop -c ninja -C build (after menu/config edits): clean.
- ctest (targeted): aaa-* + testchromaconfigdialog + testexportarguments all
  Passed; python3 ci/check_ci_contracts.py passed.
- nix develop -c cmake reconfigure (new test target) + ninja: clean.
- Full ctest: 36/36 Passed (new testthemeconfiguration included).

## Internal docs updated (same commit, user-requested)

- docs/Tools/tbc-analyse.md: replaced the stale -style fusion dark-mode note with a
  Theme section (follow-OS default, Themes menu presets + Keep theme choice, CLI pins)
  and refreshed the options block to match the real --help output; noted the
  user-absolute chroma decoder selection + ntsc3dnoadapt round-trip in the export
  decoder defaults section.
- src/tbc-analyse/README.md: Display Options updated (follow-OS default, --light-theme
  added, pin precedence over persisted choice, Themes menu summary); Qt5->Qt6 fix.

## Pending

- Real-GUI confirmation (user): tick "Keep theme choice", pick Dark, restart
  tbc-analyse -> should start Dark with Dark ticked; untick, restart -> should follow the OS.
- Commit/push: done (user-requested) - see the commit list in the main repo history.
- GitHub Actions parity for the pushed head.
