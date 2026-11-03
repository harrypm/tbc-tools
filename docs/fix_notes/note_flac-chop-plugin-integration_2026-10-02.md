# Deferred integration note — FLAC-Chop in the plugin system (2026-10-02)

## User request (parked for later)
1. Add FLAC-Chop to the tbc-analyse plugin system.
2. Integrated call option when the binary is set/detected.
3. Prompt when new plugin versions are available to download.
4. Immediate want (paused mid-implementation): when an RF-export feature is used and flac-chop is NOT installed, prompt to download FLAC-Chop from the repo.

## Research findings (2026-10-02, for pickup)
### FLAC-Chop releases (harrypm/FLAC-Chop, e.g. v1.0.7)
- linux_FLAC-Chop_<tag>_x86.AppImage / _arm64.AppImage (self-contained, custom AppRun, bundles Qt6 + SoX)
- windows_FLAC-Chop_<tag>_x86_64.exe / _arm64.exe — 7z SFX: RAW sfx module + .7z payload concatenated, NO InstallConfig stub (build.yml "Create single-file portable Windows EXE (SFX)" step) → running it pops an extraction dialog; not silently automatable without 7z or an SFX-config change on the FLAC-Chop side. UNVERIFIED idea: Windows 10+ ships System32\tar.exe (libarchive) which may extract the appended 7z.
- macos_FLAC-Chop_<tag>_universal.dmg — needs hdiutil attach/copy/detach; CLI binary at FLAC-Chop.app/Contents/MacOS/flac-chop. QNetwork downloads do not set the quarantine xattr, so Gatekeeper should not block a copied app.

### tbc-analyse existing infrastructure that overlaps
- PluginCatalog (plugincatalog.{h,cpp}): bundled catalog Qt resource :/plugins/catalog.json + cached remote + jsDelivr/raw fetch of plugins/catalog.json from PluginCatalog::repositoryOwner/Name. Entry = id/displayName/description/category/backend/homepage (+generic: version/packageUrl/files with per-file SHA-256).
- GenericPluginInstaller (genericplugininstaller.{h,cpp}): backend "generic" — downloads packageUrl, tar-extracts, SHA-256 verifies, writes <pluginsRoot>/<id>/plugin.json install record. Install root: ~/.local/share/tbc-tools/plugins (Linux), %LOCALAPPDATA%/tbc-tools/plugins (Windows), ~/Library/Application Support/tbc-tools/plugins (macOS).
- CudaPluginManager + pluginmanagerdialog + updatechecker already do download progress/verify/update prompts.
- Gap for FLAC-Chop: release assets are AppImage/SFX-exe/dmg, NOT tar+manifest — either (a) FLAC-Chop also publishes .tar.xz packages + sha256 manifest (mirrors cuda-plugin pattern, cleanest), (b) new "appimage/exe" backend kind, or (c) download-prompt-only flow outside the plugin system for now.

### Current (pre-plugin) wiring that must keep working
- runRfSegmentExport(): resolve flac-chop via persisted rfExport/flacChopPath → beside-exe → PATH; interactive locate prompt validates isRunnableExecutableFile + --version banner ("FLAC-Chop") then persists; rfExport/sourcePath remembers the last probed RF source.
- Note: resolveExternalExecutable() should learn the future plugin install dir too when integrated.

## Decision
Deferred by user 2026-10-02 ("need to think about integration on this though"); design discussion to happen before implementation.
