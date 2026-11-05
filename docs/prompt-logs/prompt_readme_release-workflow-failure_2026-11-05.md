# Prompt README - Release workflow failure analysis (v3.3.1 dispatch)

Date: 2026-11-05
Repo: /home/harry/tbc-tools (main @ b3ffbe6 at start, working tree on top)

## User request (input)

Analyze why the Release workflow failed (manual dispatch, tag v3.3.1, from
main @ b3ffbe6).

## Analysis (verified against run logs + git history)

- Failed run 42088225784 (Release, workflow_dispatch): only the "Resolve
  release tag" job ran; "Update codebase version tags" step exited 1 with
  "Failed to update packageVersion in flake.nix". All build/upload jobs
  skipped as dependencies. No tag v3.3.1 existed, so this was the fresh
  manual-release path.
- Root cause: the version-tagging step in release.yml rewrites the literal
  `packageVersion = "X.Y.Z";` in flake.nix. That literal no longer exists:
  commit 8898af3 ("build(pyinstaller): bake vendor version into frozen
  tbc-video-export") changed it to
  `packageVersion = (builtins.fromJSON (builtins.readFile ./vcpkg.json)).version;`
  making vcpkg.json the single source of truth (flake.nix comment says so;
  CMakeLists.txt also reads APP_VERSION from vcpkg.json when unset). The
  workflow step was never updated for this, re.subn found 0 replacements and
  raised SystemExit before touching vcpkg.json.
- Side observations (not the failure, noted for context):
  - release.yml is CRLF throughout (461 lines); the edit preserved that.
  - vcpkg.json was at 3.3.0 (last release v3.3.0 via 9ce60e1 when the literal
    still existed); the failed dispatch was for v3.3.1.

## Fix (release.yml "Update codebase version tags" step)

- vcpkg.json is bumped first (it is the single version source of truth).
- flake.nix: if it still carries a literal packageVersion it is rewritten
  too (legacy layout); otherwise the step verifies the vcpkg.json derivation
  line exists, and fails loudly if neither is present - so a future
  restructure cannot silently ship releases with a stale version.

## Verification (hard data)

- Extracted the updated step's python from release.yml (indentation + CRLF
  stripped, mirroring what the YAML runner does) and ran it against copies:
  - Current flake.nix (derivation): exit 0, vcpkg.json "version" bumped to
    3.3.1, flake.nix byte-identical (diff-exit 0).
  - Legacy literal flake.nix (packageVersion = "0.0.0";): exit 0, the literal
    rewritten to 3.3.1 and vcpkg.json bumped.
  - The neither-literal-nor-derivation guard raises SystemExit (simple
    containment check, same pattern as the verified paths).
- python3 ci/check_ci_contracts.py: passed (release.yml is
  contract-checked).

## Pending

- Commit/push only when the user asks; then re-dispatch the Release workflow
  (workflow_dispatch, version v3.3.1) - the failed run left no tag and no
  release, so a re-run starts clean.
- GitHub Actions parity for the pushed head.
