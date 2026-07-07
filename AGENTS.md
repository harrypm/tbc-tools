# AGENTS.md

This file provides guidance to AI agents when working with code in this repository.

## Project Overview

The ld-decode tools project provides professional-grade tools for digitizing, processing, and analyzing analog video sources (particularly LaserDisc captures) with exceptional quality and accuracy. The codebase consists of multiple C++ command-line tools and a shared library infrastructure.

## Development Environment

This project uses **Nix** for reproducible builds and development environments.

### Essential Commands

**Setup Development Environment:**
```bash
nix develop
```

**Build (inside Nix shell):**
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

**Build without entering shell:**
```bash
nix develop -c cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
nix develop -c ninja -C build
```

**Install without entering profile:**
```bash
nix profile install .#
```

**Run Tests:**
```bash
# Inside build directory after cmake/ninja
ctest --test-dir build --output-on-failure
```

**Clean Build:**
```bash
rm -rf build
```

## Architecture Overview

### Core Structure
- **`src/`**: All source code organized by tool
- **`src/library/`**: Shared libraries used across tools
  - **`src/library/filter/`**: Digital signal processing filters (FIR, IIR, de-emphasis)
  - **`src/library/tbc/`**: TBC format handling, metadata management, video/audio I/O
- **Individual tool directories**: Each tool has its own directory under `src/`

### Key Tools Categories
- **Core Processing**: `ld-process-vbi`, `ld-process-vits`
- **EFM Decoder Suite**: `efm-decoder-f2`, `efm-decoder-d24`, `efm-decoder-audio`, `efm-decoder-data`, `efm-stacker-f2`
- **Analysis**: `ld-analyse` (GUI), `ld-discmap`, `ld-dropout-correct`
- **Export/Conversion**: `ld-chroma-decoder`, `tbc-export-metadata`, `ld-lds-converter`, `tbc-metadata-converter`

### Build System
- **CMake-based** with Ninja generator preferred
- **Out-of-source builds required** (enforced by CMakeLists.txt)
- **Multi-threading support** for performance
- **Qt6** dependency for GUI components and core functionality
- **FFTW3** for signal processing
- **SQLite** for metadata storage

### Critical Dependencies
- **ezpwd Reed-Solomon library**: Managed as git submodule at `src/efm-decoder/libs/ezpwd`
- **Qt6**: Core, Gui, Widgets, Sql modules
- **FFmpeg, FFTW, SQLite**: Via Nix or system packages

## File Format Specifications

### TBC Files
- **Binary format**: 16-bit unsigned samples, little-endian
- **Extension**: `.tbc`
- **Metadata**: Stored in separate SQLite database (`.tbc.db`)
- **Field-based**: Sequential field data with fixed width per line

### Metadata Format
- **SQLite database** format (internal, subject to change)
- **Do NOT access directly** - use `tbc-export-metadata` instead
- **Tables**: `video_parameters`, `fields`, `dropouts`

## Development Patterns

### Shared Library Usage
```cpp
// TBC metadata access
#include "tbc/lddecodemetadata.h"
LdDecodeMetaData metadata;
metadata.read("video.tbc.db");

// Video I/O
#include "tbc/sourcevideo.h"
SourceVideo source;
source.open("input.tbc", fieldWidth);

// Filtering
#include "filter/firfilter.h"
FIRFilter<double> filter(coefficients);
```

### Testing Framework
- **CTest** integration for automated testing
- **Unit tests** in `src/library/*/test*` directories
- **Integration tests** via scripts in `scripts/` directory
- **Test data** expected in `testdata/` directory (git submodule)

## Important Notes

- **SQLite metadata format is internal only** - never access `.tbc.db` files directly
- **Out-of-source builds are enforced** - use `build/` or `build-*` directories
- **Nix environment provides all dependencies** - prefer Nix over manual dependency management
- **Qt6 required** - all tools use Qt framework even for CLI tools
- **Multi-threading enabled** by default for performance-critical operations
- **Hard rule: all build/testing work must pass GitHub Actions workflows** - do not treat build verification as complete unless GitHub Actions succeeds
- **Hard rule: GUI input contrast is mandatory** - never ship a GUI binary where text/edit/selection widgets have low-contrast text; all Qt GUI entrypoints must apply the shared `tbc::ui::enforceInputWidgetContrast(...)` guard after palette/theme setup
- **Hard rule: preserve legacy GUI theme appearance** - do not introduce blue-tinted global backgrounds; keep the established neutral grey/black theme in existing GUIs by preserving each app’s intended `QPalette::Window`/`QPalette::Base` colors
- **Hard rule: contrast guards must not restyle the full app** - shared contrast enforcement may adjust text readability roles (`Text`, `PlaceholderText`, `HighlightedText`) but must not globally override theme-defining palette roles such as `Base` and `Highlight`
- **Hard rule: tbc-video-export is the sole export/deinterlace engine** - ld-analyse must not hand-roll ffmpeg `bwdif`/`parity` filter graphs; proxy/web deinterlaced output must be produced via tbc-video-export web profiles (`h264_web`/`h265_web`/`av1_web`) so field-order (parity) resolution flows through `src/tbc-video-export/src/tbc_video_export/common/field_order.py`. Enforced by `ci/check_ci_contracts.py` (`LD_ANALYSE_FORBIDDEN_SNIPPETS` forbids `bwdif=mode=send_frame:parity=auto:deint=all`; `LD_ANALYSE_REQUIRED_SNIPPETS` requires the proxy web-profile routing marker + `proxyExportProfileName`).
- **Hard rule: field-order parity must default to AUTO** - `--field-order` must resolve from `firstActiveFrameLine`/`lastActiveFrameLine` + output padding (`compute_top_pad_lines`/`compute_is_tff`); never hardcode TFF/BFF in export or proxy paths. Enforced by `ci/check_ci_contracts.py` (`TBC_VIDEO_EXPORT_REQUIRED_SNIPPETS` requires `default=FieldOrder.AUTO` in `src/tbc-video-export/src/tbc_video_export/opts/opts_ffmpeg.py` and the field-order helpers in `common/field_order.py`).
