# Prompt README — Plugin Catalog for the Plugin Manager

**Date:** 2026-08-22
**Repo:** `C:\Users\Harry\tbc-tools` (branch: `main`)
**Model:** glm 5.2 (Oz)

## User input (request)

> "I want to add a plugins folder with a JSON manifest which is pulled down
> every time the plugin manager window is opened. This will allow for users
> to see and download new plugins."
>
> (Follow-up) "Make a detailed and aligned `plugins.md` file for laying out
> plugin support, UX and integration, and execute. I want the plugin manager
> to be generic to support more than just CUDA."

## Clarifying answers (from user)

1. Catalog manifest hosted in a `plugins/` folder committed in this
   `harrypm/tbc-tools` repo, fetched via jsDelivr CDN with
   `raw.githubusercontent.com` fallback (matches existing rate-limit
   insulation pattern).
2. Catalog lists plugins with direct `package_url` + per-file SHA-256; the
   existing CUDA backend stays separate and untouched; new plugins use a
   generic download/verify/install path.
3. Ship a bundled manifest (Qt resource) as the always-available baseline;
   fetch the latest remote manifest on dialog open; on fetch failure show
   the bundled/cached list + a Retry button and an error notice.

## Design summary

- The Plugin Manager UI is **generic**: it routes every action by each
  catalog entry's `backend` field. Two backends:
  - `cuda-runtime` → existing `CudaPluginManager` (UNCHANGED).
  - `generic` → new `GenericPluginInstaller`.
- Adding a new `generic` plugin is a data change to `plugins/catalog.json`,
  not a code change / rebuild.
- `CudaPluginManager` and the `cudaPlugin` Configuration group are not
  modified.

## Files created

- `docs/plugins.md` — plugin support / UX / integration guide (canonical).
- `plugins/catalog.json` — bundled plugin catalog (v1; CUDA entry today).
- `src/ld-analyse/plugincatalog.h` / `.cpp` — discovery: bundled + cached +
  remote (jsDelivr-first, raw fallback), validation, cache write.
  - Shared `PluginCatalogEntry` / `PluginCatalogFile` structs.
  - `pluginsRootDirectory()` shared install-root helper.
- `src/ld-analyse/genericplugininstaller.h` / `.cpp` — generic backend:
  download → tar extract → SHA-256 verify → write `plugin.json`; remove;
  `installedInfo()`; signals mirroring `CudaPluginManager`.

## Files modified

- `src/ld-analyse/pluginmanagerdialog.h` — catalog-driven members,
  `showEvent` override, new slots (retry/catalog fetched/failed), removed
  `PluginDescriptor` struct (replaced by `PluginCatalogEntry`).
- `src/ld-analyse/pluginmanagerdialog.cpp` — ctor loads bundled+cached and
  adds catalog status label + Retry button; `showEvent` fetches remote every
  open; `updateStatusDisplay`/`onCheckForUpdate`/`onInstall`/`onRemove`/
  `onInstallFromLocalArchive` route by `backend`; simplified `setBusy`;
  appended `showEvent`/`onRetry`/`onCatalogFetched`/`onCatalogFetchFailed`.
- `src/ld-analyse/ld-analyse-resources.qrc` — embeds `plugins/catalog.json`
  at `:/plugins/catalog.json` (new `<qresource prefix="/plugins">`).
- `src/ld-analyse/CMakeLists.txt` — added `plugincatalog` and
  `genericplugininstaller` source pairs to `ld-analyse_SOURCES`.

## Plan

- Plan artifact created (`create_plan`): "Plugin catalog manifest for the
  Plugin Manager" — approved by user.

## Commands run

1. `ls C:\Users\Harry\tbc-tools` — top-level layout.
2. `grep` for `plugin`/`Plugin`/`PLUGIN` under `src` — found existing
   `pluginmanagerdialog.*`, `cudapluginmanager.*`, config keys.
3. Reads of `pluginmanagerdialog.{h,cpp}`, `cudapluginmanager.{h,cpp}`,
   `configuration.{h,cpp}`, `mainwindow.{h,cpp}`, `CMakeLists.txt`,
   `ld-analyse-resources.qrc`, `updatechecker.*`, `main.cpp`,
   `ci/check_ci_contracts.py`, `CMakePresets.json`.
4. Inspected `build\CMakeCache.txt` → generator `Visual Studio 17 2022`.
5. `python C:\Users\Harry\tbc-tools\ci\check_ci_contracts.py`
   → **"CI contract checks passed."** (no regression)
6. `cmake --build C:\Users\Harry\tbc-tools\build --config Release --target ld-analyse`
   → **Success.** CMake re-configured (picked up new sources + changed qrc),
   rcc generated `qrc_ld-analyse-resources.cpp`, MOC generated for
   `pluginmanagerdialog.cpp`, `plugincatalog.cpp`, `genericplugininstaller.cpp`.
   Outputs:
   - `build\lib\lddecode-library.lib`
   - `build\lib\lddecode-chroma.lib`
   - `build\bin\ld-analyse.exe`
   No compile/link errors.

## Validation status

- [x] Clean compile of new + modified files (primary correctness gate).
- [x] `ci/check_ci_contracts.py` passes (no contract regression).
- [x] `plugins/catalog.json` is valid JSON; embedded via Qt resource.
- [ ] **Runtime GUI confirmation — PENDING user.** This is a
      user-interactable change; per project rule, the user should open the
      Plugin Manager (Plugins → Plugin Manager…) and confirm:
        - dialog opens centered over the main window,
        - the CUDA plugin appears (from the bundled catalog) immediately,
        - the catalog status line updates ("Fetching catalog…" →
          "Catalog updated" online, or "Couldn't fetch catalog…" + Retry
          when offline/rate-limited),
        - Retry re-fetches,
        - Check/Install/Remove work for the CUDA entry as before.
- End-to-end generic-plugin download cannot be exercised until a real
  generic plugin package is hosted; the install/verify/remove code path is
  implemented and compile-verified.

## Revision 2 — Cancel download button + 404 diagnosis (2026-08-22)

### User feedback (round 1 GUI test)
1. Dialog opens centered over main window — confirmed.
2. CUDA plugin appears immediately (bundled catalog) — confirmed.
3/4. Catalog fetch failed with HTTP 404 ("Could not download the plugin
   catalog (last HTTP status 404)."). Retry also 404'd.
5. Check/Install/Remove work for CUDA, but a **Cancel download** button is
   needed.

### 404 root cause (verified via `git remote -v` + `git status`)
- `origin` = `https://github.com/harrypm/tbc-tools.git`; HEAD at
  `746b31d3 (HEAD -> main, origin/main)`.
- `git status` shows `?? plugins/` (untracked) — `plugins/catalog.json` is
  NOT committed/pushed, so both
  `https://cdn.jsdelivr.net/gh/harrypm/tbc-tools@main/plugins/catalog.json`
  and
  `https://raw.githubusercontent.com/harrypm/tbc-tools/main/plugins/catalog.json`
  correctly return 404.
- This is expected, not a bug. The bundled-resource fallback is why the CUDA
  plugin still appears. Once the new files are committed & pushed to `main`,
  the remote fetch returns 200 and the status line shows "Catalog updated".
- No code change was made for the 404.

### Cancel download button (implemented)
- `CudaPluginManager`: added `m_inFlightReply` (tracks the manifest/package
  reply) + `cancelInstall()` which calls `reply->abort()`. Set in
  `downloadAndInstall()` for both the manifest and package replies; cleared
  in each reply's `finished` lambda. (Departs from the original "unchanged"
  plan; required by the cancel requirement. The CUDA download/verify/install
  logic and the `cudaPlugin` Configuration group are unchanged.)
- `GenericPluginInstaller`: same pattern — `m_inFlightReply` + `cancelInstall()`.
- `PluginManagerDialog`: added a "Cancel download" button next to the
  progress bar; `setBusy(bool, bool canCancel=false)` shows it only for
  installs (not check/remove/local-archive). `onCancelDownload()` routes by
  backend to the matching `cancelInstall()`. `m_installCancelled` flag makes
  `onInstallFailed` show a friendly "The download was cancelled." info box
  instead of an "Install failed" warning; the flag is reset in `onInstall`
  (start) and `onInstallSucceeded`.
- Aborting the reply fires its `finished` handler → `installFailed`
  ("Operation canceled") → handled as a cancellation. Cancelling during the
  extraction phase is a no-op (reply already finished); the flag is reset on
  success so no stale cancel state leaks to a later failure.

### Revision 2 commands run
- `git --no-pager remote -v` / `git --no-pager log --oneline -3` /
  `git status --short ...` — confirmed catalog.json uncommitted.
- `cmake --build build --config Release --target ld-analyse` → success
  (rebuilt cudapluginmanager.cpp, genericplugininstaller.cpp,
  pluginmanagerdialog.cpp + MOC; `build\bin\ld-analyse.exe`).
- `python ci\check_ci_contracts.py` → "CI contract checks passed."

### Revision 2 validation status
- [x] Clean compile of cancel-button changes.
- [x] CI contracts pass.
- [ ] **Runtime re-confirm — PENDING user.** Please re-test:
        - Start a CUDA install; while the progress bar is active, click
          "Cancel download" → expect "The download was cancelled." and the
          progress bar/cancel button to hide, no partial install left.
        - Confirm Check/Install/Remove still work afterwards.
        - (Optional) After committing & pushing the new files to `main`,
          reopen the Plugin Manager and confirm the status line shows
          "Catalog updated." (remote fetch 200) instead of the 404.

## Notes / restore point

- No commit was made (user has not requested one).
- No restore-point zip created yet (rule: created when the user states a
  change is "fully fixed/working" — not yet confirmed).
- `CudaPluginManager`: modified in Revision 2 (added `cancelInstall()` +
  `m_inFlightReply` tracking for the Cancel download button). The
  `cudaPlugin` Configuration group remains untouched.
