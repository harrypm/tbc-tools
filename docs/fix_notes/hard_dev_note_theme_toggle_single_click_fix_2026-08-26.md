# HARD DEV NOTE — single-click theme settle fix (2026-08-26)

## Confirmed symptom
Theme switching in `ld-analyse` required pressing Dark/Light twice for all elements (plots/scopes/cached widgets) to settle fully.

## Root-cause logic
The second manual click effectively performed a delayed re-apply pass after UI propagation had already advanced, which forced late/cached render paths to refresh completely. One-click flow was doing the immediate apply but not reproducing that delayed second user action at the menu-handler level.

## Final fix
In `src/ld-analyse/mainwindow.cpp` (`populateThemesMenu()`), both stock-theme handlers (Dark and Light) now do:
1. Immediate theme apply (`tbc::ui::applyStockDarkThemeToApp()` / `...Light...`)
2. Immediate `refreshThemeDependentUi()`
3. Delayed second apply via `QTimer::singleShot(24, ...)` plus `refreshThemeDependentUi()`
4. Guard: delayed pass only runs if that same action is still checked

This intentionally mirrors the previously successful “press it twice” behavior in one click.

## Files in this final hard-fix set
- `src/ld-analyse/mainwindow.cpp` (final one-click settle behavior)
- plus the broader theme infrastructure and entrypoint changes already in this working tree:
  - `src/library/tbc/uistyle.h`
  - `src/ld-analyse/mainwindow.h`
  - `src/ld-analyse/main.cpp`
  - `src/ld-analyse/efmhandler-main.cpp`
  - `src/audio-align/main.cpp`
  - `src/tbc-export-metadata/main.cpp`
  - `src/ld-lds-converter/main.cpp`
  - `src/tbc-metadata-converter/main.cpp`
  - `src/ld-analyse/plotwidget.{h,cpp}`
  - `src/ld-analyse/gui/oscilloscope/plotwidget.{h,cpp}`

## Build verification
`PATH="/nix/var/nix/profiles/default/bin:$PATH" nix develop -c ninja -C /Users/harry/tbc-tools/build ld-analyse`
- result: successful build (`ninja: no work to do` on follow-up run)

## Restore-point policy
User confirmed fixed state; a restore-point zip is created alongside this note for rollback safety.
- Archive: `restore_point_20260826T002218Z_theme_single_click_fix.zip`
