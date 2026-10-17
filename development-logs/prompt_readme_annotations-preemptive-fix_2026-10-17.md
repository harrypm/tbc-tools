# Prompt readme — pre-emptive annotation warnings fix (2026-10-17)

Prompt: "pre-emtivly fix the annotation warnings"

## Annotations found (green Tests run 39014418365, via `gh run view`)

1. `! CUDA closure restore failed (rc=1); falling back to NVIDIA/cache.nixos.org.`
   (Full Linux build and test parity) — my insurance step's restore FAILED on
   the runner; the run passed only via the NVIDIA fallback, so the insurance
   was not actually working.
2. "The ubuntu-latest label will migrate to Ubuntu 26 beginning October 19,
   2026" (actions/runner-images#14748) — on resolve-tag and the container
   Linux x86_64 build; migration is 2 days away.
3. "Due to capacity constraints, jobs targeting macOS arm64 runners may
   experience longer queue times" — GitHub-side notice, not actionable
   in-repo.

## Diagnosis (hard data)

**1. Restore failure** — parity job log (jobs API, --allow-escape-sequences):
`error: cannot add path '...-libcusparse-11.7.5.86' because it lacks a
signature by a trusted key`. Root cause: tests.yml installs Nix via
`cachix/install-nix-action@v27` = **multi-user daemon**; the runner user is
untrusted, so the daemon refuses the client-side `--option require-sigs
false` (a trusted setting). Proof it is the install mode, not the script:
publish_cuda_plugin.yml installs Nix with `--no-daemon` (single-user) and its
Aug-19 run's restore SUCCEEDED with the identical command
("restore complete: 100 paths now importable").

## Fixes

1. `scripts/cuda-closure-cache.sh` `cmd_restore`: on plain-copy failure,
   retry the identical `nix copy --from ... --option require-sigs false` as
   root via passwordless sudo (root is trusted by the daemon; single-user
   installs and the publish job's --no-daemon install take the plain path).
   Die with a clear message if sudo is unavailable. The contract-required
   `--option require-sigs false` string remains.
2. Pinned all 5 `ubuntu-latest` jobs to `ubuntu-24.04` (today's ubuntu-latest
   == 24.04, so zero behaviour change now; no surprise Ubuntu 26 migration on
   Oct 19): release.yml resolve-tag + upload-release-assets,
   build_linux_tools.yml Build tbc-tools (x86_64), publish_cuda_plugin.yml
   both publish jobs. tests.yml already uses ubuntu-22.04 (unaffected).
3. AGENTS.md closure-cache section: documented the daemon-mode root-retry.

## Commands run

- gh run view 39014418365 / 39016274995 annotations; jobs API logs for the
  parity job (restore failure) and publish job 95999237351 (Aug-19 restore
  success — the differential proof)
- grep runs-on across all workflows (5 ubuntu-latest uses; ubuntu-22.04 in
  tests.yml; ubuntu-24.04-arm in build_linux_tools)
- Compare Install Nix steps: tests.yml (install-nix-action@v27, daemon) vs
  publish_cuda_plugin.yml (curl install --no-daemon)
- bash -n scripts/cuda-closure-cache.sh — OK
- `bash ci/run_local_ci_parity.sh --guardrails-only` — 49 unit tests ok,
  exit 0 (actionlint + check_ci_contracts.py + bash -n all pass)

## Result / validation

(to be updated: commit pushed, the primary nix store cache entry deleted to
force a cache-miss parity run that exercises the sudo-restore path and the
ubuntu-24.04-pinned release-style jobs end-to-end)
