# Prompt README - Issue #30: decode-orc .tbcy/.tbcc filenames will not export

Date: 2026-11-02
Repo: /home/harry/tbc-tools (main @ 4c2c0bf at start, working tree on top)
Issue: https://github.com/harrypm/tbc-tools/issues/30 (opened by alexandeeznuts3505)

## User request (input)

Fix the export pipeline for decode-orc based filenames: exporting (.tbcy) and (.tbcc)
files does not work because tbc-video-export expects (.tbc) and (_chroma.tbc).
Stated fix: let tbc-video-export recognize tbcy as luma and tbcc as chroma.

## Research findings (verified against code)

- decode-orc Y/C ground truth (from /home/harry/decode-orc-latest,
  orc/plugins/stages/tbc_source): luma = .tbcy, chroma = .tbcc; metadata lives at the
  base .tbc name (foo.tbc.db SQLite, legacy foo.tbc.json fallback) - i.e. for
  foo.tbcy the metadata is foo.tbc.db / foo.tbc.json, NOT foo.tbcy.json.
- tbc-analyse (tbcsource.cpp) already accepts: .tbc (+_chroma.tbc suffix/chroma_
  prefix), .ytbc+.ctbc, .tbcy+.tbcc - and its Export tab passes
  tbcSource->getCurrentSourceFilename() (e.g. foo.tbcy) as the tbc-video-export
  positional input plus --input-tbc-json <snapshot>.
- tbc-video-export FileHelper._set_tbc_files() only probed <stem>.tbc and
  <stem>_chroma.tbc -> foo.tbcy input raised "TBC not found at location."
- FileHelper's default metadata <stem>.tbc.json already matches the decode-orc
  base naming once the Y/C files are detected (no metadata change needed).
- NOTE for CLI users with SQLite-only metadata: tbc-video-export is JSON-only
  (pre-existing behaviour for .tbc sources too); decode-orc's legacy foo.tbc.json
  or --input-tbc-json / conversion via tbc-metadata-converter covers that. Not
  part of this issue.

## Changes

- src/tbc-video-export/src/tbc_video_export/common/file_helper.py
  (_set_tbc_files): luma/combined candidates in priority order (.tbc, .tbcy,
  .ytbc) + chroma candidates (_chroma.tbc, .tbcc, .ctbc). Only a plain .tbc
  without a separate chroma stream is COMBINED; .tbcy/.ytbc are always LUMA
  (with or without chroma). Chroma without luma keeps the existing error.
- tests: new fixtures (orc_yc.tbcy/.tbcc, orc_y_only.tbcy, orc_legacy.ytbc/.ctbc,
  orc_chroma_only.tbcc, orc_mixed.tbc/.tbcy + .tbc.json copies) and test cases in
  test_file_helper.py: decode-orc Y/C pair, luma-only, alternate ytbc/ctbc pair,
  .tbc-preferred mixed stem, chroma-only error; conftest FileHelperTestCase gains
  an optional chroma_tbc assertion.
- Docs: src/tbc-video-export/README.md gains an Input Files section (extension
  table + metadata auto-detection); docs/Tools/tbc-analyse.md's file-naming note
  now lists the decode-orc and alternate Y/C extensions and that exports
  recognise them.

## Commands run and results

- pytest (CI invocation: .venv + build/bin on PATH, `pytest tests -q`):
  321 passed (was 315 before; +6 new cases/assertions).
- Build-tree python sources refreshed via ninja (NOTE: build/bin/tbc-video-export
  runs the build/share copy - a source edit needs a ninja run to take effect;
  first dry-run attempt failed with the stale copy, which is expected).
- End-to-end (real data, decode-orc layout in /tmp/orc-e2e: foo.tbcy +
  foo.tbcc + foo.tbc.json copied from the pal_svideo fixtures):
  - tbc-video-export foo.tbcy dry_out --dry-run: detected S-Video (Y+C),
    Luma+Chroma (merged) mode, commands reference foo.tbcy (luma pass) and
    foo.tbcc (chroma pass), metadata foo.tbc.json.
  - tbc-video-export foo.tbcy real_out (real run): produced real_out.mkv;
    ffprobe: ffv1, 928x576, yuv422p10le, matroska, 1 frame (0.04 s), JSON
    metadata attached.
- Full ctest: 36/36 Passed.
- python3 ci/check_ci_contracts.py: passed.

## Iteration 2 (user: "it should auto-convert SQL to JSON on loading for tools support")

- tbc-video-export FileHelper now auto-converts SQLite metadata to JSON at load:
  - _resolve_tbc_json_path(): explicit .db via --input-tbc-json converts first
    (suffix check before is_file - a .db is a file too); otherwise the detected
    <stem>.tbc.json is used when present; otherwise a sibling <stem>.tbc.db is
    converted; otherwise the original path is returned so TBCJsonHelper raises
    its usual 'TBC json not found' error.
  - _convert_sqlite_to_json(): converts via the existing tbc-metadata-converter
    (resolved ad-hoc with files.find_binary - deliberately NOT added to the
    tools dict, which the process handler spawns as pipeline stages). Writes to
    a temp file first, then moves it next to the source for reuse by later runs
    and other JSON-only tools; read-only source dir keeps the temp (removed at
    exit via atexit); converter failure raises a clear TBCError with its output.
  - The GUI path was already covered (tbc-analyse passes a JSON snapshot via
    --input-tbc-json); this makes the standalone CLI and auto-detection paths
    work from decode-orc's SQLite-only captures.
- New fixture tests/files/orc_db_only.tbc.db (69 KB, generated from
  pal_svideo.tbc.json with tbc-metadata-converter) + 4 pytest cases: sqlite-only
  auto-convert (json placed next to source), existing-json precedence,
  --input-tbc-json pointing at a .db (temp result), read-only source dir
  fallback (POSIX, permissions restored for cleanup). Converter-missing runs
  skip. Initial test run caught a real ordering bug (db override parsed as
  JSON) - fixed by checking the suffix before is_file().
- pytest: 325 passed (was 321).
- E2E (real data, /tmp/orc-db-e2e: foo.tbcy + foo.tbcc + foo.tbc.db, NO json):
  - dry-run logged "Only SQLite metadata found (foo.tbc.db); auto-converting to
    JSON" + "Converted metadata saved to foo.tbc.json"; full S-Video (Y+C)
    pipeline readout.
  - Real export: all pipeline stages 0 errors; db_real.mkv produced; ffprobe:
    ffv1, 928x576, yuv422p10le, matroska, 1 frame.
  - Second run: 0 conversion messages - the saved json is reused.
- Docs: tbc-video-export README Input Files section now documents the SQLite
  auto-conversion and the --input-tbc-json .db case.

## Commit / push / CI (2026-11-02)

- Commit 0682659 (user-requested, references Fixes #30; issue #30 closed at
  2026-11-02T05:36:50Z). 19 files, +472/-16, includes the new fixtures (note:
  orc_mixed.tbc needed git add -f - the root .gitignore ignores *.tbc globally
  with only test-data/ excepted; the existing .tbc fixtures in tests/files are
  force-added the same way).
- Tests run 41397377458: first attempt FAILED on macOS x86_64 ONLY - a
  transient actions/checkout git fetch network failure (curl 56 Recv failure /
  early EOF, git exit 128, three attempts), not a code/workflow problem.
  Everything else passed, including Full Linux build and test parity (the 325
  pytest + 36 ctest). Re-ran the failed job (gh run rerun --failed): macOS
  x86_64 + universal macOS app/dmg then PASSED; FINAL conclusion=success, all
  jobs green (Upload release assets correctly skipped, no publish).

DONE: commit pushed; GitHub Actions parity satisfied for head 0682659; issue
#30 closed.
