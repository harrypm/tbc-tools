# Prompt README — AAA Linux fix (2026-08-28)

Goal: Actually fix AAA (VhsDecodeAutoAudioAlign) for Linux and pass a targeted
GitHub Actions test (the `build_linux_tools.yml` workflow, which contains the
AAA build+package step in both the x86_64 and arm64 jobs).

## Input / context
- Repo: /home/harry/tbc-tools, branch main, HEAD `3c344c2`.
- AAA = vendored VhsDecodeAutoAudioAlign C# project at
  `src/audio-align/vendor/vhs_decode_auto_audio_align-src` (v1.0.2).
- Linux CI builds AAA from vendored source (`scripts/build-aaa-linux.sh`) and
  packages it into a self-contained AppImage (`scripts/package-aaa-appimage.sh`)
  that bundles a Mono runtime (replaces the prebuilt Windows .exe so no host
  Mono is needed at runtime).

## Investigation (hard data)
- Latest CI run 33106684405 (2026-08-27 19:04) FAILED at step
  "Build and package native AAA AppImage from vendored source" in BOTH jobs.
- That run ran on commit `820b918` — BEFORE the two later fixes:
  - `f318569` (23:09): `<DebugType>embedded</DebugType>` -> `full`
    (OL8 Mono CSC: `CS1902: Invalid debug option 'embedded'`).
  - `39a0c83` (23:54): xbuild `TargetFrameworkVersion=v4.5` override + dropped
    apt `msbuild` on arm64 (`E: Unable to locate package msbuild`).
- No CI run has been triggered since those fixes landed. HEAD `3c344c2` has them.
- `39a0c83` only "verified" via the contract checker — the AAA compile was never
  actually run under xbuild, and the AppImage packaging was never exercised on
  CI at all (every run failed at compile first).

## Local verification performed
Toolchain on host: Mono 6.12.0.200, xbuild 14.0, msbuild, patchelf, unzip, bwrap.

### 1. Compile — reproduced OL8 (xbuild-only) and confirmed the fix
- Built a private PATH dir mirroring /usr/bin MINUS msbuild (xbuild + csc
  present, msbuild absent) to reproduce the OL8 toolchain.
- `env PATH=/tmp/xbuild-only bash scripts/build-aaa-linux.sh --out-dir /tmp/aaa-xbuild-test`
  => EXIT=0, produced VhsDecodeAutoAudioAlign.exe + Binah.dll + appimage/.
  Auto-detected `msbuild: /tmp/xbuild-only/xbuild`, no CS1902, no v4.7.2 warning.
  Script's mono smoke test passed.
- `mono .../VhsDecodeAutoAudioAlign.exe show-build-info` => prints
  "VHS-Decode Auto Audio Align v1.0.2" (grep -i audio matches).

### 2. Packaging — found and fixed a real self-containment bug
- `bash scripts/package-aaa-appimage.sh` (original) produced a 104 MB AppImage
  BUT its smoke test FAILED: `System.DllNotFoundException: System.Native`.
- Root cause: the bundled `mono` (distro mono-sgen, compiled prefix /usr) needs
  the dllmap config (`/etc/mono/config`: `System.Native -> $mono_libdir/libmono-native.so`)
  and the unversioned `libmono-native.so` (distro ships only `.so.0`). The
  original AppRun set no env and the script did not bundle `/etc/mono` nor the
  `.so` symlink, so the AppImage silently relied on host Mono paths.
- Empirical tests on a reconstructed AppDir:
  - A: LD_LIBRARY_PATH only -> FAIL (System.Native).
  - B: + MONO_CFG_DIR=bundled /etc/mono -> PASS.
  - C: bwrap with host /usr/lib/mono, /etc/mono, and host libmono* all MASKED,
    + LD_LIBRARY_PATH + MONO_CFG_DIR=bundled -> PASS. (Mono relocates its
    libdir relative to the binary, so this is genuinely host-independent.)

### Fix applied to `scripts/package-aaa-appimage.sh`
1. AppRun now exports `LD_LIBRARY_PATH=$HERE/usr/lib` and `MONO_CFG_DIR=$HERE/etc`
   before exec'ing the bundled mono (primary self-containment mechanism).
2. Bundles `/etc/mono` (or `<prefix>/etc/mono`) into `$APPDIR/etc/mono` so the
   dllmap config travels with the AppImage.
3. Creates `$APPDIR/usr/lib/libmono-native.so -> libmono-native.so.0` so the
   dllmap target resolves against the bundled lib.
4. Optional `patchelf --set-rpath '$ORIGIN:$ORIGIN/../lib'` on bundled mono +
   native libs (belt-and-suspenders; skipped silently where patchelf is absent,
   e.g. arm64 Ubuntu which doesn't apt-install patchelf — AppRun's export
   covers it, per test C).

### 3. Re-verified the fixed AppImage
- `bash scripts/package-aaa-appimage.sh` => EXIT=0, smoke test passed.
- CI-verifier style (NO external env):
  `env APPIMAGE_EXTRACT_AND_RUN=1 /tmp/aaa-fixed.AppImage show-build-info`
  => EXIT=0, "VHS-Decode Auto Audio Align v1.0.2".
- True self-containment (bwrap, host mono masked, NO external env):
  => EXIT=0, `BWRAP-GREP-AUDIO-OK`.

### 4. Contracts / tests
- `python3 ci/check_ci_contracts.py` => "CI contract checks passed." (exit 0).
- `python3 -m unittest discover -s ci/tests -p 'test_*.py'` => 46 tests OK.
- `bash -n scripts/package-aaa-appimage.sh` => SYNTAX-OK.

## Files changed
- `scripts/package-aaa-appimage.sh` — self-containment fix (AppRun env,
  bundle /etc/mono, libmono-native.so symlink, optional patchelf RPATH).

(No change needed to `scripts/build-aaa-linux.sh` — the compile fix from
`f318569`+`39a0c83` is already correct and verified. No change needed to the
workflow YAML or contract checker.)

## User decision
- User chose: commit + push directly to main, then trigger the workflow on main.

## Commits pushed to origin/main
1. `173ce6d` — package-aaa-appimage.sh: AppRun exports LD_LIBRARY_PATH +
   MONO_CFG_DIR; bundle /etc/mono; libmono-native.so -> .so.0 symlink;
   optional patchelf RPATH. (Worked locally; passed arm64 CI build-script
   smoke test but NOT OL8 — see below.)
2. `c33a932` — build-aaa-linux.sh: (a) resolve OUT_DIR absolute before cd into
   WORK_DIR (fixed arm64 "built exe missing in build-aaa"); (b) create host
   libmono-native.so symlink when missing (fixed OL8 build-script smoke test
   System.Native DllNotFound).
3. `281fc44` — package-aaa-appimage.sh: rewrite bundled etc/mono/config so
   Mono-native dllmap targets are BARE filenames (not $mono_libdir/...), so
   LD_LIBRARY_PATH resolves them regardless of Mono relocatability (OL8 Mono
   6.8 is NOT relocatable). Also bundle libmono-btls-shared.so; smoke test now
   prints output on failure for diagnostics.

## CI runs (build_linux_tools.yml, workflow_dispatch on main)
- 33106684405 (820b918, pre-fix): BOTH jobs failed at AAA compile (CS1902 /
  v4.7.2 / apt msbuild) — these were already fixed in HEAD by f318569+39a0c83
  but never run on CI.
- 33136997871 (173ce6d): BOTH jobs failed at AAA step.
  - x86_64 (OL8, mono 6.8.0.123, xbuild): compile OK, build-aaa-linux.sh smoke
    test FAILED — host mono DllNotFoundException: System.Native (OL8 ships only
    libmono-native.so.0, no unversioned .so the dllmap targets).
  - arm64 (Ubuntu 24.04, mono 6.8.0.105, xbuild): build-aaa-linux.sh OK, but
    package-aaa-appimage.sh FAILED — "built exe missing in build-aaa"
    (relative --out-dir created inside mktemp WORK_DIR, deleted by EXIT trap).
- 33163486396 (c33a932): arm64 AAA step SUCCESS; x86_64 AAA step FAILED at
  package-aaa-appimage.sh AppImage smoke test ("packaged AppImage did not run").
  Root cause: OL8 Mono 6.8 not relocatable; bundled dllmap config targets
  $mono_libdir/libmono-native.so = host /usr/lib64 path, not bundled; LD_LIBRARY_PATH
  can't redirect absolute-path dlopen.
- 33165019289 (281fc44): IN PROGRESS — robust bare-filename config fix.

## Local verification (hard data, host = Linux Mint, Mono 6.12 + xbuild 14.0)
- xbuild-only PATH reproduces OL8 toolchain: build-aaa-linux.sh compiles,
  smoke test passes, artifacts land in caller CWD (relative --out-dir fix).
- Symlink fix verified via MONO_CONFIG redirect to user-writable lib dir with
  only libmono-native.so.0 (no .so): reproduced DllNotFoundException
  (EXIT=1), then created libmono-native.so -> .so.0 symlink (the fix logic),
  re-ran -> show-build-info OK (GREP-AUDIO-OK). Proves the OL8 crash + fix.
- Robust AppImage (281fc44): runs with no external env (CI-verifier style,
  EXIT=0) AND under bwrap with host Mono fully masked (BWRAP-GREP-AUDIO-OK).
- Bundled config confirmed: dllmap targets are bare filenames
  (libmono-native.so / libMonoPosixHelper.so / libmono-btls-shared.so),
  no $mono_libdir remains.
- ci/check_ci_contracts.py + 46 contract unit tests pass; both scripts
  bash -n clean.

## Caveat / residual risk
- Could not fully simulate OL8's NON-relocatable Mono 6.8 locally (host Mono
  6.12 is relocatable). The bare-filename + LD_LIBRARY_PATH mechanism is the
  standard, well-understood one (same as Mono's libc dllmap) and is a strict
  superset of the relocatable approach, so it should work on OL8. The
  diagnostic smoke test (281fc44) will print the real error if OL8 still
  fails for any other reason, so the next CI run is diagnosable.
- The build-aaa-linux.sh host-symlink step modifies the host /usr/lib* (as
  root in CI). This is intentional: it creates the unversioned libmono-native.so
  dev symlink OL8's mono-devel package omits, which the distro SHOULD ship.
  No-op on Debian/Ubuntu/Mono 6.12+ which already ship it.
