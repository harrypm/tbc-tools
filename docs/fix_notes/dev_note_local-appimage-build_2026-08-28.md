# Dev note — Local AppImage build (1:1 with CI)

A 1:1 local build guide was missing, so reproducing the GitHub Actions
`build_linux_tools.yml` x86_64 job on a developer machine was trial-and-error.
This note captures the working procedure and the three gotchas that cost real
time, with hard data, so the next person doesn't repeat them.

**Bottom line:** build the AppImage exactly like CI, but resolve the toolchain
(Qt/qmake, appimagetool env) from the flake's pinned store paths — never from a
blind `find /nix/store | head -1`, and never assume the bundle verifier launches
ld-analyse (it doesn't).

## Goal

Produce a runnable `tbc-tools-x86_64.AppImage` locally that is equivalent to the
CI artifact, for testing bundling/detection changes without pushing and waiting
on GH Actions.

## Prerequisites

- Nix with flakes enabled (`nix develop` works in the repo).
- `mono-devel`, `unzip`, `patchelf`, `curl` on the host (or via nix).
- `python3.11` + `pyinstaller` + `dunamai` (CI uses the container's python3.11;
  locally `python3.11 -m pip install -r src/tbc-video-export/pyinstaller/requirements-build-linux.txt`).
- Read/write on `/tmp` (the build uses ~1 GB).

## Working procedure

All script work is done from the repo root inside `nix develop`. The three
phases map 1:1 to workflow steps.

### 1. Nix build + AppDir assembly (`Build with Nix`)

```bash
cd /home/harry/tbc-tools
nix build .#
nix develop -c bash /tmp/build-appdir.sh
```

`build-appdir.sh` is a local replica of the workflow's `Build with Nix` step:
copies `result/bin/.*-wrapped` (the real Qt-linked ELFs, not the nix wrapper
scripts), bundles the flake's `.#ffmpeg`, normalises nix-store shebangs, copies
`result/lib` + `result/share`, gathers the glibc runtime + loader from the
binary's interpreter, closes the `/nix/store` lib dependency graph, and
patchelf-sets `$ORIGIN` rpaths. Output: `/tmp/tbc-appimage-build/AppDir`.

**Non-root adaptation:** nix-store files are copied read-only (0444). CI runs as
root so `patchelf`/`rm` bypass write perms; as a non-root developer you must
`chmod -R u+w` the AppDir before any rewrite or `rm -rf`. The script does this
before the patchelf rpath loop and before `rm -rf` of a stale AppDir.

### 2. PyInstaller tbc-video-export (`Build self-contained tbc-video-export`)

```bash
cd src/tbc-video-export
python3.11 -m pip install --upgrade pip
python3.11 -m pip install -r pyinstaller/requirements-build-linux.txt
export PYTHONPATH="$PWD/src"
mkdir -p build
python3.11 pyinstaller/build_linux.py
cp -f dist/tbc-video-export /tmp/tbc-appimage-build/AppDir/usr/bin/tbc-video-export
chmod +x /tmp/tbc-appimage-build/AppDir/usr/bin/tbc-video-export
```

This overwrites the CMake bash wrapper with the self-contained ELF (glibc 2.28
baseline on CI; locally it inherits the host glibc — fine for local testing,
do NOT ship a locally-built AppImage as a release).

### 3. AAA AppImage (`Build and package native AAA`)

```bash
bash scripts/build-aaa-linux.sh --out-dir /tmp/aaa-filetest/build-aaa   # xbuild or msbuild
AAAVENDOR=/tmp/tbc-appimage-build/AppDir/usr/bin/vendor/vhs_decode_auto_audio_align
mkdir -p "$AAAVENDOR"
bash scripts/package-aaa-appimage.sh --built-dir /tmp/aaa-filetest/build-aaa \
    --output "$AAAVENDOR/vhs-decode-aaa.AppImage"
rm -f "$AAAVENDOR/VhsDecodeAutoAudioAlign.exe" "$AAAVENDOR/Binah.dll"   # drop prebuilt .exe
```

The AAA AppImage is self-contained (bundles Mono, bare-filename dllmaps, internal
`libmono-native.so` symlink, AppRun exports `LD_LIBRARY_PATH`+`MONO_CFG_DIR`).

### 4. Package the full AppImage (`Download linuxdeploy and build AppImage`)

```bash
nix develop -c bash /tmp/package-appimage.sh
```

`package-appimage.sh` replicates the workflow's linuxdeploy + linuxdeploy-plugin-qt
+ Qt-plugin bundling + ELF-loader wrapping + AppRun + appimagetool squashfs
pipeline. Output: `/tmp/tbc-appimage-build/release/tbc-tools-x86_64.AppImage`
(~221 MB).

## Gotchas (with hard data)

These are the three that broke a "passing" build into a non-running AppImage.

### Gotcha 1 — Qt version skew: resolve qmake from the BUILT binary, not `find`

**Symptom:** AppImage exits 134 (SIGABRT):
`Could not find the Qt platform plugin "xcb" ... This application failed to start`.

**Root cause:** `package-appimage.sh` (copied from CI) resolved qmake with
```bash
QMAKE="$(find /nix/store -type f -path '*/bin/qmake' | sort | head -n 1)"
```
On a developer machine with multiple Nix evaluations cached, this grabs
**qtbase-6.10.1** (alphabetically first) while the flake pins **qtbase-6.8.3**.
`linuxdeploy-plugin-qt` then deploys a `libqxcb.so` that requires `Qt_6.10`
against the bundled `libQt6Core.so.6` = 6.8.3 → version not found → SIGABRT.

Hard data (readelf/strings on the broken bundle):
```
libqxcb.so: version `Qt_6.10` not found (required by libqxcb.so)
bundled libQt6Core.so.6: Qt 6.8.3
ld-analyse links: /nix/store/n1zfyp02...-qtbase-6.8.3/lib/libQt6Core.so.6
blind find picked:   /nix/store/kavwy4v9...-qtbase-6.10.1/bin/qmake
```

CI's clean runner store has only one qmake (6.8.3), so the blind find happens to
work there — but it is not deterministic and breaks on any machine with >1 Qt.

**Fix:** derive qmake from the SAME Qt store path the built binaries link against
(via `ldd` on `result/bin/.ld-analyse-wrapped` → `libQt6Core.so` → its store
prefix → `<prefix>/bin/qmake6`). Guard the `ldd` with `-f` because
`.ld-analyse.real` doesn't exist until the later wrap step (under
`set -o pipefail` a failing `ldd` in `$(...)` kills the script).

```bash
QT_CORE_LINKED="$(ldd result/bin/.ld-analyse-wrapped 2>/dev/null | awk '/libQt6Core\.so/ {print $3; exit}')"
QTBASE_PREFIX="$(dirname "$(dirname "$QT_CORE_LINKED")")"   # /nix/store/...-qtbase-6.8.3
QMAKE="$QTBASE_PREFIX/bin/qmake6"
```

The same determinism rule applies to the manual `copy_qt_plugin` fallback and
`copy_runtime_lib` (libQt6XcbQpa.so.6 etc.): resolve from the 6.8.3 qtbase
store path, not `find /nix/store -name <lib> | head -1`.

### Gotcha 2 — SOURCE_DATE_EPOCH breaks appimagetool's mksquashfs

**Symptom:** appimagetool fails:
`FATAL ERROR: SOURCE_DATE_EPOCH and command line options can't be used at the same time to set timestamp(s)`.

**Root cause:** `nix develop` exports `SOURCE_DATE_EPOCH=315532800` for
reproducibility. appimagetool's mksquashfs rejects it when it also passes a
timestamp option. CI doesn't hit this (the `Build Linux tools` job does NOT run
inside `nix develop` for the packaging step — it uses the installed Nix but a
plain shell, so SOURCE_DATE_EPOCH isn't inherited).

**Fix:** `unset SOURCE_DATE_EPOCH` immediately before the appimagetool call.

### Gotcha 3 — the bundle verifier does NOT launch ld-analyse

**Symptom:** `ci/verify_linux_bundle.sh x86-appimage <AppImage>` reports
`Linux bundle validation passed` even though the AppImage SIGABRTs on launch.

**Root cause:** the verifier's smoke tests only run non-Qt tools:
`x86-appimage-extract-and-run-tbc-video-export`, `x86-appimage-apprun-tbc-video-export`,
`x86-appimage-apprun-ffmpeg`. None of them launch `ld-analyse`, so a bundle
whose Qt platform plugin can't initialise (Gotcha 1) passes verification. The
AAA detection checks added in `eafcdb7` validate AAA detection/launch, but
there is still no check that ld-analyse itself starts.

**Fix (procedural):** after the verifier passes, always do a real launch test:
```bash
/tmp/tbc-appimage-build/release/tbc-tools-x86_64.AppImage ld-analyse --version
# or headless: APPIMAGE_EXTRACT_AND_RUN=1 <AppImage> ld-analyse --version
```
A running AppImage is the only proof the Qt bundle is coherent. (A future
verifier hardening: add an `x86-appimage-ld-analyse-launch` smoke test that
runs ld-analyse --version with QT_QPA_PLATFORM=offscreen so it's headless-safe.)

## Verification that the AppImage actually runs

After fixing Gotchas 1+2:
```bash
APPIMAGE_EXTRACT_AND_RUN=1 release/tbc-tools-x86_64.AppImage ld-analyse --version
```
must print the ld-analyse version without SIGABRT. Then:
```bash
bash ci/verify_linux_bundle.sh x86-appimage "$(pwd)/release/tbc-tools-x86_64.AppImage"
```
passes (AAA detection + no-host-mono checks). Both together = a runnable,
detection-valid bundle.

## Files

- `/tmp/build-appdir.sh` — Build-with-Nix AppDir assembly replica.
- `/tmp/package-appimage.sh` — linuxdeploy + Qt + appimagetool packaging replica
  (with the deterministic-qmake and unset-SOURCE_DATE_EPOCH fixes applied).
- Repo scripts unchanged: `scripts/build-aaa-linux.sh`,
  `scripts/package-aaa-appimage.sh`, `ci/verify_linux_bundle.sh`.

## Caveat

A locally-built AppImage is for testing only — it inherits the host glibc
(Linux Mint ~2.35) not CI's OL8 glibc 2.28 baseline, so it is less portable
than the CI artifact. Do not ship it as a release; the canonical build remains
the GH Actions artifact.
