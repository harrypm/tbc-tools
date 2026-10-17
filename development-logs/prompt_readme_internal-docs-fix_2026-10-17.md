# Prompt readme — internal docs staleness fixes (2026-10-17)

Prompt: "fix any internal docs that need updating"

## Survey + stale spots found (verified against code before editing)

1. `AGENTS.md` Testing Framework: "Test data expected in `testdata/`
   directory (git submodule)" — stale (no such dir/submodule; data is
   in-repo `test-data/` with ntsc/pal/secam). Fixed to the real layout.
2. `AGENTS.md` CUDA hard rule: "the default Linux/tests/Windows CI jobs no
   longer restore the CUDA 11.8 closure" — stale for the tests job since the
   conditional insurance restore. Reworded (Linux/Windows release jobs; tests
   job restores conditionally, pointing at the closure-cache section).
3. `CMakeLists.txt` test block: stale "clone ld-decode-testdata as testdata"
   comment + unused `set(TESTDATA_DIR ${CMAKE_SOURCE_DIR}/testdata)` pointing
   at a nonexistent dir. Comment rewritten (in-repo test-data fixtures),
   unused variable removed.
4. `docs/Tools/tbc-analyse.md`: "SECAM/MESECAM ... export defaults to `mono`"
   — stale since the 2026-09-13 tuning session; verified in code
   (tbcmetadata.cpp:478-486 defaults `chromaDecoder = "secam"` for
   secamFamilySystem). Fixed + noted secam/secam-predemod names are accepted
   and chroma gain/phase are passed for them.
5. `src/tbc-chroma-decoder/README.md`:
   - Overview said "PAL and NTSC" only → added SECAM-family.
   - Decoder list missing `secam`/`secam-predemod` → mirrored the CLI help.
   - No SECAM options → added `--secam-click-nr` and
     `--secam-first-line-is-red` (wording from main.cpp help text).
   - Standards list missing SECAM/MESECAM → added.
   - Examples used nonexistent flags (`-p transform`, `-n 3D`, `-p mono`) →
     replaced with real invocations (`--decoder ...`) + a SECAM example.
   - Duplicated tail sections (Active Area Selection / Chroma Processing /
     NTSC Specific / Transform Decoder / Standard Options repeating earlier
     sections) → removed.
6. Checked and NOT stale: root README.md (already lists MESECAM/SECAM),
   BUILD.md / README.md test sections (none), docs/ otherwise,
   tbc-metadata-converter/README.md (schema NULL docs unaffected by the
   0-valued-metadata sentinel fix), plugins.md, test-data/secam/README.md
   (written this week).

## Validation

- cmake reconfigure + ninja + `ctest -R decode-pretbc-secam|mesecam`: 3/3
  passed (CMakeLists comment change safe)
- `bash ci/run_local_ci_parity.sh --guardrails-only`: exit 0
- Previous Tests runs for c13cd99 / c49767b / f2dab5d: all success

## Result

(to be updated after the pushed commit's Tests run completes)
