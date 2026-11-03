# Prompt README — Release workflow fix + v3.2.9 re-cut (2026-09-23)

## User prompt
> "figure out why releases are breaking and not running..."
> Answer: "Fix workflow + re-cut v3.2.9 from current main" (via clarifying question)

## Root cause (hard data)
- Two Release dispatches (runs 35912991113, 35928311317) failed in ~3–4 min at
  "Resolve and validate release tag" with:
  `Release integrity check failed: source commit 1c4efeb != tag commit 24269a1 for v3.2.9`.
- `v3.2.9` (annotated tag → `24269a1` "chore(release): prepare v3.2.9") was created by
  yesterday's successful release run. `main` then moved ahead (docs readme,
  dropout-correct rename, CI ffmpeg fix), so re-dispatching with
  `allow_existing_tag_rebuild=true` still left the tag on the old commit — the re-cut
  path skipped the tag-creation steps entirely, and the source-vs-tag integrity check
  (correctly) refused to build a stale tag. No v3.2.9 release object existed
  (latest published was v3.2.8).
- Version files (`flake.nix` `packageVersion`, `vcpkg.json`) already said 3.2.9, so a
  re-cut needs no new prep commit — just a re-pointed tag.

## Fix (`ci(release): re-point stale tag to HEAD on allow_existing_tag_rebuild re-cuts`, commit `6e42ae6`)
- `.github/workflows/release.yml` "Detect existing tag and release" step: when the tag
  exists and `allow_existing_tag_rebuild=true`, delete the stale tag (remote
  `git push origin :refs/tags/…` + local `git tag -d`) and fall through as a fresh cut
  (`tag_exists=false`), so version bump → prep commit (no-op here) → tag creation at
  current HEAD → publish all run. Orphaned-release cleanup now runs for both the
  never-existed and just-deleted cases. Refusal without the flag is unchanged.

## Validation (hard data)
- YAML parse OK; extracted step script passes `bash -n`; `ci/check_ci_contracts.py`
  passed; `ci.tests.test_check_ci_contracts` OK.
- Re-dispatch (run 35947241418, create_release=true, version=3.2.9,
  allow_existing_tag_rebuild=true): "Resolve release tag" ✓ in 1m55s;
  `v3.2.9` now → `6e42ae6` (current main HEAD). All six platform build jobs running.

## Status
Release re-cut v3.2.9 in progress (builds take ~1–2 h; upload job publishes assets).
Note: the release-notes generator's NOISE_PATTERNS already hides
"chore(release): prepare" commits; no release-notes changes were needed.
