# Log note — AAA Linux fix validated (2026-08-28)

User-confirmed fix. GitHub Actions run is GREEN for both Linux jobs.

## Result (hard data)
- Workflow: `build_linux_tools.yml`, run 33166410891, head `6d26700`, main.
- Run conclusion: **success** (completed).
- Build tbc-tools (x86_64) — Oracle Linux 8 container, Mono 6.8.0.123, xbuild 14.0: **success**.
- Build tbc-tools (arm64) — Ubuntu 24.04, Mono 6.8.0.105, xbuild: **success**.
- AAA "Build and package native AAA AppImage from vendored source" step passed in BOTH jobs; the bundle verifier (which runs the self-contained AAA AppImage with no host Mono) also passed.

## Commit range on origin/main (AAA fix)
1. `173ce6d` — package-aaa-appimage.sh: self-containment (AppRun exports LD_LIBRARY_PATH + MONO_CFG_DIR; bundle /etc/mono; libmono-native.so -> .so.0 symlink; optional patchelf RPATH).
2. `c33a932` — build-aaa-linux.sh: resolve OUT_DIR absolute before cd into mktemp WORK_DIR (fixed arm64 "built exe missing in build-aaa"); + host libmono-native.so symlink (interim, later removed).
3. `281fc44` — package-aaa-appimage.sh: rewrite bundled etc/mono/config so Mono-native dllmap targets are BARE filenames (not $mono_libdir/...), so LD_LIBRARY_PATH resolves them on non-relocatable OL8 Mono 6.8; bundle libmono-btls-shared.so; smoke test prints output on failure for diagnostics.
4. `6d26700` — build-aaa-linux.sh: replace fragile host-mono runtime smoke test with `file`-based .NET assembly check (no System.Native/dllmap dependency); removed the interim host-symlink workaround. Net -18 lines.

## Root causes that were fixed
- OL8 Mono 6.8.0.123 ships only versioned `libmono-native.so.0` (no unversioned `.so` the dllmap targets) AND is non-relocatable ($mono_libdir resolved from compile-time /usr/lib64). Host `mono show-build-info` crashed with DllNotFoundException: System.Native on the first DateTime.Now (AAA Log prefixes timestamps).
- build-aaa-linux.sh did `cd "$WORK_DIR"` (mktemp) then copied artifacts to a relative `$OUT_DIR`, so the CI's `--out-dir build-aaa` was created inside WORK_DIR and deleted by the EXIT trap -> package step saw "built exe missing in build-aaa".
- package-aaa-appimage.sh's bundled dllmap config kept `$mono_libdir/...` absolute targets, so on non-relocatable OL8 Mono the dlopen pointed at the HOST lib, not the bundled one; LD_LIBRARY_PATH can't redirect an absolute-path target.

## Final mechanism (simple)
- build-aaa-linux.sh: build via xbuild (TargetFrameworkVersion=v4.5 override + DebugType=full), stage to absolute OUT_DIR, validate the exe with `file` (no host-mono runtime).
- package-aaa-appimage.sh: bundle distro mono + native libs + /etc/mono config; rewrite config so dllmap targets are bare filenames; create internal libmono-native.so -> .so.0 symlink; AppRun exports LD_LIBRARY_PATH=$HERE/usr/lib + MONO_CFG_DIR=$HERE/etc; optional patchelf RPATH. AppImage is self-contained (verified under bwrap with host Mono fully masked).

## Restore point
- Validated working tree state is exactly origin/main @ `6d26700`.
- A zip of the two changed scripts at this commit is preserved alongside this note: `aaa-linux-fix-validated_2026-08-28.zip`.

## Follow-up: AAA detection tests (commit eafcdb7, 2026-08-28)

The user pointed out the green run only proved the AAA AppImage exists + runs
standalone — it did NOT validate that ld-analyse actually DETECTS and launches
AAA, and there was no CI test for detection. The bundle verifier had been
running the AAA AppImage at a hard-coded absolute path, which passes even if
ld-analyse's appDir-relative resolver could not find it.

Added real detection coverage:
1. Public resolver API in audioalignmentutil.h/.cpp:
   - resolvedAudioAlignPath() — detection (appDir-relative vendor lookup + PATH).
   - audioAlignRunnerCommand() — launch argv ([mono,<exe>] or
     [env,APPIMAGE_EXTRACT_AND_RUN=1,<appimage>]).
2. testaudioalignmentruntime.cpp: --detect-aaa mode (detect must pass; launch
   best-effort SKIP/77 if .exe + no mono). CMakeLists.txt: aaa-detection CTest.
3. verify_linux_bundle.sh: x86-appimage-aaa-detection + arm64-aaa-detection —
   compute the AAA AppImage path RELATIVE to ld-analyse's own directory (the
   resolver's applicationDirPath()), launch via the resolver's env mechanism.
   check_ci_contracts.py enforces the new labels.

Verified locally:
- aaa-detection CTest passes; aaa-runtime still passes.
- Bundle verifier (with detection check) passes against the real green x86
  AppImage artifact.
- contracts + 46 unit tests pass.
- Bundle layout confirmed correct against the real artifact: ld-analyse at
  usr/bin/ld-analyse, AAA AppImage at usr/bin/vendor/vhs_decode_auto_audio_align/
  vhs-decode-aaa.AppImage (matches the resolver path); the real bundled AAA
  AppImage runs self-contained (BUNDLED-GREP-AUDIO-OK).

CI run 33170709416 (eafcdb7) triggered to confirm on real CI.
