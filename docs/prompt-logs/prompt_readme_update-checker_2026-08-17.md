# Prompt README - Update Checker (Check for Updates + Weekly GitHub release check)

**Date:** 2026-08-17
**Repo:** /home/harry/tbc-tools (https://github.com/harrypm/tbc-tools.git)
**Branch:** main
**Request:** Add a "Check for updates" feature (links to GitHub releases) and a weekly automatic update checker that queries GitHub for a newer release version.

---

## User input
- "I want to add check for updates (links to releases) and I want to add a weekly is there an update checker to the tool to check with GitHub if there is a new release version"

## Verified hard data (GitHub API)
Queried the real endpoint to confirm tag format (rule: never assume without hard data):
```
curl -sS -H "Accept: application/vnd.github+json" "https://api.github.com/repos/harrypm/tbc-tools/releases/latest"
```
Result:
- `tag_name`: `v3.2.6`  (leading `v` prefix - confirmed)
- `html_url`: `https://github.com/harrypm/tbc-tools/releases/tag/v3.2.6`
- `name`: `v3.2.6 - SECAM Support`
- `prerelease`: false
Tags list: `v3.2.6, v3.2.5, v3.2.4, ...`
App `APP_VERSION` (from `vcpkg.json`): `3.2.6` (no `v` prefix) -> comparison strips the leading `v`.

Endpoint used by the tool: `https://api.github.com/repos/harrypm/tbc-tools/releases/latest`

## Commands run
1. `ls -la /home/harry/tbc-tools` + `git remote` -> repo URL `https://github.com/harrypm/tbc-tools.git`
2. File reads (read_files tool): mainwindow.h, aboutdialog.h, configuration.h, mainwindow.cpp (ranges), aboutdialog.cpp, aboutdialog.ui, CMakeLists.txt, src/ld-analyse/CMakeLists.txt, vcpkg.json, configuration.cpp, main.cpp, mainwindow.ui (Help menu range), flake.nix
3. grep: APP_VERSION/APP_BRANCH/APP_COMMIT; action handlers; menu structure
4. `curl ... api.github.com/repos/harrypm/tbc-tools/releases/latest` (read-only GET) - see above
5. `nix develop -c ninja -C build ld-analyse` -> first attempt FAILED (vexing parse on `QNetworkRequest request(QUrl(apiUrl));`)
6. Fixed with brace init: `QNetworkRequest request{QUrl(apiUrl)};`
7. `nix develop -c ninja -C build ld-analyse` -> SUCCESS: `[19/19] Linking CXX executable bin/ld-analyse`
8. `python3 ci/check_ci_contracts.py` -> `CI contract checks passed.`
9. `grep actionCheck_for_Updates build/.../ui_mainwindow.h` -> action present and added to `menuHelp` (auto-connected)

## Files created
- `src/ld-analyse/updatechecker.h` - UpdateChecker class (QNetworkAccessManager, signals, version compare helpers)
- `src/ld-analyse/updatechecker.cpp` - implementation (GET GitHub releases/latest, JSON parse, QVersionNumber compare)

## Files modified
- `src/ld-analyse/configuration.h` - added `UpdateCheck` settings struct + getters/setters (enabled, lastCheckTimestamp, skippedVersion)
- `src/ld-analyse/configuration.cpp` - read/write `updateCheck` group (additive, no SETTINGSVERSION bump) + accessor implementations + defaults
- `src/ld-analyse/mainwindow.ui` - added `actionCheck_for_Updates` to Help menu (between separator and About) + action definition
- `src/ld-analyse/mainwindow.h` - include updatechecker.h, `on_actionCheck_for_Updates_triggered` slot, UpdateChecker member + handler methods
- `src/ld-analyse/mainwindow.cpp` - construct UpdateChecker + connect signals + deferred weekly check in ctor; implement manual slot, weekly throttle, timestamp persistence, update-available dialog (Download / Skip this version / Remind me later), up-to-date + failure handling
- `CMakeLists.txt` - added `Network` to `find_package(Qt6 ...)`
- `src/ld-analyse/CMakeLists.txt` - added `updatechecker.cpp updatechecker.h` to sources, `Qt::Network` to link

## Behaviour summary
- **Manual:** Help > Check for Updates... -> async GET to GitHub, compares latest tag vs APP_VERSION.
  - Newer: dialog with Installed/Latest/release name, buttons Download (opens release URL in browser), Skip this version (persisted), Remind me later.
  - Up to date: information dialog.
  - Failed: warning dialog with "Open releases page" fallback.
- **Automatic weekly:** on startup (3 s deferred) if `updateCheckEnabled` is true and >= 7 days since `lastUpdateCheckTimestamp`, performs a silent check.
  - Newer (and not equal to skippedVersion): shows the update-available dialog.
  - Up to date: transient status-bar message only (no popup).
  - Failed: debug-log only (no popup).
  - Skipped for `APP_VERSION == "0.0.0"` (dev/unversioned builds) to avoid noise.
- **Persistence:** `ld-analyse.ini` `[updateCheck]` group: `enabled`, `lastCheckTimestamp` (ISO-8601 UTC), `skippedVersion`.

## Verification status
- Compiles and links cleanly (`ld-analyse`).
- CI contract checker passes.
- GitHub API tag format verified against hard data.
- GUI interactable elements (menu item + dialogs) NOT yet confirmed by the user at runtime - awaiting real-world confirmation.

## Notes / decisions
- `SETTINGSVERSION` not bumped: new config keys are additive with defaults, so existing `ld-analyse.ini` files keep working (missing keys fall back to defaults, then get written on next save).
- Qt6 `Network` module is part of `qtbase` (available in both the Nix flake `qt6.qtbase` and vcpkg `qtbase` default features) - no new external dependency added.
- Unauthenticated GitHub API rate limit (60 req/hr) is fine for a weekly check + occasional manual checks.
