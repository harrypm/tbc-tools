### Result
- Script + flake pin-drift assertions + .gitignore guard now on origin/main as commit 86e53c0.
- Full closure already pushed to tbc-tools-ci-cache in prior session; round-trip pull+verify confirmed there.
- Still open: whether to wire pull+restore into the Linux CI workflow / dev docs (not done).

## Command log — session 2026-07-08b (wire pull+restore into Linux CI + contract + docs)

### Goal
User: wire pull+restore into Linux CI / dev docs so CI self-serves the closure from
 tbc-tools-ci-cache instead of cache.nixos.org. "more insulation from external services
 the better."

### Pre-flight hard data
- `grep -A6 nixpkgsLegacy flake.lock` -> rev 50ab793786d9de88ee30ec4e4c24fb4236fc2674,
  narHash sha256-/bVBlRpECLVzjV19t5KMdMFWSwKLtb5RyXdjz3LJT+g=.
- `grep NIXPKGS_REV= scripts/cuda-closure-cache.sh` -> same rev + same sha. EXACT MATCH:
  restored closure will satisfy what `nix build .#` requests (no re-fetch from cache.nixos.org).
- Inspected .github/workflows/{build_linux_tools.yml,tests.yml}, ci/run_local_ci_parity.sh,
  ci/verify_linux_bundle.sh, ci/check_ci_contracts.py, ci/tests/test_check_ci_contracts.py.
  Confirmed: x86_64 build job runs in oraclelinux:8 container w/ --no-daemon nix install
  (so `nix copy --from file://` works without daemon); arm64 job has enableCuda=false.

### Edits (5 files, +150 lines)
- .github/workflows/build_linux_tools.yml: new step "Restore pinned CUDA 11.8 closure
  from tbc-tools-ci-cache" inserted after Nix store cache restore, BEFORE "Create release
  directory"/"Build with Nix" in build-tbc-tools (x86_64) job ONLY. shell: bash,
  continue-on-error: true, set +e + rc checks + ::warning:: fallback to cache.nixos.org.
  NOT added to arm64 job.
- .github/workflows/tests.yml: same step in full-build-test (ubuntu-22.04 x86_64) job,
  after Nix store cache restore, before "Build and test full parity flow".
- ci/check_ci_contracts.py: +TESTS_WORKFLOW, +CUDA_CLOSURE_CACHE_SCRIPT (both required
  files); +LINUX_CUDA_CACHE_REQUIRED_SNIPPETS (step name + pull + restore + tbc-tools-ci-cache);
  +TESTS_CUDA_CACHE_REQUIRED_SNIPPETS; checks against both workflows + exact-count==1 guard
  for the step in build_linux_tools.yml (must NOT appear in arm64 job).
- ci/tests/test_check_ci_contracts.py: +4 tests (linux/tests snippet subset, required
  files exist, exactly-one-step-in-linux-workflow).
- AGENTS.md: +hard rule "Linux x86_64 CI self-serves the pinned CUDA 11.8 closure" +
  "CUDA 11.8 closure cache" section documenting the script commands + pin-match requirement.

### Insulation design
- Primary: pull+restore from harrypm/tbc-tools-ci-cache -> local Nix store -> `nix build .#`
  finds CUDA 11.8 paths locally, does NOT substitute from cache.nixos.org for them.
- Fallback: if cache repo unreachable, continue-on-error + ::warning:: -> build falls back
  to cache.nixos.org. Build succeeds if EITHER service is reachable. Neither is a hard SPOF.
- Non-CUDA paths (qt6/fftw/ffmpeg from nixpkgs unstable) still use cache-nix-action +
  cache.nixos.org; only the at-risk nixpkgsLegacy CUDA 11.8 closure is insulated.
- arm64 untouched (enableCuda=false; an x86_64 closure restore would fail there).

### Verification (hard data, all local)
- `python3 -m unittest -v ci.tests.test_check_ci_contracts` -> Ran 24 tests ... OK
  (4 new: test_linux_cuda_cache_contract_requires_pull_restore_step,
   test_tests_cuda_cache_contract_requires_pull_restore_step,
   test_cuda_closure_cache_script_and_tests_workflow_are_required_files,
   test_linux_workflow_has_exactly_one_cuda_cache_restore_step).
- `python3 ci/check_ci_contracts.py` -> CI contract checks passed. (exit 0)
- `bash ci/run_local_ci_parity.sh --guardrails-only` -> actionlint validated BOTH edited
  workflow YAMLs (no errors) + 24 contract tests OK + contract check passed. (exit 0)
- (Earlier: `bash -n scripts/cuda-closure-cache.sh` -> SYNTAX OK.)

### Commit + push
- `git add` the 5 files; untracked .md/.zip prompt logs left out.
- `git commit -F -` -> [main e0eba78] ci(nix): self-serve pinned CUDA 11.8 closure in
  Linux x86_64 CI, 5 files changed, 150 insertions(+). Co-Authored-By: Oz <oz-agent@warp.dev>.
- `git push origin main` -> 86e53c0..e0eba78 main -> main.

### Open
- First real CI run will exercise the restore step end-to-end on GitHub runners
  (container x86_64 + ubuntu-22.04). Watch for the restore log line
  "CUDA 11.8 closure restored from tbc-tools-ci-cache" or the ::warning:: fallback.
- GitHub Actions Tests/build_linux_tools workflows must pass to confirm (per hard rule:
  build verification is not complete until GitHub Actions succeeds).

# Prompt README — CUDA 11.8 closure caching (2026-07-07)

Goal: pre-empt cache.nixos.org GC / Nixpkgs 25.05 removal of
`cudaPackages_11_8` breaking the GTX-1000-series (Pascal) CUDA builds, by
caching + bundling the pinned CUDA 11.8 closure flake.nix pulls from
`nixpkgsLegacy` (nixos-24.11).

Artifacts:
- `flake.nix` — pin-drift assertions (uncommitted).
- `scripts/cuda-closure-cache.sh` — export / verify / restore / push / pull.
- `tbc-tools-ci-cache` (separate public repo) — git-blob binary-cache store.

## Command log (appended as work progresses)

### Verification: flake pin-drift assertions (hard data)
- `nix eval --impure --raw ...cudaPackages_11_8.cudatoolkit.version` -> `11.8`
  (deprecation warning emitted; pin still provides 11.8).
- `nix eval ...cudnn_8_9.version|gcc11.version` -> `8.9.7.29|11.5.0`
  (asserts' guarded attrs all present on pinned nixpkgsLegacy rev 50ab793).

### Bug found + fixed: bogus `allow-unfree` daemon setting
- `nix --version` -> 2.33.3 ; `nix config show allow-unfree` ->
  `error: could not find setting 'allow-unfree'` (not a Nix daemon setting).
- Small-subset test passed only because gcc11 is FREE; unfree CUDA outPath
  eval failed without a real allow-unfree mechanism.
- Fix: `scripts/cuda-closure-cache.sh` out_path()/package_version() now use
  `nix eval --impure --raw --expr` with `builtins.fetchTree` (github type,
  exact rev) + `config.allowUnfree = true`; removed bogus NIX_CONFIG line.
- Verified: unfree `cudaPackages_11_8.cudatoolkit.outPath` -> `/nix/store/gqs6xflqwp8cmh8ykmc8knzirzj00sy1-cuda-merged-11.8`.

### Round-trip test (small subset: gcc11)
- `bash scripts/cuda-closure-cache.sh export --out /tmp/cuda-cache-test --subset gcc11`
  -> 29 store paths, 65M cache, 0 split nars, no warnings.
- `bash scripts/cuda-closure-cache.sh verify --out /tmp/cuda-cache-test`
  -> narinfos=29 plain_nars=29 split_nars=0, nix can read cache, verify OK.
- `bash scripts/cuda-closure-cache.sh restore --out /tmp/cuda-cache-test`
  -> reassemble (none split) + `nix copy --from` -> 29 paths importable.

### Open: full closure + push + CI integration (needs user decision)
- Full closure: 43 paths, not all realised locally; `nix path-info --closure-size`
  returned 0 bytes (cannot size without downloading the missing paths).
- Free disk: 492G on / (ample).
- Push target: public repo harrypm/tbc-tools-ci-cache (multi-GB, hard to reverse).
- NOT done autonomously — awaiting confirmation on scope/approach.

## Command log — session 2026-07-08 (commit + push script/flake to tbc-tools)

### Pre-commit hard-data verification
- `git status --short` -> `M flake.nix`, `?? cuda-cache/` (1.6G), `?? scripts/cuda-closure-cache.sh`, plus untracked prompt logs/zips.
- `git --no-pager diff flake.nix` -> 3 new asserts: cudaPackages_11_8 present, complete (cudatoolkit+cudnn_8_9+gcc11), toolkit version has prefix "11.8".
- `bash -n scripts/cuda-closure-cache.sh` -> SYNTAX OK; `-rwxrwxr-x` (executable bit set).
- `nix eval --raw .#default.name` -> `ld-decode-tools-3.2.4` (assertions fire + pass on x86_64-linux where enableCuda=true; deprecation warning re: Nixpkgs 25.05 removal of <12.0 CUDA confirms the threat model).

### Safety: keep 1.6G working cache out of main repo
- Appended to `.gitignore` under "# Nix artefacts":
  `# CUDA 11.8 closure cache (lives in the dedicated tbc-tools-ci-cache repo,
  # not here). Created by scripts/cuda-closure-cache.sh.` + `cuda-cache/`

### Staged + committed (only 3 files)
- `git add scripts/cuda-closure-cache.sh flake.nix .gitignore`
- `git --no-pager diff --cached --stat` -> .gitignore +4, flake.nix +13, scripts/cuda-closure-cache.sh +322 (339 insertions). No prompt logs / zips / cuda-cache staged.
- `git commit -F -` (HEREDOC) -> `[main 86e53c0] ci(nix): cache pinned CUDA 11.8 closure + add pin-drift assertions`, 3 files changed, 339 insertions(+), create mode 100755. Co-Authored-By: Oz <oz-agent@warp.dev>.

### Pushed
- `git push origin main` -> `3504262..86e53c0  main -> main` (to https://github.com/harrypm/tbc-tools.git).

### Result
- Script + flake pin-drift assertions + .gitignore guard now on origin/main as commit 86e53c0.
- Full closure already pushed to tbc-tools-ci-cache in prior session; round-trip pull+verify confirmed there.
- Still open: whether to wire pull+restore into the Linux CI workflow / dev docs (not done).


## Command log — session 2026-07-09 (fix Win arm64 Release failure: gas-preprocessor.pl 429)

### User report
"all builds work but Win Arm64" — Release run 28986645614 (workflow_dispatch, sha 626dbeb)
failed only on "Build Windows binaries / Build tbc-tools (arm64)" job 86017432909.
My e0eba78 Tests run Win arm64 job (86010244921) had SUCCEEDED (warm cache).

### Root cause (verified from raw log /tmp/win-arm64-log.txt + vcpkg source @ d30fdf55cf)
- Log lines 1105-1124 / 1226-1238: vcpkg ffmpeg:7.1.1#4 arm64-windows portfile:606 calls
  vcpkg_find_acquire_program(GASPREPROCESSOR) — ONLY for arm/arm64-windows (x64 uses NASM,
  ffmpeg portfile.cmake:27 + 604-606). That downloads gas-preprocessor.pl from
  raw.githubusercontent.com/FFmpeg/gas-preprocessor/9309c67.../gas-preprocessor.pl.
- Log line 1106: `error: ...status code 429` (raw.githubusercontent.com rate-limited).
- -> vcpkg_download_distfile FATAL "Download failed, halting portfile" -> "building
   ffmpeg:arm64-windows failed with: BUILD_FAILED" -> "vcpkg install failed" -> CMake fail.
- NOT my CUDA work; NOT an arm64 incompatibility; transient external 429.
- vcpkg_download_distfile.cmake (fetched @ pin): if ${DOWNLOADS}/<filename> exists AND
  file(SHA512)==download_sha512 -> "Using cached <filename>" + return (SKIP network fetch).
- GASPREPROCESSOR program def (fetched via gh api): commit_id=9309c67acb..., download_filename
  =gas-preprocessor-9309c67a.pl, download_sha512=b4749cf8...d8ad4f.
- Mirror SHA512 check (local): raw.githubusercontent, cdn.jsdelivr.net, github.com/raw ALL
  serve byte-identical content == b4749cf8...d8ad4f. MATCH x3.

### Fix (commit d8b4e97, +115 lines, 4 files)
- .github/workflows/build_windows_tools.yml: new step "Pre-fetch gas-preprocessor.pl for
  arm64 ffmpeg (insulate from raw.githubusercontent.com 429)" before Run CMake, gated
  if: matrix.arch == 'arm64', continue-on-error: true. Parses pinned vcpkg's
  GASPREPROCESSOR program def for commit_id+download_sha512 (auto-tracks VCPKG_COMMIT
  bump), computes filename gas-preprocessor-<commit8>.pl, pre-fetches from jsDelivr ->
  github.com/raw -> raw.githubusercontent.com (4 attempts, backoff), SHA512-validates,
  places at ${VCPKG_ROOT}/downloads/<filename> so vcpkg skips its network fetch.
- ci/check_ci_contracts.py: +WINDOWS_GAS_PREPROCESSOR_REQUIRED_SNIPPETS (step name,
  GASPREPROCESSOR, jsdelivr, raw.githubusercontent, Get-FileHash SHA512, arm64 gate) +
  exact-count==1 guard. +2 unit tests.
- AGENTS.md: +hard rule.
- Rebased onto incoming 4c15dcd (v3.2.5 prep) + n00mkrad PRs #25/#26; contract check
  passed post-rebase. Pushed 4c15dcd..d8b4e97 main -> main.

### Verification (run 29043359522, sha d8b4e97, push-triggered Tests)
- Contract: 26/26 unit tests OK; check_ci_contracts.py passed; guardrails (actionlint)
  validated Windows workflow YAML. EXIT 0.
- Win arm64 job 86207047123: conclusion=success, status=completed.
- Log line 188: built d8b4e97 (the fix). Line 863: "Pre-fetched gas-preprocessor.pl from
  https://cdn.jsdelivr.net/gh/FFmpeg/gas-preprocessor@9309c67.../gas-preprocessor.pl
  (sha512 OK) -> C:\a\tbc-tools\tbc-tools\vcpkg\downloads\gas-preprocessor-9309c67a.pl".
- grep "Downloading https://raw.githubusercontent.com/FFmpeg/gas-preprocessor" -> 0
  (vcpkg never hit raw.githubusercontent.com). grep 429/Download failed/BUILD_FAILED -> 0.
- HONEST CAVEAT: this run's ffmpeg arm64 was a binary-cache HIT (95.5 ms) so vcpkg did
  NOT build ffmpeg from source and did not need to consume gas-preprocessor.pl. The
  cold-cache from-source consumption path is proven by vcpkg source (skip-on-hash-match)
  + mirror SHA512 verification, NOT by this run's logs. 100% runtime proof of the
  from-source path would require a cold arm64 vcpkg cache run (bump cache key / new pin).

### Open
- The v3.2.5 TAG (4c15dcd) does NOT include this fix; re-running the Release for v3.2.5
  would still 429 on a cold arm64 cache. The fix is on main (d8b4e97). A v3.2.6 (or
  re-cut) release would pick it up.
- Optional further insulation: mirror gas-preprocessor.pl into tbc-tools-ci-cache so the
  pre-fetch has a zero-external-dependency primary source (not done; jsDelivr+github/raw
  triple mirror deemed sufficient).

## Command log — session 2026-07-09b (fix CUDA closure restore signature failure)

### User report
Release run 29046813179 (workflow_dispatch, d8b4e97), "Build Linux binaries / Build tbc-tools (x86_64)"
job 86218020149 logged: "CUDA closure restore failed (rc=1); falling back to cache.nixos.org
for CUDA 11.8 paths." Build still succeeded via fallback, but insulation was a no-op.

### Root cause (verbatim from job 86218020149 log)
- Lines 1613-1619: pull + split-nar reassembly SUCCEEDED (sha256 OK).
- Line 1620: "importing closure into the local Nix store" -> nix copy --from file://$cache
- Line 1624: `error: cannot add path '/nix/store/24mvv71hs7nixmczdz99y0ah6kfwr4ik-cuda_cuxxfilt-11.8.86'
  because it lacks a signature by a trusted key`
- Line 1625: ##[warning] fallback. The tbc-tools-ci-cache binary cache is UNSIGNED; Nix
  requires trusted-key signatures for file:// substituters by default (require-sigs=true).
  Structural block, not transient -> CI silently fell back to cache.nixos.org every time.

### Local verification (Nix 2.33.3, isolated store /tmp/testnix to avoid touching live store)
- REPRODUCE (current cmd, default require-sigs): "cannot add path ... lacks a signature", rc=1.
- FIX (--option require-sigs false): "copying path '...' from 'file://...'", rc=0, imported.
- Full script restore end-to-end via nix wrapper -> isolated store: reassemble split nars
  (sha256 OK) + "copying path ..." + "restore complete: 1 paths now importable", rc=0.
- Safety: cache is local/self-built; reassemble_all already SHA256-verifies every nar;
  require-sigs=false only lifts the trusted-key sig check (Nix still verifies each nar's
  content hash vs narinfo narHash).

### Fix (commit 5a7d1e6, +30/-1, 4 files)
- scripts/cuda-closure-cache.sh cmd_restore: nix copy --from file://$cache --option
  require-sigs false "${paths[@]}" + safety comment.
- ci/check_ci_contracts.py: check_contains(CUDA_CLOSURE_CACHE_SCRIPT, "--option require-sigs
  false") so the silent-fallback regression can't return. +1 unit test (27 total).
- AGENTS.md: note on unsigned-cache require-sigs restore rationale.
- Pushed d8b4e97..5a7d1e6 main -> main (clean, no rebase needed).

### CI verification (Tests run 29051535862, sha 5a7d1e6, job 86233554285)
- conclusion=success. Store was COLD (100 paths imported, not a warm no-op).
- grep counts: cannot add path=0, ##[warning]CUDA closure=0, lacks a signature=0, BUILD_FAILED=0.
- copying path '...' from 'file://...cuda-cache' = 100 (ALL manifest paths imported from
  tbc-tools-ci-cache, incl. the exact cuda_cuxxfilt-11.8.86 path that previously failed).
- Line 1737: "cuda-closure-cache: restore complete: 100 paths now importable" (real stdout).
- Line 1738: "CUDA 11.8 closure restored from tbc-tools-ci-cache; nix build .# will use local
  store paths." (real stdout 21:36:10; line 1608 was the echoed step body at 21:35:11).
- => Insulation now ACTUALLY works: CI self-serves CUDA 11.8 from tbc-tools-ci-cache, not
  cache.nixos.org. No separate Release trigger needed (cold-store import path proven here).
