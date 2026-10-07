# Prompt README — "ld-dropout-correct not found in path" on new Ubuntu/Mint (2026-10-07)

## User prompts
1. "critical bug newer OS's are having issues with tbc-tools not exportimg finding issues with
   'ld-dropout-correct' not found in pathj etc has there been a rename / change over botch ? or a
   packaging boatch too?? how is this happening if CI tests are passing?"
2. "tbc-tools should also be isolasted from any old legacy tools installs..."
3. "The issue was reported on current new Ubuntu and Mint not windows"

Environment: Linux Mint, repo /home/harry/tbc-tools, branch main, HEAD 8addcaa (2026-10-03).

## Commands run and hard-data results

### Repo searches
- `grep` for `ld-dropout-correct` (old name only) across the repo: **only historical matches remain**
  (`development-logs/*`, `test-artifacts/win-x86-job.log`). No functional code, no `.github/`, no
  `scripts/` reference the old binary name. The rename sweep was complete in-tree.
- `grep` `.github` for `dropout-correct`: **zero matches** — no workflow names the tool explicitly;
  packaging copies whole directories (`result/bin/*`).
- `ls src/`: all current tool dirs are `tbc-*` (tbc-dropout-correct etc.).

### Rename history (git log)
- `2ae99e8 2026-09-22` rename chroma tools to tbc-* (merge tbc-process-vbi into one CLI+GUI binary)
- `38badfd 2026-09-22` rename ld-analyse to tbc-analyse
- dropout-correct rename same day (dev log `prompt_readme_dropout-correct-rename_2026-09-22.md`:
  one-commit sweep, 22 files, enums.py `ProcessName.__str__` added)
- PyInstaller self-contained tbc-video-export: `e2c2a0e 2026-07-01` (so every release since v3.2.6
  ships a frozen onefile `tbc-video-export` built from the tagged source)
- **Release tags: latest is `v3.2.9 2026-09-13` — 9 days BEFORE the rename.**
  `git ls-tree v3.2.9:src` shows `ld-analyse`, `ld-chroma-decoder`, `ld-dropout-correct`,
  `ld-process-vbi`... i.e. the published release is entirely pre-rename.

### Runtime resolution code (current main)
- `src/tbc-video-export/src/tbc_video_export/common/utils/files.py` `find_binary(name)`:
  1. `Path(name).is_file()` (CWD-relative!), 2. script/runtime dir, 3. `which(name)` (PATH),
  else raise `FileIOError(f"{name} not in PATH or script dir.")`. **Identical logic at v3.2.9**
  (verified via `git show v3.2.9:...files.py`) — so the literal error text naming `ld-dropout-correct`
  can only come from pre-rename (v3.2.9-era) tbc-video-export code.
- `common/enums.py` `ProcessName.__str__`: `LD_DROPOUT_CORRECT` → "tbc-dropout-correct" (current).
- `file_helper._get_tool_paths()` resolves sibling tools via `find_binary` (PATH-dependent).
- `src/tbc-analyse/exportdialog.cpp` `defaultExecutableSearchDirs()` (~line 346): app dir + up to 4
  parents, CWD + `build/bin`, input-file dir, **`~/.local/bin` + `~/bin`**, then system dirs.
  `resolveVideoExportPath()` tries those candidate dirs, then `QStandardPaths::findExecutable`
  (PATH). **No version/consistency handshake with the tbc-video-export it picks.**
- AppRun (x86 AppImage) exports `PATH="$APPDIR/usr/bin:$PATH"` (also present at v3.2.9).

### Local artifact verification (AppImage built 2026-09-26, post-rename)
- `AppDir/usr/bin/tbc-video-export` + `squashfs-root/usr/bin/tbc-video-export`: 8,724,088-byte ELF
  (PyInstaller onefile), same BuildID, dated Sep 26. Bundle bin contains only new-named tools
  (`.tbc-dropout-correct.real` etc.).
- Frozen binary plaintext grep for both names: 0/0 (PYZ compressed — inconclusive), so instead:
- **Dry-run export executed with the frozen binary + fixtures**:
  `squashfs-root/usr/bin/tbc-video-export --dry-run tests/files/pal_svideo /tmp/tve-dry/pal_svideo`
  → resolves `tbc-dropout-correct` / `tbc-chroma-decoder` from its own dir; plan printed with all
  new names. **Local AppImage is self-consistent post-rename.** (Observation: dry-run printed a
  complete plan but **exited 1**; version string is `0.0.0` — separate anomalies worth follow-up.)

### CI coverage (why it passes)
- `scripts/test-video-export-runtime.py` (ctest `video-export-runtime`): runs a REAL export from
  `build/bin/tbc-video-export` — tool resolution succeeds via script-dir check (build/bin holds
  all new-named binaries), so PATH/isolation behavior is never exercised.
- `ci/verify_linux_bundle.sh` (both x86 AppImage and arm64 release modes): only smoke-tests
  `tbc-video-export --version` / `ffmpeg -version` via AppRun/launcher. `--version` exits during
  argparse before FileHelper construction, so dropout-correct resolution is NEVER reached in CI.
- CI runners are clean ephemeral containers: no legacy `ld-*` suite, no stale `~/.local/bin`,
  no old release mixed in. The mixed-install condition that produces the bug does not exist in CI.
- `ci/check_ci_contracts.py` validates static source snippets, not runtime resolution.

## Findings (verified)
1. The `ld-*` → `tbc-*` rename itself was NOT botched: in-tree it was a complete one-day sweep,
   and the locally built (Sep 26) AppImage is internally consistent (proven by dry-run).
2. The published release situation IS the botch surface: **v3.2.9 (latest) is fully pre-rename;
   current main is fully post-rename; nothing has been released since.** Any machine mixing a
   pre-rename `tbc-video-export` (old pip/pipx `~/.local/bin` install, extracted old tarball,
   legacy ld-decode-tools scripts dir, or a stale PATH entry) with new-named-only binaries fails
   with exactly `ld-dropout-correct not in PATH or script dir.` — because the OLD code is what
   asks for the OLD name, and only NEW binaries exist.
3. Current resolution logic is not isolated from such mixes:
   - `find_binary` consults CWD then PATH after script dir; a stale PATH/CWD tool can be picked.
   - tbc-analyse's candidate dirs deliberately include `~/.local/bin`/`~/bin` (stale user installs
     win over PATH fallback, and can win over bundled siblings when tbc-analyse isn't run from a
     bundle), with no version handshake.
4. CI is green because it never tests tool resolution through a packaged artifact against a
   poisoned PATH/legacy install, and `--version` smoke tests exit before resolution.

## Open questions (asked to user, pending answers)
- Which artifact + version are the Ubuntu/Mint reporters actually running?
- Do those machines have old tbc-tools / legacy ld-decode-tools / pip user-site installs present?
- Exact error text (GUI vs CLI) — is it literally "ld-dropout-correct not in PATH or script dir."?
- Whether to implement isolation hardening + CI poisoned-PATH test now.

## Proposed fix directions (not yet implemented)
- Version handshake in tbc-analyse before launching a resolved tbc-video-export (refuse/replace
  stale ones, prefer bundled sibling), and/or drop `~/.local/bin`/`~/bin` from candidate dirs.
- Demote/remove the CWD-relative `Path(name)` check in `find_binary` (script/runtime dir first).
- Add a real dry-run export smoke test through the packaged AppImage in CI with a poisoned PATH
  (legacy old-named stubs + fake stale `tbc-video-export`) to prove isolation; fix dry-run exit
  code first (currently exits 1 on success).
- Cut a post-rename release so published assets match the new names.

## Update 1 — user-supplied failure log + v3.3.0 asset verification (2026-10-07 ~01:00)

### User input
- Log file: `/home/harry/Downloads/export-fail-06.10.2026-20.13.54.log`; user states the machine
  ("decodepc") is on the latest release, v3.3.0.

### Log contents (verbatim key lines)
- `Timestamp: 2026-10-06T20:13:54.250`, `Failure: Export failed`,
  `Input: /media/decodepc/storage2/decodes/MoniqueBetamaxes/5_Various/vid.tbc`
- `FileIOError: ld-dropout-correct not in PATH or script dir.` (in "Process output")
- Main export dialog log: `tbc-video-export started (PID 1823770)` → same-second
  `FileIOError: ld-dropout-correct not in PATH or script dir.` → `Export failed.`
  → instant failure at FileHelper init (tool resolution), written by tbc-analyse's
  `writeExportFailureLog()` (`src/tbc-analyse/exportdialog.cpp` ~line 5886).

### v3.3.0 release verification (hard data)
- Local repo had no v3.3.0 tag; `git fetch origin --tags` (origin = harrypm/tbc-tools, verified;
  legacy ld-tools upstream untouched) → new tag v3.3.0; fetch also showed remote re-cut
  v3.2.3/v3.2.6/v3.2.9 tags (rejected clobber locally — re-cut mechanism from commit 6e42ae6).
- `v3.3.0` = commit `9ce60e1` "chore(release): prepare v3.3.0" 2026-10-03 14:06:59 UTC,
  == origin/main HEAD. Tag tree verified post-rename: src dirs all `tbc-*` (tbc-dropout-correct
  present, no ld-dropout-correct); `enums.py` `ProcessName.__str__` returns tbc- names.
- `gh release view v3.3.0`: published 2026-10-03T15:59:54Z, target main, 7 platform assets.
- Downloaded `tbc-tools_v3.3.0_linux_x86_64.tar.xz` (146,083,456 bytes) → extracted →
  `tbc-tools-x86_64.AppImage` (147,473,600 bytes, Oct 3 15:18) + scripts.
- `--appimage-extract` → `usr/bin` contains: tbc-analyse, tbc-chroma-decoder,
  tbc-chroma-encoder, tbc-dropout-correct, tbc-efm-handler, tbc-export-metadata,
  tbc-metadata-converter, tbc-process-vbi, tbc-video-export, ld-discmap, ld-disc-stacker,
  ld-lds-converter, efm-* (no ld-dropout-correct).
- Shipped frozen `tbc-video-export --version`: **`0.0.0`** (version embedding broken in the
  PyInstaller release build — vendor version env not passed; also matches local build).
- Shipped frozen `tbc-video-export --dry-run <repo fixture>`: resolves **tbc-dropout-correct** /
  tbc-chroma-decoder from its own `usr/bin`; full plan printed (exit 1 — dry-run exit-code
  anomaly consistent with the local Sep-26 build).

### Conclusion (verified)
- The released v3.3.0 x86_64 artifact is internally consistent and post-rename. It CANNOT
  produce `ld-dropout-correct not in PATH or script dir.` — that string only exists in
  pre-rename (≤v3.2.9) tbc-video-export code.
- Therefore decodepc launched a STALE pre-rename `tbc-video-export` (v3.2.9-era frozen binary,
  old pip/`~/.local/bin` install, or old extracted tarball on PATH) rather than the v3.3.0
  bundled one. Isolation gaps that allow this: `resolveVideoExportPath()` falls back through
  `~/.local/bin`/`~/bin`/PATH with no version/compatibility handshake (and the GUI may not even
  be running from the v3.3.0 bundle on that machine), and `find_binary()` consults CWD then
  PATH after script dir.
- CI remains green because no CI stage exercises resolution against stale/legacy installs
  (clean runners; `--version`-only bundle smoke tests exit before tool resolution; ctest runs
  from the build tree via script-dir resolution).
- Anomalies to fix regardless: frozen binary `--version` = 0.0.0 (blocks any version handshake);
  `--dry-run` exits 1 despite printing a valid plan (blocks adding a dry-run CI smoke test).

### Pending machine-side diagnostics (asked user to run on decodepc)
- `which -a tbc-video-export tbc-analyse ld-dropout-correct tbc-dropout-correct`
- `ls ~/.local/bin ~/bin 2>/dev/null | grep -iE 'tbc-|ld-'`
- `echo $PATH`, location of the tbc-analyse in use (AppImage path? copied binary? nix profile?),
  and any old tbc-tools/ld-decode-tools tarballs extracted on PATH.

## Update 2 — why frozen tbc-video-export reports 0.0.0 (versioning history, 2026-10-07 ~01:30)

User question: "Why was the internal versioning not fixed when tbc-video-export was effectively merged?"

### Version plumbing (code, verified)
- `consts.py`: `__version__ = "0.0.0"` with comment "substituted by poetry-dynamic-versioning when
  doing pyinstaller builds" (upstream Jitterbug build pipeline). Runtime chain:
  `TBC_VIDEO_EXPORT_VENDOR_VERSION` env → `importlib.metadata` (installed wheel) → `__version__`.
- Wrapper `tbc-video-export.in` exports `TBC_VIDEO_EXPORT_VENDOR_VERSION="@APP_VERSION@"` etc.
  (runtime evaluation — correct while the release shipped wrapper+source+host python).

### History (git log, verified)
- `8411dd1 2026-03-19` Vendor tbc-video-export — upstream's poetry-dynamic-versioning / wheel
  publishing pipeline NOT carried over.
- `878b3c1 2026-04-06` "Fix profile mapping and vendored tbc-video-export version metadata" —
  added the TBC_VIDEO_EXPORT_VENDOR_* runtime env mechanism via the CMake wrapper (this WAS the
  merge-era versioning fix; it worked because the wrapper evaluated at runtime).
- `e2c2a0e 2026-07-01` "Make AAA + tbc-video-export self-contained" — PyInstaller --onefile frozen
  binary REPLACED the wrapper in the bundle. Frozen binaries bake `consts.py` at BUILD time;
  no workflow/ci file ever sets `TBC_VIDEO_EXPORT_VENDOR_VERSION` (grep verified: only
  `tbc-video-export.in` and `consts.py` reference it) → `0.0.0` baked into every release since
  v3.2.6.
- `pyinstaller/build_linux.py` imports `dunamai` but only to derive the pre-release stage
  (".99" 4th component); it does NOT call dunamai.from_git() to get the version — even though
  the CI checkout fetches tags (`fetch-depth: 0`, `fetch-tags: true`), so a git-derived version
  was available at build time. Half-wired mechanism.

### Why nothing caught it
- `ci/verify_linux_bundle.sh` `run_smoke_test` asserts only the EXIT CODE of
  `tbc-video-export --version`, never the version VALUE; no test compares the frozen version
  against the release tag.

### Verified contrast on the shipped v3.3.0 AppImage
- C++ tools carry CMake/Nix APP_BRANCH/APP_COMMIT compile definitions and report correctly:
  `tbc-dropout-correct --version` → "tbc-tools - Branch: nix / Commit: 9ce60e1";
  `tbc-chroma-decoder --version` → same.
- Frozen `tbc-video-export --version` → `0.0.0`; ffmpeg mux metadata also gets
  `tbc_tools_version=0.0.0`.
- (Side note: `tbc-analyse --version` cannot run headless — the bundle ships only the `xcb`
  Qt platform plugin; not a versioning issue.)

### Relevance to the isolation bug
- A version handshake between tbc-analyse and a resolved tbc-video-export (the isolation fix)
  is impossible while the frozen binary reports 0.0.0.
- Users/diagnostics cannot tell a stale pre-rename tbc-video-export from a current one via
  `--version` — exactly what prevented identifying the stale binary on decodepc.

### Fix directions for versioning (not yet implemented)
- Pass the release version into the PyInstaller build (set TBC_VIDEO_EXPORT_VENDOR_VERSION from
  vcpkg.json APP_VERSION in the workflow step, or use dunamai.from_git() in build_linux.py as
  the fallback), and extend verify_linux_bundle.sh to assert the frozen `--version` matches the
  release tag.

## Update 3 — proposed fix: build-time version injection for the PyInstaller freeze (2026-10-07 ~01:40)

User asked: "Propose a fix to inject the version metadata at build time for the PyInstaller freeze."

### Research findings feeding the proposal
- `consts.py` is RE-EXECUTED at runtime inside the frozen binary: the env chain
  (`TBC_VIDEO_EXPORT_VENDOR_*` → importlib.metadata → `__version__ = "0.0.0"`) resolves to 0.0.0
  on user machines because nothing sets the env at runtime. Setting an env var at build time
  alone changes nothing — the value must be CAPTURED into the artifact.
- All three freeze scripts exist: `pyinstaller/build_linux.py`, `build_windows.py`,
  `build_macos.py`. Windows/macOS already parse `consts.PROJECT_VERSION` for the exe
  version-file / plist — so the Windows exe file-properties AND macOS
  CFBundleShortVersionString are ALSO 0.0.0 today (same root cause).
- pyproject.toml: upstream poetry-dynamic-versioning substitution targets consts.py — never
  runs in tbc-tools CI (no poetry/pip build; freeze uses PYTHONPATH=src directly).
- All three platform workflows check out with `fetch-depth: 0` + `fetch-tags: true`
  (verified: linux x86/arm64, windows, macos) → dunamai.from_git() works in CI.
- Root CMakeLists APP_VERSION source = vcpkg.json "version" (single source of truth for the
  C++ side); flake.nix passes only APP_BRANCH/APP_COMMIT (self.shortRev) — NOT APP_VERSION.
- macOS workflow already computes `steps.version.outputs.version` (release_tag or git describe)
  for the .app Info.plist — precedent for version plumbing at workflow level.
- flake.nix line ~116 `packageVersion = "3.2.9"` is stale vs vcpkg.json 3.3.0 (derivation label
  only — not passed as -DAPP_VERSION — but version-drift in the same family).
- Windows workflow already runs `tbc-video-export.exe --dump-default-config` on x86_64 — a
  ready-made place to assert `--version`.

### Proposal summary (presented to user; not yet implemented)
1. Runtime-hook capture: shared `pyinstaller/vendor_version.py` helper — resolve
   (env TBC_VIDEO_EXPORT_VENDOR_{VERSION,COMMIT,BRANCH} → dunamai.from_git() → consts fallback
   with LOUD warning), write `build/_vendor_runtime_hook.py` using `os.environ.setdefault`, pass
   `--runtime-hook` to PyInstaller in all three build scripts. consts.py unchanged (reuses the
   existing `_runtime_vendored_version()` `version+commit` composition; runtime override still
   possible; CMake wrapper path unaffected).
2. Workflow env (deterministic single source): export TBC_VIDEO_EXPORT_VENDOR_VERSION from
   vcpkg.json + TBC_VIDEO_EXPORT_VENDOR_COMMIT from `git rev-parse --short HEAD` in the
   PyInstaller steps of build_linux_tools.yml (x86_64 + arm64), build_windows_tools.yml,
   build_macos_tools.yml (macOS: replace/reconcile with its existing git-describe version step).
3. Per-platform extras fixed by the same resolved value: Windows create_versionfile (exe
   properties), macOS CFBundleShortVersionString plist.
4. CI assertions (why it regressed silently): verify_linux_bundle.sh gains an EXPECT_VERSION
   check on the frozen `--version` (workflow exports it from vcpkg.json); Windows asserts
   `--version` output contains the vcpkg.json version; macOS asserts the .app's tbc-video-export
   --version; optional ci/check_ci_contracts.py snippet guard pinning the runtime-hook wiring.
5. Consistency cleanup: sync or derive flake.nix packageVersion from vcpkg.json (currently 3.2.9
   stale).
6. Unlocked follow-up: with real versions baked, `--version` distinguishes stale builds (the
   decodepc case) and the tbc-analyse version handshake (isolation fix) becomes possible.

Awaiting user approval to implement.

## Update 4 — implementation + local validation (2026-10-07 ~11:30)

User approved: "okay lets go with this cleanup" → "continue and cleanup".

### Implemented (10 modified + 1 new file; NOT committed — no commit requested)
- NEW `src/tbc-video-export/pyinstaller/vendor_version.py` — resolve (env
  TBC_VIDEO_EXPORT_VENDOR_{VERSION,COMMIT,BRANCH} → dunamai.Version.from_git() +
  git rev-parse → loud 0.0.0 warning fallback) + write_runtime_hook() generating
  build/_vendor_runtime_hook.py (os.environ.setdefault).
- `build_linux.py` / `build_windows.py` / `build_macos.py` — write_runtime_hook(Path("build"))
  + `--runtime-hook` passed to PyInstaller; consts.PROJECT_VERSION parsing replaced by
  the resolved version (Windows versionfile + macOS plist get the real version too).
- `build_linux_tools.yml` — both PyInstaller steps (x86_64 + arm64) export
  TBC_VIDEO_EXPORT_VENDOR_VERSION (vcpkg.json) + COMMIT (git rev-parse --short HEAD) +
  BRANCH (GITHUB_REF_NAME); both validation steps export EXPECT_VERSION (vcpkg.json).
- `build_windows_tools.yml` — PyInstaller step sets the same env via $env:; new assertion
  that tbc-video-export.exe --version contains the vcpkg.json version (both arches).
- `build_macos_tools.yml` — PyInstaller step exports the env via "$PYTHON_BIN"; the
  "Verify app bundle" launch smoke test now also asserts the version (case prefix-match).
- `ci/verify_linux_bundle.sh` — new check_frozen_exporter_version() (EXPECT_VERSION
  prefix-match on the first --version line; 0.0.0 rejected when unset), called in
  x86-appimage + arm64-release modes.
- `flake.nix` — packageVersion derived from vcpkg.json (was a stale "3.2.9" literal).
- `ci/check_ci_contracts.py` + `ci/tests/test_check_ci_contracts.py` — new guards:
  PYINSTALLER_BUILD_SCRIPT_REQUIRED_SNIPPETS (write_runtime_hook/--runtime-hook on all 3
  build scripts), PYINSTALLER_VENDOR_VERSION_SCRIPT_REQUIRED_SNIPPETS,
  VENDOR_VERSION_ENV_REQUIRED_SNIPPETS (count>=2 linux; present win+mac),
  BUNDLE_VERIFY_VERSION_REQUIRED_SNIPPETS, export EXPECT_VERSION= count>=2; new test
  test_pyinstaller_freeze_bakes_vendor_version.

### Mishap + repair (documented for the record)
- My first insert of the macOS env line mangled the quoting around $PYTHON_BIN; two
  attempted fixes left it in a third mangled state. Repaired deterministically via a
  chr()-built rewrite; verified with <Q>/<BS> safe-dump + boolean checks (correct form
  present, all stray forms absent).
- Initial fallback failure was a dunamai API mismatch: module-level from_git() was
  removed (dunamai 1.26.2); must call Version.from_git() — fixed + contract snippet updated.

### Local validation (hard data)
- py_compile OK (4 pyinstaller/*.py); bash -n OK (verify_linux_bundle.sh); YAML parse OK
  (all 5 touched workflows).
- Guardrails (`ci/run_local_ci_parity.sh --guardrails-only`): actionlint clean, 49/49
  unit tests OK, CI contract checks passed.
- Functional negative tests (check_frozen_exporter_version vs the old Sep-26 AppImage
  that reports 0.0.0): no EXPECT_VERSION → "reports placeholder version 0.0.0 (vendor
  version injection failed)" exit 1; EXPECT_VERSION=3.3.0 → "version mismatch: expected
  3.3.0, got: 0.0.0" exit 1 — the check catches exactly the v3.2.6..v3.3.0 shipping bug.
- End-to-end freeze (venv, PyInstaller 6.22.3, env VERSION=9.9.9-test COMMIT=deadbee
  BRANCH=verify): hook included in the freeze; frozen `--version` → `9.9.9-test+deadbee`
  exit 0; positive check passes (EXPECT_VERSION=9.9.9-test); `--dry-run` fixture resolves
  tbc-dropout-correct/tbc-chroma-decoder/ffmpeg and the mux metadata now carries
  `tbc_tools_version=9.9.9-test+deadbee` (dry-run exit 1 = known pre-existing anomaly,
  unchanged).
- Fallback path (env cleared): resolve_vendor_version() → ('3.2.9', '8addcaa', '') from
  git; write_runtime_hook generated the hook with real values.
- `nix eval .#packages.x86_64-linux.default.version` → "3.2.9" (derived from vcpkg.json
  at this checkout; 3.3.0 once the release-prep commit lands here).

### Cleanup performed
- Removed session scratch: /tmp/{v330-test,tve-dry,tve-dry2,pyi-venv,cfv.sh}, stray
  /tmp/tbc-video-export-*.dropout.db, test freeze artifacts (src/tbc-video-export/
  {build,dist,tbc-video-export.spec}, pyinstaller/__pycache__).
- Left untouched (pre-existing user working dirs): AppDir/, squashfs-root/, release/,
  linuxdeploy*/appimagetool tools, ffmpeg-result-*, .smoke-x86-appimage-runtime.log.
  Flagged for the user: repo-root litter dirs `os/ sys/ shutil/ argparse/ subprocess/`
  (stray python package dirs, likely from an old misdirected pip --target) — review +
  delete separately.

### Pending
- Committed and pushed to origin/main at the user's request ("commit and push").
  GitHub Actions verification still required (per AGENTS.md) — the tests workflow
  (which runs the reusable release workflow, create_release: false, across all
  platforms) must run green before this counts as verified.
- Follow-ups from the investigation (not in this change): tbc-analyse ↔ tbc-video-export
  version handshake + resolution isolation (the decodepc fix), --dry-run exit code 1
  anomaly, decodepc machine-side diagnostics (which/PATH outputs still not provided).
