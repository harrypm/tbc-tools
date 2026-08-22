# tbc-tools Plugin System

This document describes the plugin support model, user experience, and
integration architecture for **ld-analyse's Plugin Manager**. It is the
canonical reference for how plugins are discovered, downloaded, verified,
installed, removed, and routed — and for how to add a new plugin without
rebuilding the application.

The Plugin Manager is **generic**: it is not specific to the CUDA runtime
plugin. Plugins are described by a remote JSON catalog, and each entry
declares which *backend* handles its install/remove lifecycle. Today two
backends exist:

- `cuda-runtime` — the existing CUDA 11.8 + cuDNN 8.9 runtime plugin for
  nnTransform3D GPU acceleration. Fetched from GitHub Releases on
  `harrypm/tbc-tools-ci-cache`. Handled by `CudaPluginManager`.
- `generic` — a self-contained download/verify/install path for any future
  plugin shipped as a direct-URL archive with a per-file SHA-256 manifest.
  Handled by `GenericPluginInstaller`.

Adding a new plugin is a **data change** (edit the catalog), not a code
change, for any plugin that fits the `generic` backend.

## Goals

- Let users see and download **new** plugins without an application update.
- Keep the manager working **offline** (bundled catalog) and degrade
  gracefully on network/rate-limit failure (cached catalog + retry).
- Preserve the existing CUDA plugin's behaviour and runtime integration
  exactly — the CUDA backend is one backend among many, not a special case
  in the UI.
- Treat every plugin uniformly in the UI: list, status, check-for-update,
  install/update, remove, and a restart prompt.

## Catalog model

The catalog is a single JSON file committed to this repository at
`plugins/catalog.json`. It is both:

- **Bundled** into ld-analyse as a Qt resource (`:/plugins/catalog.json`),
  so the manager always has a baseline list even with no network; and
- **Fetched fresh** from the web every time the Plugin Manager window is
  opened, so newly-published plugins appear immediately.

### Hosting and fetch order

The remote catalog is fetched with a CDN-first, raw-fallback order to
insulate against `raw.githubusercontent.com` rate limiting (HTTP 429), the
same pattern used elsewhere in tbc-tools (e.g. the Windows arm64
gas-preprocessor pre-fetch):

1. `https://cdn.jsdelivr.net/gh/harrypm/tbc-tools@main/plugins/catalog.json`
2. `https://raw.githubusercontent.com/harrypm/tbc-tools/main/plugins/catalog.json`

The first source that returns a valid, parseable catalog wins. On a
successful fetch, the raw JSON is cached to disk so the next open (online or
not) starts from the most recent good catalog.

### Schema

```json
{
  "catalog_version": 1,
  "updated": "2026-08-22T00:00:00Z",
  "plugins": [
    {
      "id": "tbc-tools.cuda-runtime",
      "display_name": "nnTransform3D CUDA",
      "description": "CUDA 11.8 + cuDNN 8.9 runtime ...",
      "category": "GPU acceleration",
      "backend": "cuda-runtime",
      "homepage": "https://github.com/harrypm/tbc-tools/wiki"
    }
  ]
}
```

Top-level fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `catalog_version` | int | yes | Schema version. Currently `1`. |
| `updated` | string (ISO-8601 UTC) | no | When the maintainer last edited the catalog. Shown for information. |
| `plugins` | array | yes | Ordered list of plugin entries. |

Per-plugin fields:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `id` | string | yes | Stable plugin id, e.g. `tbc-tools.cuda-runtime`. Used as the install subdirectory name and the routing key. Must be unique within the catalog. |
| `display_name` | string | yes | Shown in the plugin list. |
| `description` | string | yes | Shown in the details panel. |
| `category` | string | yes | Grouping label, e.g. `GPU acceleration`. |
| `backend` | string | yes | One of `cuda-runtime`, `generic`. Determines which installer handles the plugin. |
| `homepage` | string (URL) | no | Optional link for more info. |
| `version` | string | `generic` only | The catalog-advertised version of this plugin. Used for update detection. |
| `package_url` | string (URL) | `generic` only | Direct download URL for the archive (`.zip`/`.tar.gz`/`.tgz`). |
| `files` | array | `generic` only | Per-file manifest for SHA-256 verification (see below). |

`files[]` entry:

| Field | Type | Required | Meaning |
|---|---|---|---|
| `name` | string | yes | Path of the file **as extracted** by `tar` (relative to the install dir). |
| `sha256` | string (hex) | yes | Expected SHA-256, lowercase hex. |

### Validation rules

- `catalog_version` must equal `1`.
- `plugins` must be an array (may be empty).
- Every entry must have a non-empty `id`, `display_name`, `description`,
  `category`, and `backend`.
- `backend` must be a known value (`cuda-runtime` or `generic`); unknown
  backends are skipped with a debug log so a forward-incompatible catalog
  never breaks the manager.
- `generic` entries must additionally have `version`, `package_url`, and a
  non-empty `files[]` with `name` + `sha256` on each entry.
- Duplicate `id`s: the first occurrence wins; later ones are skipped.

A catalog that fails validation is treated as a fetch failure (the manager
falls back to the bundled/cached catalog and offers Retry).

## Backends and routing

`PluginManagerDialog` does not know how any plugin is downloaded. It routes
each action by the entry's `backend` field:

| Action | `cuda-runtime` | `generic` |
|---|---|---|
| Check for update | `CudaPluginManager::checkForUpdate()` (GitHub Releases API) | Compare installed `plugin.json` version vs catalog `version` (local; no network) |
| Install / Update | `CudaPluginManager::downloadAndInstall()` | `GenericPluginInstaller::install()` |
| Install from local archive | `CudaPluginManager::installFromLocalArchive()` | Disabled (generic plugins use direct URLs) |
| Remove | `CudaPluginManager::remove()` | `GenericPluginInstaller::remove()` |
| Status source | `Configuration` (`cudaPlugin` group) | `<installDir>/plugin.json` |

This keeps `CudaPluginManager` and the `cudaPlugin` Configuration group
completely untouched, while the manager UI itself is backend-agnostic.

### Why the CUDA backend stays separate

The CUDA plugin has two properties that do not fit the generic direct-URL
model:

1. It is resolved through the **GitHub Releases API** (latest release tag →
   manifest asset → package asset), not a fixed URL.
2. Its runtime integration (`comb.cpp`'s
   `ensureWindowsOnnxCudaProviderLoaded`) reads the CUDA-specific
   `Configuration` keys (`cudaPlugin.installPath`, `.enabled`, `.trusted`).

Unifying it under `generic` would either duplicate the Releases-API logic
inside the generic installer or require moving the runtime loader onto
`plugin.json`, both of which risk the existing, working CUDA path. The
generic backend is therefore the path for **new** plugins; CUDA remains a
first-class backend routed through the same UI.

## User experience

Opening the Plugin Manager (menu: **Plugins → Plugin Manager…**) shows:

1. A **catalog status line** at the top: `Fetching catalog…` on open, then
   either `Catalog updated` (remote fetch succeeded) or
   `Couldn't fetch catalog — showing bundled/cached list` with a **Retry**
   button (fetch failed / offline / rate-limited).
2. A **splitter**: the plugin list on the left, details on the right.
3. A **progress bar** and a **log view** below the splitter.
4. A **Close** button.

### List and details

Each plugin is one row in the list. Selecting a row populates the details
panel: display name, category, description, and a form with Status /
Installed version / Platform / Install path. The action buttons
(Check for update, Install / Update, Install from local archive, Remove)
enable/disable according to the selected plugin's backend and state.

### Lifecycle states

- **Not installed** — Install button reads `Install v<version>` once a
  version is known (CUDA: after Check; generic: from the catalog directly).
- **Installed** — version + path shown; Remove enabled; Install reads
  `Up to date`.
- **Update available** — Install reads `Update to v<version>`.

### Install / update flow

1. User clicks **Install / Update** (CUDA requires **Check for update**
   first to resolve the latest release; generic does not).
2. Confirmation dialog notes the package is SHA-256 verified but **not
   code-signed**, and shows the install path.
3. Progress bar tracks the download; the log names each file as it
   downloads.
4. On success: a message box prompts to **restart ld-analyse** for the
   change to take effect. On failure: the error is shown and (for SHA-256
   mismatch) the partial install is quarantined.

### Remove flow

1. User clicks **Remove** and confirms.
2. The install directory tree is deleted.
3. A message box prompts to **restart ld-analyse**.

### Offline / failure behaviour

- The dialog is **never empty on open**: the bundled resource catalog
  populates the list immediately, then the remote fetch refreshes it.
- If the remote fetch fails, the manager keeps showing the bundled/cached
  list, shows the failure status line, and reveals **Retry**. Action
  buttons remain usable for already-resolvable plugins (CUDA Check still
  works once it resolves its own release; generic plugins are resolvable
  from the catalog alone).
- Catalog fetch failure does **not** block install/remove of plugins whose
  state is already known.

## Integration architecture

```
PluginManagerDialog  (UI; backend-agnostic routing)
   |-- PluginCatalog            (discovery: bundled + cached + remote)
   |-- CudaPluginManager        (backend: cuda-runtime; UNCHANGED)
   '-- GenericPluginInstaller   (backend: generic; NEW)
```

### `PluginCatalog` (`src/ld-analyse/plugincatalog.{h,cpp}`)

Owns plugin discovery. Holds `PluginCatalogEntry` records parsed from the
catalog JSON. Public surface:

- `loadBundled()` — parse `:/plugins/catalog.json`. Always available.
- `loadCached()` — parse the last-good remote catalog from
  `<GenericDataLocation>/tbc-tools/plugins/catalog.cache.json`, if present.
- `fetchRemote()` — async GET with the CDN-first / raw-fallback order.
  On success: save cache, emit `catalogFetched(entries)`.
  On failure: emit `fetchFailed(error)`.
- `entries()` — the currently-loaded list.
- Signals: `catalogFetched(QList<PluginCatalogEntry>)`,
  `fetchFailed(QString)`.

Uses the same `User-Agent` (`tbc-tools/<version> (ld-analyse plugin
catalog)`) and `NoLessSafeRedirectPolicy` request setup as the rest of
ld-analyse's network code.

### `GenericPluginInstaller` (`src/ld-analyse/genericplugininstaller.{h,cpp}`)

Owns the `generic` backend lifecycle. Self-contained; does not touch
`CudaPluginManager` or the `cudaPlugin` Configuration group.

- `install(const PluginCatalogEntry &)` — download `package_url` to a temp
  file, `mkpath <GenericDataLocation>/tbc-tools/plugins/<id>`, extract via
  `tar -xf` (same extraction approach as the CUDA installer), SHA-256
  verify each `files[]` entry against the extracted file, and on success
  write `<installDir>/plugin.json` then emit `installSucceeded(path)`.
  On SHA-256 mismatch: rename the install dir to `<id>.quarantine` and emit
  `installFailed(...)`.
- `remove(const QString &id)` — delete the install dir; emit
  `removeSucceeded()` / `removeFailed(error)`.
- `installedInfo(const QString &id)` — read `<installDir>/plugin.json` and
  return `{installed, version, installPath}`.
- Signals mirror `CudaPluginManager`: `installProgress(recv, total,
  currentFile)`, `installSucceeded(path)`, `installFailed(error)`,
  `removeSucceeded()`, `removeFailed(error)`.

### `PluginManagerDialog` (`src/ld-analyse/pluginmanagerdialog.{h,cpp}`)

- Replaced its hardcoded `QList<PluginDescriptor>` with the catalog-driven
  `QList<PluginCatalogEntry>` from `PluginCatalog`.
- Loads bundled + cached catalog in the constructor (UI is never empty),
  then calls `fetchRemote()` from `showEvent()` **every time** the dialog
  is shown — this is the "pulled down every time the window is opened"
  trigger.
- Routes Check / Install / Install-from-archive / Remove by `backend`.
- Generic install/remove success/failure handlers mirror the existing CUDA
  handlers (log + `updateStatusDisplay()` + restart-prompt message box).
- The dialog is parented to the main window, so the shared
  `MainWindow` application-wide `QEvent::Show` filter already centers it
  over the main window (see AGENTS.md "ld-analyse sub-windows open
  centered"). No per-dialog `move()` centering is added.

## On-disk layout

Install root (OS-appropriate, update-persistent — same convention as the
CUDA plugin):

- Linux:   `~/.local/share/tbc-tools/plugins/`
- Windows: `%LOCALAPPDATA%/tbc-tools/plugins/`
- macOS:   `~/Library/Application Support/tbc-tools/plugins/`

Per generic plugin:

```
plugins/<id>/
    <extracted files...>
    plugin.json          # written by the installer on success
```

`plugin.json` (generic plugins' install record):

```json
{
  "plugin_id": "tbc-tools.example-plugin",
  "version": "1.0.0",
  "package_url": "https://.../example-plugin-1.0.0.zip",
  "install_path": "/.../plugins/tbc-tools.example-plugin",
  "installed_at": "2026-08-22T09:00:00Z",
  "files": [
    {"name": "example.dll", "sha256": "..."}
  ]
}
```

The catalog cache:

```
plugins/catalog.cache.json   # last-good remote catalog
```

The CUDA plugin continues to use its existing
`plugins/cuda/` directory and `cudaPlugin` Configuration keys; nothing
about its on-disk layout changes.

## Security

- All generic-plugin files are **SHA-256 verified** against the catalog
  manifest before the install is considered successful. A mismatch
  quarantines the partial install (renamed to `<id>.quarantine`) and the
  plugin is not activated.
- Plugin packages are **not code-signed**. The install confirmation dialog
  states this explicitly, matching the CUDA plugin's existing trust model.
- The catalog is fetched over HTTPS only; redirects are constrained by
  `NoLessSafeRedirectPolicy`.
- The catalog's `package_url` is taken as-is from the remote catalog;
  maintainers must only publish URLs they control over HTTPS. (Future
  hardening: pin allowed URL hosts in the catalog.)

## Publishing / adding a plugin

To add a **generic** plugin (no rebuild required):

1. Build/package the plugin as a `.zip` (Windows) or `.tar.gz` (Linux)
   archive.
2. Host the archive at a stable HTTPS URL.
3. Compute the SHA-256 of every file *as it will be extracted by `tar`*
   (i.e. relative to the archive root).
4. Add an entry to `plugins/catalog.json` with `backend: "generic"`,
   `version`, `package_url`, and `files[]`.
5. Commit and push to `main`. The next time any user opens the Plugin
   Manager, the jsDelivr/raw fetch returns the updated catalog and the new
   plugin appears.

To add a plugin that needs a **custom download mechanism** (e.g. a
different release API), add a new backend value and a matching installer
class, then route on it in `PluginManagerDialog`. The catalog schema and
UI need no changes.

The existing CUDA plugin is published by the
`publish_cuda_plugin.yml` workflow to `tbc-tools-ci-cache` Releases; that
workflow and its contract (`ci/check_ci_contracts.py`) are unchanged by
this feature.

## Build wiring

- `src/ld-analyse/ld-analyse-resources.qrc` embeds `plugins/catalog.json`
  at resource path `:/plugins/catalog.json`.
- `src/ld-analyse/CMakeLists.txt` lists `plugincatalog.cpp/.h` and
  `genericplugininstaller.cpp/.h` in `ld-analyse_SOURCES`. The qrc is
  already processed by the existing `qt_add_resources` call, so no
  additional CMake change is needed for the resource.

## CI contracts

`ci/check_ci_contracts.py` enforces no plugin-manager-specific contracts
today (it targets the export dialog and build/publish workflows). This
feature adds no new contract requirements; the check is re-run after the
change to confirm no regression. Optional future hardening: add a contract
asserting the catalog fetch uses the jsDelivr-first / raw-fallback order,
matching the gas-preprocessor insulation pattern.
