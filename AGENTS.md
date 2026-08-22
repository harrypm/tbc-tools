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
- **Hard rule: never touch or target the ld-decode-tools upstream remote** - do not fetch from, pull from, push to, diff against, cherry-pick from, or otherwise reference `https://github.com/simoninns/ld-decode-tools.git` (or a git remote alias pointing to it, e.g. `upstream`) for any task in this project
- **Hard rule: all build/testing work must pass GitHub Actions workflows** - do not treat build verification as complete unless GitHub Actions succeeds
- **Hard rule: GUI input contrast is mandatory** - never ship a GUI binary where text/edit/selection widgets have low-contrast text; all Qt GUI entrypoints must apply the shared `tbc::ui::enforceInputWidgetContrast(...)` guard after palette/theme setup
- **Hard rule: preserve legacy GUI theme appearance** - do not introduce blue-tinted global backgrounds; keep the established neutral grey/black theme in existing GUIs by preserving each app’s intended `QPalette::Window`/`QPalette::Base` colors
- **Hard rule: contrast guards must not restyle the full app** - shared contrast enforcement may adjust text readability roles (`Text`, `PlaceholderText`, `HighlightedText`) but must not globally override theme-defining palette roles such as `Base` and `Highlight`
- **Hard rule: tbc-video-export is the sole export/deinterlace engine** - ld-analyse must not hand-roll ffmpeg `bwdif`/`parity` filter graphs; proxy/web deinterlaced output must be produced via tbc-video-export web profiles (`h264_web`/`h265_web`/`av1_web`) so field-order (parity) resolution flows through `src/tbc-video-export/src/tbc_video_export/common/field_order.py`. Enforced by `ci/check_ci_contracts.py` (`LD_ANALYSE_FORBIDDEN_SNIPPETS` forbids `bwdif=mode=send_frame:parity=auto:deint=all`; `LD_ANALYSE_REQUIRED_SNIPPETS` requires the proxy web-profile routing marker + `proxyExportProfileName`).
- **Hard rule: field-order parity must default to AUTO** - `--field-order` must resolve from `firstActiveFrameLine`/`lastActiveFrameLine` + output padding (`compute_top_pad_lines`/`compute_is_tff`); never hardcode TFF/BFF in export or proxy paths. Enforced by `ci/check_ci_contracts.py` (`TBC_VIDEO_EXPORT_REQUIRED_SNIPPETS` requires `default=FieldOrder.AUTO` in `src/tbc-video-export/src/tbc_video_export/opts/opts_ffmpeg.py` and the field-order helpers in `common/field_order.py`).
- **Hard rule: CUDA is stripped from default releases; GPU acceleration is an opt-in plugin** - default release builds (Linux x86_64, Windows x86_64, macOS) build with `-DLDCHROMA_ENABLE_CUDA=OFF` (flake.nix `packages.default`), so the default Linux/tests/Windows CI jobs no longer restore the CUDA 11.8 closure or bundle CUDA runtime DLLs. The legacy custom CUDA kernel path (`nnTransform3DCUDA` + `nnTransform3D_kernel.cu`, gated by `configuration.useNNTransform3D`) is dropped from default builds (emits the existing "build does not include CUDA kernels" warning + falls back, same as macOS); production nnTransform3D keeps GPU acceleration via the ONNX Runtime CUDA EP, loaded at runtime when the CUDA plugin (a CUDA 11.8 + cuDNN 8.9 runtime DLL/SO package published to `harrypm/tbc-tools-ci-cache` Releases) is present. `flake.nix` retains `packages.cuda` (CUDA-enabled build) for the plugin-publish CI job. The `scripts/cuda-closure-cache.sh` + `scripts/windows-cuda-runtime.sh` remain required infrastructure for that job; their content is still validated by `ci/check_ci_contracts.py`. GTX-1000-series (Pascal) support is preserved (CUDA 11.8 + cuDNN 8.9; the flake's eval-time assertions still guard the pin).
- **Hard rule: Windows arm64 CI must insulate the gas-preprocessor.pl fetch from raw.githubusercontent.com** - the vcpkg `ffmpeg` port calls `vcpkg_find_acquire_program(GASPREPROCESSOR)` only for `arm`/`arm64` Windows (x64 uses NASM and is unaffected), downloading `gas-preprocessor.pl` from `raw.githubusercontent.com` which rate-limits (HTTP 429) and fails the whole Win arm64 build with `building ffmpeg:arm64-windows failed`. The `build-tbc-tools` (arm64) job in `.github/workflows/build_windows_tools.yml` must pre-fetch `gas-preprocessor.pl` (SHA512-validated, parsed from the pinned vcpkg's `GASPREPROCESSOR` program definition) from CDN mirrors (jsDelivr first, then `github.com/.../raw/`, then `raw.githubusercontent.com` with retries) into `${VCPKG_ROOT}/downloads/` before `Run CMake`, so `vcpkg_download_distfile` finds the file locally and skips the network fetch. The step is gated to `matrix.arch == 'arm64'`, best-effort (`continue-on-error`), and must appear exactly once. Enforced by `ci/check_ci_contracts.py` (`WINDOWS_GAS_PREPROCESSOR_REQUIRED_SNIPPETS` + an exact-count-==1 guard).
- **Hard rule: Windows x86_64 release ships CPU-only-at-runtime; CUDA is an opt-in plugin** - the Windows x86_64 release no longer bundles the ~1.6 GB CUDA 11.8 + cuDNN 8.9 runtime DLLs; `onnxruntime_providers_cuda.dll` is still shipped (from the ORT GPU package, copied by the "Copy ONNX Runtime DLLs" step) but `LoadLibrary` fails without the CUDA runtime DLLs and ORT silently falls back to the CPU EP (intended for the default release). Users who want GPU acceleration install the CUDA plugin (Phase 4: a `tbc-tools-cuda-plugin-windows-x64.zip` published to `tbc-tools-ci-cache` Releases), which provides the CUDA runtime DLLs; the runtime loader (comb.cpp `ensureWindowsOnnxCudaProviderLoaded`, extended in Phase 2 to search the plugin dir) resolves them. arm64 uses CPU-only ONNX Runtime and is unaffected. The vendored vhs-teletext + AAA payloads are untouched. Enforced by `ci/check_ci_contracts.py` (the old `WINDOWS_CUDA_RUNTIME_REQUIRED_SNIPPETS` + `LINUX_CUDA_CACHE_REQUIRED_SNIPPETS` + `TESTS_CUDA_CACHE_REQUIRED_SNIPPETS` workflow-step requirements are removed; the script-content checks for the pinned wheel versions + `--option require-sigs false` remain).

## CUDA 11.8 closure cache

The flake vendors CUDA 11.8 from `nixpkgsLegacy` (nixos-24.11) for GTX-1000-series (Pascal) support. Nixpkgs 25.05 will remove `cudaPackages_11_8`, and `cache.nixos.org` may GC its paths. To insulate the build, the pinned closure is mirrored in the public `harrypm/tbc-tools-ci-cache` git repo as a Nix binary cache (nars > 95 MiB are chunked to fit GitHub's 100 MiB/file limit).

**Note:** default release builds no longer use this closure (`packages.default` builds with `LDCHROMA_ENABLE_CUDA=OFF`). The closure is now consumed only by `packages.cuda` / the CUDA-plugin publish CI job (Phase 4). The script + cache repo remain required infrastructure.

`scripts/cuda-closure-cache.sh` manages it:
```bash
scripts/cuda-closure-cache.sh export  --out ./cuda-cache   # realise + write the binary cache (chunk big nars)
scripts/cuda-closure-cache.sh verify  --out ./cuda-cache   # structural + sha256 check (no restore)
scripts/cuda-closure-cache.sh restore --out ./cuda-cache   # reassemble chunks + import into local Nix store
scripts/cuda-closure-cache.sh push    --out ./cuda-cache --yes   # commit + push cache to tbc-tools-ci-cache
scripts/cuda-closure-cache.sh pull    --out ./cuda-cache   # clone tbc-tools-ci-cache -> ./cuda-cache
```

The script's `NIXPKGS_REV`/`NIXPKGS_SHA` must match `flake.lock`'s `nixpkgsLegacy` rev; `flake.nix` has eval-time assertions that fail loudly if the pin drifts and drops `cudaPackages_11_8`/`cudnn_8_9`/`gcc11` or ships a non-11.8 toolkit. The local `cuda-cache/` working dir is gitignored here (the closure lives in `tbc-tools-ci-cache`, not this repo).

The cache is self-built and **unsigned**, so `restore` imports it with `nix copy --from file://... --option require-sigs false`. This is safe because the cache is local (not an untrusted network substituter), `reassemble_all` already SHA256-verifies every reassembled nar, and `require-sigs = false` only lifts the trusted-key signature check — Nix still verifies each imported nar's content hash against the narinfo's `narHash`. Without this, Nix refuses the paths (`cannot add path ... because it lacks a signature by a trusted key`) and CI silently falls back to `cache.nixos.org` (no insulation). Enforced by `ci/check_ci_contracts.py` (`CUDA_CLOSURE_CACHE_SCRIPT` must contain `--option require-sigs false`).

## Windows CUDA runtime DLL plugin package

The Windows x86_64 release ships `onnxruntime_providers_cuda.dll` (the ONNX Runtime 1.18.1 CUDA-11.x execution provider, copied from the ORT GPU package) but **does not bundle** the CUDA 11.8 runtime + cuDNN 8.9 DLLs it imports. On a clean Windows machine those are not present, so `LoadLibrary("onnxruntime_providers_cuda.dll")` fails and ORT silently falls back to the CPU EP — the intended behaviour for the default (CPU-only-at-runtime) release. GPU acceleration is an opt-in **CUDA plugin** (Phase 4: a `tbc-tools-cuda-plugin-windows-x64.zip` published to `tbc-tools-ci-cache` Releases) that provides the CUDA runtime DLLs; the Phase 2 runtime loader (comb.cpp `ensureWindowsOnnxCudaProviderLoaded`, extended to search the plugin dir) resolves them so `LoadLibrary` succeeds and the ORT CUDA EP registers.

`onnxruntime_providers_cuda.dll` directly imports `cudart64_110.dll`, `cublas64_11.dll`, `cublasLt64_11.dll`, `cufft64_10.dll` and `cudnn64_8.dll`; `cudnn64_8.dll` then lazy-loads the `cudnn_*_infer64_8.dll` sub-libraries for the op categories the model uses (the `chroma_net_v2.onnx` 3D-Conv chroma model — op set `Conv x8, LeakyRelu x7, Add x3, Sigmoid x1` — needs the cnn/ops infer sub-DLLs; `cudnn_adv_infer64_8` is dropped from the plugin as unused).

`scripts/windows-cuda-runtime.sh` fetches the DLL set from NVIDIA's official redistributable pip wheels (`nvidia-*-cu11`, `win_amd64`) — public on PyPI with no NVIDIA login (unlike `developer.nvidia.com` cuDNN) — and mirrors it (chunked under 95 MiB) into the dedicated `harrypm/tbc-tools-ci-cache` repo so CI pulls ~1.6 GB from our own cache instead of re-downloading from PyPI each run:
```bash
scripts/windows-cuda-runtime.sh fetch  --out ./win-cuda-cache            # download wheels via PyPI JSON API, extract DLLs + licenses, write manifest.txt
scripts/windows-cuda-runtime.sh verify --out ./win-cuda-cache            # check every expected DLL present + SHA256 vs manifest.txt
scripts/windows-cuda-runtime.sh push   --out ./win-cuda-cache --yes      # chunk files >95 MiB, commit into tbc-tools-ci-cache, push
scripts/windows-cuda-runtime.sh pull   --out ./win-cuda-cache            # clone tbc-tools-ci-cache, reassemble chunks, place DLLs
```

Pinned wheel versions: `nvidia-cuda-runtime-cu11==11.8.89`, `nvidia-cublas-cu11==11.11.3.6`, `nvidia-cufft-cu11==10.9.0.58`, `nvidia-cudnn-cu11==8.9.5.29` (cuDNN 8.x is required by ORT 1.18.x CUDA-11.x; 9.x is ABI-incompatible). Plugin DLLs (7, trimmed): `cudart64_110`, `cublas64_11`, `cublasLt64_11`, `cufft64_10`, `cudnn64_8`, `cudnn_cnn_infer64_8`, `cudnn_ops_infer64_8` (the `_train_` sub-DLLs are dropped — inference only; `cudnn_adv_infer64_8` is dropped — unused by the conv-only `chroma_net_v2.onnx` model). NVIDIA License.txt files are included in the plugin package. The local `win-cuda-cache/` working dir is gitignored here (the DLL set lives in `tbc-tools-ci-cache`, not this repo). Enforced by `ci/check_ci_contracts.py` (`WIN_CUDA_RUNTIME_SCRIPT` is a required file; the script must contain the pinned wheel versions; `--option require-sigs false` in the closure cache script). The workflow-step requirements (`WINDOWS_CUDA_RUNTIME_REQUIRED_SNIPPETS` etc.) were removed when CUDA was stripped from the default release.
