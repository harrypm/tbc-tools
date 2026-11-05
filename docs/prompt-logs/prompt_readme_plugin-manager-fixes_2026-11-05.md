# Prompt README - Plugin Manager: wider list + missing CUDA plugin install

Date: 2026-11-05
Repo: /home/harry/tbc-tools (main @ edf0218 at start, working tree on top)

## User request (input)

1. Fix the Plugin Manager's "Available plugins" list view to be wider.
2. The install CUDA plugin is missing (cannot install the CUDA runtime plugin).

## Research findings (verified against hard data)

- List width: PluginManagerDialog puts the QListWidget in a QSplitter with
  stretch 1:3 and only a 720px dialog minimum - at minimum size the list gets
  ~25% (~180px), eliding plugin names.
- Missing CUDA plugin: the bundled AND cached catalogs both contain the
  tbc-tools.cuda-runtime entry (no platform gate), so the list itself was not
  the problem. The actual failure: CudaPluginManager resolved the plugin via
  GitHub's /releases/latest endpoint, which returns whichever release GitHub
  marks "latest" overall in harrypm/tbc-tools-ci-cache. Verified live:
  cuda-lite-plugin-v1.0.0 (teletext lite runtime) is now the repo-wide
  "Latest" release; cuda-plugin-v1.0.0 (the CUDA runtime) is older. The
  manager therefore resolved cuda-lite-plugin-v1.0.0, found no
  tbc-tools-cuda-plugin-<platform>-<arch> assets in it, and failed - the CUDA
  runtime plugin could not be checked/installed.
- Related: the plugin catalog remote fetch tried jsDelivr first, but jsDelivr
  now permanently refuses this repo ("Package size exceeded the configured
  limit of 50 MB" - the repo outgrew the CDN limit once test fixtures grew),
  so every dialog open wasted a guaranteed-failing request before the
  raw.githubusercontent.com fallback.

## Changes

- src/tbc-analyse/cudapluginmanager.h/.cpp:
  - New public static selectLatestCudaPluginRelease(const QJsonArray&):
    picks the first (newest) cuda-plugin-v* release from a /releases listing,
    skipping drafts/prereleases; empty object when none exist.
  - checkForUpdate() now GETs /releases?per_page=100 instead of
    /releases/latest.
  - handleReleaseReply() parses the listing array, selects by tag prefix, and
    reports the existing "no CUDA plugin release published" error when no
    cuda-plugin-v* release exists; resolveAssetsFromReleaseJson(payload)
    became resolveAssetsFromRelease(QJsonObject) (same asset matching).
- src/tbc-analyse/pluginmanagerdialog.cpp: m_pluginList->setMinimumWidth(280)
  plus splitter->setSizes({320, 480}) initial split (~40% list at the 720px
  minimum); stretch factors unchanged.
- src/tbc-analyse/plugincatalog.cpp: catalog fetch order swapped to
  raw.githubusercontent.com first, jsDelivr fallback (documented why).
- New headless test src/tbc-analyse/testcudapluginmanager/ (root CMake
  BUILD_TESTING): pins the selection logic - the live failure repro
  (cuda-lite newest + cuda-plugin older -> cuda-plugin selected), newest-first
  ordering, draft/prerelease skipping, no cross-stream matching, empty and
  malformed listings.

## Commands run and results

- nix develop cmake reconfigure + ninja: clean.
- ctest -R testcudapluginmanager: Passed.
- Full ctest: 37/37 Passed. python3 ci/check_ci_contracts.py: passed.
- Live data verified during research: gh release list on tbc-tools-ci-cache
  (cuda-lite-plugin-v1.0.0 marked Latest; cuda-plugin-v1.0.0 not), the
  manager's /releases/latest usage (cudapluginmanager.cpp:171), and the
  jsDelivr 50MB refusal via curl.

## Pending

- Real-GUI confirmation (user): Plugin Manager list opens wide (>=280px at
  minimum dialog size); selecting "nnTransform3D CUDA" + Check for update
  resolves cuda-plugin-v1.0.0 and Install works again.
- Commit/push only when the user asks.
