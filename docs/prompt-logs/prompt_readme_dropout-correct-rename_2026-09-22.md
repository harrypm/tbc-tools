# Prompt README — ld-dropout-correct → tbc-dropout-correct rename (2026-09-22)

## User prompt
> "this seems hood what about tbc-dropout-correct"

(Asking for the same `ld-` → `tbc-` rename treatment applied to `ld-dropout-correct`.)

## Work performed
- `git mv src/ld-dropout-correct → src/tbc-dropout-correct` (all sources: `main.cpp`,
  `dropoutcorrect.{h,cpp}`, `correctorpool.{h,cpp}`, `CMakeLists.txt`, `README.md`).
- `git mv docs/Tools/ld-dropout-correct.md → tbc-dropout-correct.md` + `docs/Tools/.pages` entry.
- Bulk token replacement `ld-dropout-correct` → `tbc-dropout-correct` across 22 files:
  root `CMakeLists.txt` (`add_subdirectory`), `scripts/test-decode-pretbc` (strings only),
  `src/tbc-analyse/exportdialog.cpp` (`seedStat` tool name), `AGENTS.md`, `README.md`,
  `docs/How-to-guides/Working-with-multiple-discs.md`, `src/tbc-process-vbi/README.md`,
  tbc-video-export wrapper/parser/opts/tests.
- `enums.py`: added `ProcessName.__str__` mapping `LD_DROPOUT_CORRECT` → `tbc-dropout-correct`
  (previously fell through to the default `ld-dropout-correct` name).
- Followed the established chroma-rename convention (verified against commit `2ae99e8`):
  - Enum member `LD_DROPOUT_CORRECT` and module files `wrapper_ld_dropout_correct.py` /
    `parser_ld_dropout_correct.py` keep their `ld_` names (matching enum members);
  - Class names `WrapperLDDropoutCorrect` / `ParserLDDropoutCorrect` unchanged;
  - Only binary-name strings/mappings change;
  - `scripts/test-decode-pretbc` function name `run_ld_dropout_correct` kept
    (mirrors `run_ld_chroma_decoder`).
- Release-notes needle: `"ld-dropout"` kept for historical subjects; generic `"tbc-"`
  matcher already covers the new name. No change needed.

## Validation (hard data, 2026-09-22)
- Clean target build under Nix: `bin/tbc-dropout-correct` linked.
- `ctest`: 100% tests passed, 0 failed out of 30.
- `python3 ci/check_ci_contracts.py`: passed.
- pytest (tbc-video-export, venv): 315 passed, 2 failed — `av1`, `av1_web`.
  - A/B proven pre-existing: stashing the rename and running the same two tests on the
    pre-rename tree (old binary name via symlink) fails identically. Root cause: the
    AV1 profiles use `libsvtav1` (missing from system ffmpeg 4.4.8); failure occurs at
    the instant-death ffmpeg step regardless. `tbc-dropout-correct` reports SUCCESS in
    both pipeline stages in `--debug` output.
- Residual grep `ld-dropout-correct`: only historical CI log under `test-artifacts/`
  (intentionally preserved).

## Status
Complete; committed and pushed. AV1 profile test failures are a separate pre-existing
environment issue (system ffmpeg lacks `libsvtav1`; brew ffmpeg 8.1.1 has it but the
export still fails instantly — worth its own investigation if desired).
