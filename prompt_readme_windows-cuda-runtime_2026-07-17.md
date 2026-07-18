# Prompt log — Windows CUDA runtime DLL bundling (completion)

**Date:** 2026-07-17
**Repo:** /home/harry/tbc-tools
**Branch:** main
**Topic:** Bundle CUDA 11.8 + cuDNN 8.9 runtime DLLs into the Windows x86_64 release so the ONNX Runtime CUDA EP loads on a clean NVIDIA machine.

## User intent
Complete the remaining work for the Windows x86_64 CUDA runtime DLL bundling:
verify locally, document in AGENTS.md, commit + push, then trigger a Windows
x64 build to confirm the DLLs bundle on CI. (Prior steps — script, fetch/verify,
push to cache, workflow wiring, contract + tests — were already implemented.)

## Commands run + key outputs

### 1. Local verification (all passed)
- `bash -n scripts/windows-cuda-runtime.sh` -> `bash -n OK`
- `python3 -m unittest ci.tests.test_check_ci_contracts -v` -> `Ran 31 tests ... OK`
  (includes new tests: test_windows_cuda_runtime_contract_requires_bundle_step,
   test_windows_cuda_runtime_script_is_a_required_file,
   test_windows_cuda_runtime_script_pins_wheel_versions,
   test_windows_workflow_has_exactly_one_cuda_runtime_bundle_step)
- `python3 ci/check_ci_contracts.py` -> `CI contract checks passed.` (exit 0)
- `bash ci/run_local_ci_parity.sh --guardrails-only` -> exit 0 (contract tests +
  contract check + actionlint on all workflows + bash -n verify_linux_bundle.sh)
- Explicit actionlint on the edited Windows workflow:
  `/tmp/actionlint-bin/actionlint-1.7.12-linux-amd64 -color -shellcheck= \
   .github/workflows/build_windows_tools.yml` -> `ACTIONLINT_EXIT=0`

### 2. Inspected working-tree change set
- `git status --short` -> modified: build_windows_tools.yml, .gitignore,
  ci/check_ci_contracts.py, ci/tests/test_check_ci_contracts.py,
  scripts/cuda-closure-cache.sh; untracked: scripts/windows-cuda-runtime.sh
- `git --no-pager diff -- .gitignore scripts/cuda-closure-cache.sh`:
  - .gitignore: +`win-cuda-cache/` working dir
  - cuda-closure-cache.sh: `cmd_pull` clone -> `git clone --depth 1 --single-branch`
    (shared cache repo now also holds the ~1.6 GB Windows DLL set; full clone too slow)
- Reviewed new workflow step at build_windows_tools.yml:255
  ("Pull + bundle CUDA 11.8 + cuDNN 8.9 runtime DLLs (x86_64 only)", gated
  `matrix.arch == 'x86_64'`, shell bash, runs pull+verify then copies DLLs +
  LICENSES into release/) and the updated verify step at :481 (requires the 8
  CUDA/cuDNN DLLs for x86_64 only).

### 3. AGENTS.md documentation (applied via edit_files)
- Added hard rule bullet to "Important Notes" (now line 129): Windows x86_64
  release must bundle the CUDA 11.8 + cuDNN 8.9 runtime DLLs; arm64 unaffected;
  ~1.6 GB release-size cost; enforced by ci/check_ci_contracts.py.
- Added dedicated "## Windows CUDA runtime DLL bundling" detail section
  (lines 148-160): rationale, scripts/windows-cuda-runtime.sh modes, pinned
  wheel versions (cuda-runtime 11.8.89, cublas 11.11.3.6, cufft 10.9.0.58,
  cudnn 8.9.5.29), the 8 bundled DLLs, license copy, gitignore note, enforcement.
- Re-ran contract checker + 31 unit tests after edit -> still pass.

### 4. Commit + push
- `git add` (7 files: scripts/windows-cuda-runtime.sh, build_windows_tools.yml,
  .gitignore, ci/check_ci_contracts.py, ci/tests/test_check_ci_contracts.py,
  scripts/cuda-closure-cache.sh, AGENTS.md) — excluded unrelated untracked
  log/zip files from prior sessions.
- `git commit -F -` -> `[main af79464] ci(windows): bundle CUDA 11.8 + cuDNN 8.9
  runtime DLLs into x64 release` (7 files changed, 442 insertions(+), 1 deletion(-),
  create mode 100755 scripts/windows-cuda-runtime.sh) with
  `Co-Authored-By: Oz <oz-agent@warp.dev>`.
- `git push origin main` -> `5a7d1e6..af79464  main -> main` (PUSH_EXIT=0)

### 5. Trigger Windows build
- `gh auth status` -> logged in as harrypm, token scopes incl. `workflow`.
- `gh workflow list --all` -> "Build Windows tools" active (id 238418557).
- `gh workflow run "Build Windows tools" --ref main` -> Created workflow_dispatch
  event for build_windows_tools.yml at main.
  Run: https://github.com/harrypm/tbc-tools/actions/runs/29629579254
- `gh run view 29629579254` -> jobs queued:
  - Build tbc-tools (x86_64) (ID 88040496296)  <-- exercises new bundle + verify steps
  - Build tbc-tools (arm64) (ID 88040496307)

## Files changed in commit af79464
- scripts/windows-cuda-runtime.sh (new, 281 lines)
- .github/workflows/build_windows_tools.yml (+38)
- .gitignore (+4)
- ci/check_ci_contracts.py (+49)
- ci/tests/test_check_ci_contracts.py (+52)
- scripts/cuda-closure-cache.sh (+4/-1)
- AGENTS.md (+15)

## Completion criteria (from plan)
- [x] Local tests + contract checks pass with new script + workflow
- [x] Documentation updated with hard rule + detail section
- [x] Commit + push to origin/main
- [ ] CI Windows x64 run confirms DLLs pulled + bundled; verify step passes
      (run 29629579254 in progress)

## Notes / open
- Windows vcpkg build (ffmpeg) is long (~1h); monitor run 29629579254 x86_64 job.
- arm64 job is a regression check (gas-preprocessor insulation already landed).
