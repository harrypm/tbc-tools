# Prompt README — LdDecodeMetaData → TbcMetaData rename (internal code)

**Date:** 2026-08-23
**Repo:** `C:\Users\Harry\tbc-tools` (branch: `main`)
**Model:** glm 5.2 (Oz)

## User input

> "Do the LD/LDecode to TBC conversion naming first then lets add the metadata
> page"
>
> "internal code needs to move away from 'LD' everything as its all handling TBC
> data and TBC files..."

## Clarifying answers

1. **Naming convention:** `Tbc` PascalCase for classes, `tbc` lowercase for
   files/paths. `LdDecodeMetaData` → `TbcMetaData`, `lddecodemetadata.{h,cpp}`
   → `tbcmetadata.{h,cpp}`, CMake targets `lddecode-library`/`lddecode-chroma`
   → `tbc-library`/`tbc-chroma`.
2. **CLI executables:** deferred to a later step (not this change). Excludes
   `ld-discmap` and `ld-disc-stacker` (LD-centric). `ld-lds-converter` →
   `lds-converter` (not TBC data). This step is internal-code-only.
3. **Duplicate:** unify the `tbc-metadata-converter`'s private copy of
   `lddecodemetadata.{h,cpp}` into the shared library (deletes the duplicate,
   links `tbc-library` instead — fixes the root cause of the SECAM enum
   divergence).
4. **Execution:** one sequential pass (no child agents — shared-header
   consistency).

## What changed

### Library (shared)
- `git mv` `src/library/tbc/lddecodemetadata.{h,cpp}` → `tbcmetadata.{h,cpp}`.
- Class `LdDecodeMetaData` → `TbcMetaData` (include guard `TBCMETADATA_H`,
  all qualified names, ctor, copy-delete, method signatures).
- `src/library/CMakeLists.txt`: target `lddecode-library` → `tbc-library`,
  source `tbc/tbcmetadata.cpp`.

### Duplicate unification (tbc-metadata-converter)
- Deleted `src/tbc-metadata-converter/{lddecodemetadata,dropouts,jsonio}.{h,cpp}`
  (6 files — the duplicate copies that caused the SECAM enum divergence).
- `src/tbc-metadata-converter/CMakeLists.txt`: removed the duplicate sources
  from `add_executable`; link `tbc-library` instead of `lddecode-library`.
- `jsonconverter.cpp`: `metaData.readSqlite(...)` → `metaData.read(...)` (the
  unified `TbcMetaData::read()` auto-detects JSON vs SQLite; the duplicate's
  `readSqlite()` method no longer exists).

### Tree-wide replacement (~120 files)
- `#include "lddecodemetadata.h"` → `#include "tbcmetadata.h"` (63 files).
- `LdDecodeMetaData` → `TbcMetaData` (class references).
- CMake `lddecode-library`/`lddecode-chroma` → `tbc-library`/`tbc-chroma`
  (24 CMakeLists).
- `src/library/README.md`: doc examples updated.

### Variable-name collision fix (post-build errors)
The bulk `LdDecodeMetaData`→`TbcMetaData` regex also renamed camelCase
**variables** named `ldDecodeMetaData` to `TbcMetaData`, colliding with the
class type. Fixed by renaming those variables to `metaData` across:
- `ld-analyse/tbcsource.{h,cpp}` (value member)
- `ld-chroma-decoder/decoderpool.{h,cpp}`, `sourcefield.{h,cpp}` (reference)
- `ld-process-vbi/decoderpool.{h,cpp}`, `ld-process-vits/processingpool.{h,cpp}`
- `ld-disc-stacker/{main,stackingpool}.cpp`, `ld-dropout-correct/main.cpp`
  (QVector variables)
- `ld-discmap/discmap.{h,cpp}` (pointer member + `delete`)
- `tbc-export-metadata/metadataconverter.cpp`
- Also fixed 6 corrupted `#include "tbcmetadata.h"` → `#include "metaData.h"`
  caused by PowerShell's case-insensitive `-replace` (re-done with `-creplace`).

## Build/CI results

- **CMake configure:** success (regenerated for `tbc-library` target).
- **Full Release build:** success — all 19 targets built clean:
  ld-analyse, ld-chroma-decoder, ld-chroma-encoder, ld-disc-stacker, ld-discmap,
  ld-dropout-correct, ld-lds-converter, ld-process-vbi, ld-process-vits,
  tbc-audio-align, tbc-efm-handler, tbc-export-metadata, tbc-metadata-converter,
  ac3-decoder, efm-decoder-{audio,d24,data,f2}, efm-stacker-f2, vfs-verifier.
- **CI contracts:** `python ci/check_ci_contracts.py` → "CI contract checks
  passed." (contract paths still reference `ld-analyse` which is unchanged in
  this step).
- **Final grep:** zero remaining `LdDecodeMetaData`/`lddecodemetadata`/
  `lddecode-library`/`lddecode-chroma` references across all tracked src
  C/C++/CMake/UI/MD files (excluding the Python `tbc-video-export` tree).

## Notes

- No commit made (user has not requested one).
- On-disk metadata format unchanged (enum values, JSON/SQLite schema
  unchanged — only C++ class/file/target names changed).
- `CudaPluginManager` / `cudaPlugin` config / plugin catalog: untouched.

## Deferred (next step)

Rename CLI executables + tool directories (internal code is done):
- `ld-analyse` → `tbc-analyse`
- `ld-chroma-decoder` → `tbc-chroma-decoder`
- `ld-process-vbi` → `tbc-process-vbi`
- `ld-process-vits` → `tbc-process-vits`
- `ld-lds-converter` → `lds-converter` (not TBC data)
- **Exclude** `ld-discmap` and `ld-disc-stacker` (LD-centric, keep as-is).

That step also updates: GitHub workflow references, `ci/check_ci_contracts.py`
tool-name/path constants, `.desktop`/MIME/icon install paths, Python
wrapper/parser filenames (`wrapper_ld_*.py`, `parser_ld_*.py`, `opts_ldtools.py`,
`test_wrappers_ldtools.py`), READMEs, and the AGENTS.md hard rules that name
`ld-analyse`.

## Then

Metadata Editor page (Tools dropdown) for JSON/JSON metadata values — pending
the rename + the SECAM/MESECAM enum extension (the duplicate unification done
here is the prerequisite for that).
