# Prompt readme — vhs-teletext vendor bundling fix (Windows + macOS)

Date: 2026-07-20
Repo: C:\Users\Harry\tbc-tools (branch: main)
Head before work: eb9d5e7 (pulled to f4244ab at start of prompt)

## Prompt inputs (user requests, in order)

1. `git pull` (context: sanity check vhs-teletxt integration).
   - Result: fast-forward eb9d5e7..f4244ab, 67 files / ~5013 insertions. No
     `vhs-telet*` files in the diff (integration predates this pull).
2. "I want to sanity check vhs-teletxt integration" — locate the integration
   and understand its surface area.
3. End-user report: "Critical: Teletext Export Failed: Could not locate
   vendored vhs-teletext runtime directory." on Windows.
4. User selected scope: "b & c" = (b) apply Windows + macOS fixes,
   (c) add ci/check_ci_contracts.py guard.

## Root cause (verified against source)

- Error string: `src/ld-process-vbi/teletextintegration.cpp:383`, emitted when
  `resolveTeletextVendorDirectory()` (teletextintegration.cpp:75-95) returns
  empty. Resolver validates each candidate by checking for
  `teletext/__main__.py` inside it. Candidates:
  1. `TELETEXT_VENDOR_DIR` compile macro = build-machine source path
     (src/ld-process-vbi/CMakeLists.txt:15-18) — never on end-user machine.
  2. `<appDir>/vendor/vhs-teletext`
  3. `<appDir>/../vendor/vhs-teletext`
  4. `<appDir>/../../vendor/vhs-teletext`
- Invocation path: ld-analyse `on_actionProcess_VBI_triggered()`
  (src/ld-analyse/mainwindow.cpp:4657) shells out to `ld-process-vbi` with
  `--teletext-html-dir` (mainwindow.cpp:4722-4725); export runs inside
  `ld-process-vbi.exe`, fails the resolver, error relayed via
  `runExternalToolWithProgress` -> "Process failed" dialog
  (mainwindow.cpp:4730-4739).
- Windows packaging gap: `.github/workflows/build_windows_tools.yml`
  "Copy binaries to release directory" (line 228) flattens only *.exe/*.dll
  to `release\`, dropping the CMake POST_BUILD `build\bin\vendor\vhs-teletext`
  tree (src/ld-process-vbi/CMakeLists.txt:20-25). An AAA restore step exists
  (lines 229-243) but no equivalent for vhs-teletext. The "Verify required
  Windows runtime files" step (lines 484-526) did not check for the vendor
  tree, so the gap was uncaught.
- macOS latent gap: `.github/workflows/build_macos_tools.yml` "Build with Nix"
  loop (lines 97-103) uses `[ -f "$item" ]` which skips the
  `result/bin/vendor` directory; AAA is restored explicitly (lines 198-206)
  but vhs-teletext was not. Would fail the same way on macOS.
- Linux AppImage is NOT affected: build_linux_tools.yml (lines 141-146) uses
  `[ -e "$item" ]` + `cp -a`, which copies `result/bin/vendor/` recursively
  into `AppDir/usr/bin/vendor/`. No Linux change needed.

## Edits applied (all in this prompt)

### 1. `.github/workflows/build_windows_tools.yml`
- Added step "Copy vhs-teletext vendor payload to release directory" after the
  AAA vendor copy step. Copies `build\bin\vendor\vhs-teletext` ->
  `release\vendor\vhs-teletext` with a throw-on-missing guard for
  `teletext\__main__.py`. Not arch-gated (arch-independent Python source,
  same as AAA).
- Extended "Verify required Windows runtime files" `$required` list with:
  `release\vendor\vhs-teletext\teletext\__main__.py`,
  `release\vendor\vhs-teletext\misc\teletext-noscanlines.css`,
  `release\vendor\vhs-teletext\misc\teletext2.ttf`,
  `release\vendor\vhs-teletext\misc\teletext4.ttf`.

### 2. `.github/workflows/build_macos_tools.yml`
- Added vhs-teletext restore block inside "Create app bundle structure" step
  (after the AAA block): copies `result/bin/vendor/vhs-teletext` ->
  `dist/tbc-tools.app/Contents/MacOS/vendor/vhs-teletext` with a
  missing-payload `exit 1`.
- Extended "Verify app bundle" step with a check for
  `dist/tbc-tools.app/Contents/MacOS/vendor/vhs-teletext/teletext/__main__.py`.

### 3. `ci/check_ci_contracts.py`
- Added `WINDOWS_TELETEXT_VENDOR_REQUIRED_SNIPPETS` and
  `MACOS_TELETEXT_VENDOR_REQUIRED_SNIPPETS` tuples.
- Added `main()` checks: snippet presence + exact-count-==1 guards on the
  Windows copy step ("Copy vhs-teletext vendor payload to release directory")
  and the macOS restore echo ("Bundled vhs-teletext vendor payload to
  $TELETEXT_VENDOR_DST").

### 4. `ci/tests/test_check_ci_contracts.py`
- Added 4 tests:
  test_windows_teletext_vendor_contract_requires_bundle_step,
  test_windows_workflow_has_exactly_one_teletext_vendor_step,
  test_macos_teletext_vendor_contract_requires_restore,
  test_macos_workflow_has_exactly_one_teletext_vendor_restore.

## Commands run (this prompt)

| Command | Result |
|---|---|
| `git pull` (user-ran precommand) | fast-forward eb9d5e7..f4244ab, 67 files |
| `python ci\check_ci_contracts.py` | "CI contract checks passed." (exit 0) |
| `python -m unittest -v ci.tests.test_check_ci_contracts` | Ran 38 tests in 0.078s; OK (all pass, incl. 4 new) |
| `python -c "import yaml; ..."` (parse 2 workflows) | both OK (no YAML structural errors) |

## Validation status

- CI contract checker: PASS.
- CI contract unit tests: PASS (38/38, including 4 new teletext vendor tests).
- YAML parse of both edited workflows: PASS.
- actionlint: NOT run locally (not installed on this Windows host); runs in CI
  via `ci/run_local_ci_parity.sh --guardrails-only` (tests.yml ci-guardrails).
- Full release build (Windows + macOS): NOT run locally — requires CI runners.
  The packaging steps will be exercised by the reusable workflows triggered
  from tests.yml (build-windows-release-style / build-macos-release-style).

## Not yet done / follow-up

- No commit made (per default: do not commit unless user asks).
- No restore-point zip created (rule udQirjAOEYGyA029HzncJp triggers only when
  user confirms the fix as "fully fixed/working"; awaiting real-data
  confirmation from a Windows release build / end user).
- Linux bundle verifier (ci/verify_linux_bundle.sh) was NOT extended with a
  teletext vendor `require_path` — Linux AppImage already bundles the tree
  correctly (no bug there). Could be added later for parity/defense-in-depth.
