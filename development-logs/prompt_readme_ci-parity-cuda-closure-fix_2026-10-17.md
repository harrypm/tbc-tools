# Prompt readme — CI parity CUDA closure fetch fix (2026-10-17)

Prompt: "fix the build issue"

## Diagnosis (hard data)

The Tests run for the SECAM fixture commit (38805899161, push 24836e4) failed
at the "Full Linux build and test parity" job (3m31s; healthy runs take
13m39s). Job log (fetched via the GitHub jobs API with
`--allow-escape-sequences` after `gh run view --log` refused while the run was
in progress):

- `nix develop` could not build the devShell env:
  `error: builder for '...-cuda_nvrtc-linux-x86_64-11.8.89-archive.tar.xz.drv'
  failed ... cannot download cuda_nvrtc-linux-x86_64-11.8.89-archive.tar.xz
  from any mirror` -> cuda_nvrtc-11.8.89.drv -> cuda-merged-11.8.drv ->
  nix-shell-env.drv failed.
- Every other CUDA tarball fetch in that job succeeded; only nvrtc failed.
- Root chain of causes:
  1. flake devShells.default pulls the CUDA 11.8 toolchain closure
     (`cudaPackages.cudatoolkit` = cuda-merged + nvcc + libs + cudnn).
  2. Those tarball FODs are unfree -> never substituted from cache.nixos.org;
     first realisation must download from developer.download.nvidia.com.
  3. The actions nix store cache had been evicted (7-day no-access eviction:
     last parity save was Oct 7; the run was Oct 15), so the whole closure
     had to be fetched; NVIDIA's CDN (Akamai) failed for nvrtc from the
     runner IP.
  4. Verified from here: the tarball URL is live
     (HTTP 200, 29,507,552 bytes) — transient CDN failure from the runner,
     but a structural exposure: the tests job has no closure insulation
     (only the CUDA-plugin publish job restores the closure).
- The commit under test is unrelated: the same tree passed the Release-style
  Linux x86_64 build (17m26s) in the same run, and the full parity flow
  (fresh configure + fresh build + ctest 33/33) passes locally.
- LDCHROMA_ENABLE_CUDA defaults ON, and the parity devShell carries nvcc, so
  the parity build compiles the CUDA kernel path — CUDA cannot simply be
  stripped from the CI shell without losing that coverage.

## Fix

`.github/workflows/tests.yml` (parity job): added `id: nix-cache` to the
"Restore and save Nix store cache" step and a conditional best-effort
insurance restore after it, modeled on the publish job's pattern:

- Gate: `steps.nix-cache.outputs.hit-primary-key != 'true'` — runs on a total
  miss AND on a prefix-fallback restore of an older cache (the failed run's
  exact case; cache-nix-action exposes `hit-primary-key` distinct from `hit`).
- Body: `cuda-closure-cache.sh pull --out ./cuda-cache` then `restore` —
  imports the pinned CUDA 11.8 built closure (100 store paths incl.
  cuda-merged-11.8, cuda_nvrtc-11.8.89-lib/dev/static, cuda_nvcc, cudnn 8.9.7,
  gcc 11.5) from `harrypm/tbc-tools-ci-cache` into the local store, so
  `nix develop` needs no NVIDIA fetches. continue-on-error + ::warning::
  fallback to NVIDIA/cache.nixos.org, same as publish_cuda_plugin.yml.
- Verified the mirror actually holds the closure: manifest.txt (100 paths)
  lists cuda-merged-11.8 + nvrtc + nvcc + cudnn + gcc; the manifest pin
  matches flake.lock's nixpkgsLegacy rev
  (50ab793786d9de88ee30ec4e4c24fb4236fc2674) exactly. The missing nvrtc
  *tarball-FOD* narinfo is irrelevant: the *built* outputs are cached, so
  nothing needs to be built or fetched.

`AGENTS.md`: updated the CUDA 11.8 closure cache note — the closure is now
also consumed by the Tests parity job as a conditional insurance restore
(was documented as publish-job-only).

## Commands run

- gh run list/view 38805899161; job log via
  `gh api repos/harrypm/tbc-tools/actions/jobs/116667495462/logs
  --allow-escape-sequences` -> /tmp/parity-job.log (grep for the errors above)
- curl -sI the nvrtc tarball URL (derived via `nix show-derivation` of the
  failing drv) — HTTP 200 from here
- gh api cache-nix-action@v7 action.yml outputs (hit / hit-primary-key /
  hit-first-match semantics)
- gh api tbc-tools-ci-cache manifest.txt + narinfo existence checks
- python3 check flake.lock nixpkgsLegacy rev vs script pin; grep .gitignore
  cuda-cache/
- Local reproduction (all passed, proving the code was fine):
  `bash ci/run_local_ci_parity.sh --build-test-only` (33/33) and a completely
  fresh-dir flow: `nix develop -c cmake -S . -B /tmp/parity-fresh ...` +
  `ninja -C /tmp/parity-fresh` + `ctest --test-dir /tmp/parity-fresh` (33/33)
- Validation of the fix: `bash ci/run_local_ci_parity.sh --guardrails-only`
  (actionlint + contract unit tests + check_ci_contracts.py + bash -n) —
  exit 0, all ok

## Result / validation

(to be updated after the pushed fix's Tests run completes — the parity job
is expected to restore the closure from tbc-tools-ci-cache and go green)
