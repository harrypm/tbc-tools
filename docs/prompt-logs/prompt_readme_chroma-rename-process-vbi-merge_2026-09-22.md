# Prompt README — Chroma tool renames + single-binary tbc-process-vbi merge (2026-09-22)

## User prompts
- "Manual build info was not moved to BUILD ld-process-vbi should be tbc-process-vbi etc"
- "also update windows and tools modes i.g teltext and vbi processing window, aaa, markeers, scopes there hsould be a clear list for analyse"
- "Execute this plan."

## Work performed
- Renamed `ld-chroma-decoder` → `tbc-chroma-decoder` and `ld-chroma-encoder` → `tbc-chroma-encoder`
  (git mv of `src/` directories and `docs/Tools/*.md` + `.pages` entries).
- Renamed Python wrapper/parser modules (`wrapper_ld_chroma_*` → `wrapper_tbc_chroma_*`,
  `parser_ld_chroma_*` → `parser_tbc_chroma_*`) with registry/import updates in
  `wrapper_group.py`, `parser_group.py`, `test_wrappers_ldtools.py`; `ProcessName`
  enum string mappings updated to the new binary names.
- Merged CLI `ld-process-vbi` and the `tbc-analyse` GUI helper into a single
  `tbc-process-vbi` binary: GUI when launched bare or with `--gui`, otherwise CLI
  processing. The GUI spawns this same binary in CLI mode.
  - `git mv src/ld-process-vbi → src/tbc-process-vbi`
  - Moved `processvbidialog.{h,cpp}` out of `src/tbc-analyse`
  - Merged `main.cpp` with mode dispatch; single CMake target; tests renamed
  - Unified duplicate `VbiProcessingOptions` into `tbc-library` (`tbc/vbiprocessingoptions.h`)
- Bulk token replacement across ~121 files (source, scripts, docs, CI, workflows,
  `verify_linux_bundle.sh`, release-notes needles keep historical names + add new).
- Docs: `docs/Tools/tbc-analyse.md` gained grouped Window/Scopes/Tools/Plugins menu
  lists; README build sections slimmed to point at `BUILD.md`/`INSTALL.md`.

## Commands run (key steps)
- `git mv` operations for directories, docs, and moved sources.
- Perl in-place bulk token replacement (`ld-chroma-decoder`, `ld-chroma-encoder`,
  `ld_chroma_*`, `ld-process-vbi`, `ld_process_vbi` variants).
- Clean Nix build: `nix develop` → `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` → `ninja -C build`.
- `ctest --test-dir build --output-on-failure` — 30/30 passed.
- `python3 ci/check_ci_contracts.py` — passed.
- Residual greps for old tokens — only intentional historical-context comments remain
  (e.g. "compatible with the former ld-process-vbi" in `src/tbc-process-vbi/main.cpp`).

## Validation (hard data, 2026-09-22)
- ctest: 100% tests passed, 0 failed out of 30 (incl. `tbc-process-vbi-teletext-help`,
  `tbc-process-vbi-teletext-runtime`, chroma NTSC/PAL RGB/YCbCr regressions).
- CI contract checks: passed.
- `build/bin/` contains `tbc-analyse`, `tbc-chroma-decoder`, `tbc-chroma-encoder`,
  `tbc-process-vbi`; no old-name binaries remain.
- Commit `2ae99e8` pushed to `origin/main`; working tree clean.

## Status
Complete. Pending: real-world GUI confirmation from user (rename presentation,
settings migration, boundary default) as with prior phases.
