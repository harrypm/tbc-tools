# Prompt README — AppImage taskbar icon packaging fix (2026-09-26)

## User prompt
> "pacakging bug, appimage icon is low res for taskbar use"

## Root cause (hard data — four separate icon-integration bugs)
1. **Icon name mismatch in the AppImage**: the workflow installed the icon as
   `hicolor/256x256/apps/tbc-tools.png` (desktop file `tbc-tools.desktop`,
   `Icon=tbc-tools`), but the app looks up `QIcon::fromTheme("tbc-analyse")`
   (`src/tbc-analyse/main.cpp:182`) and sets
   `QGuiApplication::setDesktopFileName("tbc-analyse")` (`main.cpp:178`).
   The theme lookup and window↔desktop-entry association both failed inside
   the AppImage, so taskbars fell back to a generic/poorly-scaled icon.
2. **AppRun never exported `XDG_DATA_DIRS`**: the AppDir's `usr/share/icons`
   was invisible to XDG icon-theme lookups even if names had matched
   (AppRun only set PATH/QT_PLUGIN_PATH/QPA vars).
3. **CMake install lacked 256x256**: `src/tbc-analyse/CMakeLists.txt` installed
   hicolor icons at 16/32/64/128 only (the 256 one existed only in the
   workflow, under the wrong name).
4. **No `StartupWMClass`**: `tbc-analyse.desktop` had no explicit WM_CLASS
   mapping, weakening X11 taskbar matching (Qt uses "tbc-analyse").

## Fix
- `build_linux_tools.yml` AppImage step: ship `tbc-analyse.desktop` (copied
  from `src/tbc-analyse/install/`), install all sizes
  (16/32/64/128/256) as `hicolor/<s>x<s>/apps/tbc-analyse.png`, update the
  AppDir symlinks (`.DirIcon` → `tbc-analyse.png`), and export
  `XDG_DATA_DIRS="$APPDIR/usr/share:…"` in AppRun.
- `src/tbc-analyse/CMakeLists.txt`: added the 256x256 hicolor install.
- `src/tbc-analyse/install/tbc-analyse.desktop`: added
  `StartupWMClass=tbc-analyse`.

## Validation (hard data, 2026-09-26)
- Workflow YAML parses; every extracted `run:` block passes `bash -n`;
  AppRun heredoc body passes `bash -n` (incl. new XDG line).
- `python3 ci/check_ci_contracts.py`: passed.
- `ninja tbc-analyse` under Nix: links clean.
- `cmake --install build --prefix /tmp/tbc-install-test`:
  `share/icons/hicolor/{16,32,64,128,256}x*/apps/tbc-analyse.png` +
  `share/applications/tbc-analyse.desktop` staged correctly.

## Status
Committed + pushed; the Tests workflow (release pipeline, build-only) validates
all platforms on GitHub Actions. Pending per real-world-confirmation rule:
user should install/launch the next CI-built x86_64 AppImage and confirm the
taskbar/window icon is sharp at panel size (and HiDPI if applicable).
