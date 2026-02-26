# Windows Build Workflow Fixed - 2026-02-26

## Changes Made

Fixed the GitHub Actions Windows build workflow (`.github/workflows/build_windows_tools.yml`) to match the style and structure used in vhs-decode for creating portable Windows executables.

### Key Fixes Applied:

1. **Binary Copy Path**: Changed from `build\\bin\\*` to `build\\tools\\*` to match the actual CMake output structure used by tbc-tools.

2. **Extended License Files**: Added comprehensive license file copying to include all dependencies:
   - Added: brotli, bzip2, freetype, harfbuzz, icu, openssl, pcre2, qwt, zstd
   - Matches the complete license set from vhs-decode

3. **CMake Configuration**: Added `-DBUILD_PYTHON=false` flag to match vhs-decode build configuration.

4. **vcpkg Version**: Updated vcpkg commit ID to `d30fdf55cfca16e12bc3ad99cbc615997014b61b` (same as vhs-decode for consistency).

5. **Script Copying**: Added conditional copying of Windows batch scripts if they exist in `scripts/Windows/` directory.

### Tools Included in Build:

Based on CMakeLists.txt analysis, the following tools will be built:
- ld-analyse
- ld-chroma-decoder (plus encoder)
- ld-disc-stacker
- ld-discmap
- ld-dropout-correct
- ld-export-decode-metadata
- ld-export-metadata
- ld-json-converter
- ld-lds-converter
- ld-process-vbi
- ld-process-vits
- efm-decoder

### External Tools Bundled:
- FFmpeg 8.0.1 (full build)
- FLAC 1.5.0

The workflow now creates portable Windows executable packages that match the style and completeness of the vhs-decode project releases.