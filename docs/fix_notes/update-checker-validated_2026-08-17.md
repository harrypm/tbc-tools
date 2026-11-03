# Validation Log - Update Checker (Check for Updates + Weekly GitHub release check)

**Date:** 2026-08-17
**Validated by:** User (real-world GUI confirmation)
**Status:** WORKING - user confirmed runtime behavior

---

## What was validated
The `ld-analyse` GUI update checker, built from the changes in this session.

## User confirmation
- User ran `build/bin/ld-analyse` and clicked **Help > Check for Updates...**
- Result: **"Works - shows the release info / up-to-date dialog"**

Because the installed build version (3.2.6) equals the latest GitHub release tag (`v3.2.6`), the manual check correctly produced the **up-to-date** dialog (Installed: 3.2.6 / Latest: 3.2.6). This confirms the full path end-to-end:
- Menu action is present and wired (auto-connect succeeded).
- Async HTTP GET to `https://api.github.com/repos/harrypm/tbc-tools/releases/latest` succeeds.
- JSON parse of `tag_name` / `html_url` / `name` works.
- Leading-`v` stripping + `QVersionNumber` comparison works (3.2.6 == 3.2.6 -> up to date, not "update available").
- Result dialog renders correctly.

## Files in restore-point zip (`update-checker-validated_2026-08-17.zip`)
Changed/created source for this feature:
- `src/ld-analyse/updatechecker.h` (new)
- `src/ld-analyse/updatechecker.cpp` (new)
- `src/ld-analyse/configuration.h` (modified)
- `src/ld-analyse/configuration.cpp` (modified)
- `src/ld-analyse/mainwindow.h` (modified)
- `src/ld-analyse/mainwindow.cpp` (modified)
- `src/ld-analyse/mainwindow.ui` (modified)
- `CMakeLists.txt` (modified)
- `src/ld-analyse/CMakeLists.txt` (modified)

Also bundled: this validation log and `prompt_readme_update-checker_2026-08-17.md`.

## Build verification (pre-user-confirm)
- `nix develop -c ninja -C build ld-analyse` -> `[19/19] Linking CXX executable bin/ld-analyse`
- `python3 ci/check_ci_contracts.py` -> `CI contract checks passed.`

## Restore instructions
To roll back to this validated state, unzip `update-checker-validated_2026-08-17.zip` over a clean checkout of the repo at this commit and rebuild with:
```
nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
nix develop -c ninja -C build ld-analyse
```

## Notes
- `APP_VERSION` resolved from `vcpkg.json` (`3.2.6`); GitHub tags are `v`-prefixed (`v3.2.6`). The comparison strips the `v`, so an installed 3.2.6 vs latest v3.2.6 correctly reports "up to date"; a future v3.2.7 would report "update available".
- The weekly automatic check is on by default and silent (no popup unless a newer, non-skipped release exists). It is skipped for unversioned `0.0.0` dev builds.
- Config is stored in `ld-analyse.ini` under `[updateCheck]` (`enabled`, `lastCheckTimestamp`, `skippedVersion`). No `SETTINGSVERSION` bump, so existing configs migrate cleanly.
