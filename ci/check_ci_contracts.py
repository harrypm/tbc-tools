#!/usr/bin/env python3

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WINDOWS_WORKFLOW = ROOT / ".github/workflows/build_windows_tools.yml"
LINUX_WORKFLOW = ROOT / ".github/workflows/build_linux_tools.yml"
MACOS_WORKFLOW = ROOT / ".github/workflows/build_macos_tools.yml"
RELEASE_WORKFLOW = ROOT / ".github/workflows/release.yml"
TESTS_WORKFLOW = ROOT / ".github/workflows/tests.yml"
CUDA_CLOSURE_CACHE_SCRIPT = ROOT / "scripts/cuda-closure-cache.sh"
WIN_CUDA_RUNTIME_SCRIPT = ROOT / "scripts/windows-cuda-runtime.sh"
WINDOWS_REQUIREMENTS = ROOT / "src/tbc-video-export/pyinstaller/requirements-build-windows.txt"
LINUX_BUILD_REQUIREMENTS = ROOT / "src/tbc-video-export/pyinstaller/requirements-build-linux.txt"
LINUX_PYINSTALLER_SCRIPT = ROOT / "src/tbc-video-export/pyinstaller/build_linux.py"
BUNDLE_VERIFY_SCRIPT = ROOT / "ci/verify_linux_bundle.sh"
LD_ANALYSE_EXPORT_DIALOG = ROOT / "src/ld-analyse/exportdialog.cpp"
TBC_VIDEO_EXPORT_OPTS_FFMPEG = ROOT / "src/tbc-video-export/src/tbc_video_export/opts/opts_ffmpeg.py"
TBC_VIDEO_EXPORT_FIELD_ORDER = ROOT / "src/tbc-video-export/src/tbc_video_export/common/field_order.py"

XCB_RUNTIME_LIBS = (
    "libxcb-cursor.so.0",
    "libxcb-icccm.so.4",
    "libxcb-image.so.0",
    "libxcb-keysyms.so.1",
    "libxcb-render-util.so.0",
    "libxkbcommon.so.0",
    "libxkbcommon-x11.so.0",
)

MACOS_FORBIDDEN_SNIPPETS = (
    "/usr/local/*|/opt/homebrew/*",
)

WINDOWS_REQUIRED_SNIPPETS = (
    "workflow_dispatch:",
    "workflow_call:",
    'CACHE_REPOSITORY:',
    'VCPKG_COMMIT:',
    'VCPKG_BINARY_CACHE_ROOT: "${{ github.workspace }}\\\\external-cache\\\\vcpkg"',
    'cache: "pip"',
    "requirements-build-windows.txt",
    "repository: ${{ env.CACHE_REPOSITORY }}",
    "path: external-cache",
    "import pywintypes, win32file, win32pipe",
    "Initialize dedicated repository vcpkg cache",
    "vcpkg-binary-cache-${{ env.VCPKG_COMMIT }}",
    "Commit updated dedicated cache repository (workflow_dispatch only)",
    "CI_CACHE_REPO_TOKEN",
    "tbc-video-export.exe --dump-default-config",
    "Copy AAA (Auto Audio Align) vendor payload to release directory",
    "AAA vendor payload missing under",
)

LINUX_REQUIRED_SNIPPETS = (
    "workflow_dispatch:",
    "workflow_call:",
    "gcc-c++ python3",
    "for item in result/bin/*; do",
    "bash ci/verify_linux_bundle.sh x86-appimage release/tbc-tools-x86_64.AppImage",
    "bash ci/verify_linux_bundle.sh arm64-release release",
    "requirements-build-linux.txt",
    "pyinstaller/build_linux.py",
)

MACOS_REQUIRED_SNIPPETS = (
    "workflow_dispatch:",
    "workflow_call:",
    "macos-15-intel",
    "for item in result/bin/*; do",
    "Missing vendored exporter tool: dist/tbc-tools.app/Contents/MacOS/tbc-video-export",
    "tbc-tools.app/Contents/MacOS/tbc-video-export --version",
    "/nix/store/*)",
    "dep_unique_name()",
    "verify_bundled_dependencies()",
    "Unresolved bundled dependency:",
    "Bundled dependency verification failed.",
    "Bundled AAA vendor payload to $AAA_VENDOR_DST",
    "Build self-contained tbc-video-export (PyInstaller)",
    "tbc-video-export is not a Mach-O binary",
    "Missing AAA vendor payload: dist/tbc-tools.app/Contents/MacOS/vendor/vhs_decode_auto_audio_align/VhsDecodeAutoAudioAlign.exe",
)

RELEASE_REQUIRED_SNIPPETS = (
    "push:",
    "tags:",
    '- "v*"',
    "uses: ./.github/workflows/build_linux_tools.yml",
    "uses: ./.github/workflows/build_windows_tools.yml",
    "uses: ./.github/workflows/build_macos_tools.yml",
)

BUNDLE_VERIFY_REQUIRED_SNIPPETS = (
    'run_smoke_test "x86-appimage-extract-and-run-tbc-video-export"',
    'run_smoke_test "x86-appimage-apprun-tbc-video-export"',
    'run_smoke_test "arm64-launcher-tbc-video-export"',
    'require_path "$ROOT/usr/bin/tbc-video-export"',
    'require_path "$TARGET/bin/tbc-video-export"',
    'require_path "$ROOT/usr/bin/vendor/vhs_decode_auto_audio_align/VhsDecodeAutoAudioAlign.exe"',
    'require_path "$ROOT/usr/bin/vendor/vhs_decode_auto_audio_align/Binah.dll"',
    'require_path "$TARGET/bin/vendor/vhs_decode_auto_audio_align/VhsDecodeAutoAudioAlign.exe"',
    'require_path "$TARGET/bin/vendor/vhs_decode_auto_audio_align/Binah.dll"',
    'tbc-video-export is not an ELF binary',
)

WINDOWS_REQUIRED_PACKAGES = (
    "pyinstaller",
    "pyinstaller-versionfile",
    "dunamai",
    "pywin32",
)

# ld-analyse must route all deinterlace/proxy output through tbc-video-export web profiles
# instead of hand-rolling ffmpeg bwdif/parity filter graphs. The field-order (parity)
# resolution lives in tbc-video-export (common/field_order.py) and is inherited by the
# h264_web/h265_web/av1_web profiles. See AGENTS.md "Hard rule" entries.
LD_ANALYSE_FORBIDDEN_SNIPPETS = (
    # Hand-rolled bwdif parity=auto graph that bypassed field-order resolution and jittered.
    "bwdif=mode=send_frame:parity=auto:deint=all",
)
LD_ANALYSE_REQUIRED_SNIPPETS = (
    # Marker anchoring the refactored proxy path that routes via tbc-video-export web profile.
    "proxy deinterlace routed via tbc-video-export web profile",
    # Web profile selection helper used by both parallel and fallback proxy paths.
    "proxyExportProfileName",
)
# tbc-video-export must keep --field-order defaulting to AUTO so parity is derived from
# firstActiveFrameLine/lastActiveFrameLine + output padding rather than hardcoded TFF/BFF.
TBC_VIDEO_EXPORT_REQUIRED_SNIPPETS = (
    "default=FieldOrder.AUTO",
)
# Linux x86_64 CI (build_linux_tools.yml build-tbc-tools + tests.yml full-build-test)
# must self-serve the pinned CUDA 11.8 closure from the dedicated tbc-tools-ci-cache
# repo via scripts/cuda-closure-cache.sh pull+restore, so cache.nixos.org GC or the
# Nixpkgs 25.05 removal of cudaPackages_11_8 cannot break GTX-1000-series CUDA builds.
# The restore is best-effort (continue-on-error) with a cache.nixos.org fallback, so
# the contract only requires the step + commands to be present, not a hard failure.
LINUX_CUDA_CACHE_REQUIRED_SNIPPETS = (
    "Restore pinned CUDA 11.8 closure from tbc-tools-ci-cache",
    "bash scripts/cuda-closure-cache.sh pull",
    "bash scripts/cuda-closure-cache.sh restore",
    "tbc-tools-ci-cache",
)
TESTS_CUDA_CACHE_REQUIRED_SNIPPETS = (
    "Restore pinned CUDA 11.8 closure from tbc-tools-ci-cache",
    "bash scripts/cuda-closure-cache.sh pull",
    "bash scripts/cuda-closure-cache.sh restore",
)
# Windows arm64 CI must pre-fetch gas-preprocessor.pl into the vcpkg downloads
# dir (SHA512-validated, from CDN mirrors) so a raw.githubusercontent.com HTTP
# 429 / outage cannot fail the arm64 ffmpeg build. The vcpkg ffmpeg port only
# calls vcpkg_find_acquire_program(GASPREPROCESSOR) for arm/arm64-windows
# (x64 uses NASM), so this step is gated to arm64 and must appear exactly once.
# vcpkg skips its network fetch when ${VCPKG_ROOT}/downloads/<filename> already
# exists with the expected SHA512 (verified in vcpkg_download_distfile.cmake).
WINDOWS_GAS_PREPROCESSOR_REQUIRED_SNIPPETS = (
    "Pre-fetch gas-preprocessor.pl for arm64 ffmpeg",
    "vcpkg_find_acquire_program(GASPREPROCESSOR)",
    "cdn.jsdelivr.net/gh/FFmpeg/gas-preprocessor",
    "raw.githubusercontent.com/FFmpeg/gas-preprocessor",
    "Get-FileHash -Algorithm SHA512",
    "if: matrix.arch == 'arm64'",
)
# Windows x86_64 CI must bundle the CUDA 11.8 + cuDNN 8.9 runtime DLLs next to
# onnxruntime_providers_cuda.dll so the ORT CUDA EP loads on a clean NVIDIA
# machine (otherwise LoadLibrary fails and ORT silently falls back to CPU --
# no GPU accel). arm64 uses CPU-only ONNX Runtime and is unaffected. The DLLs
# are pulled from tbc-tools-ci-cache (mirrored from nvidia-*-cu11 PyPI wheels,
# SHA256-verified by scripts/windows-cuda-runtime.sh). The ~1.6 GB set is the
# inherent cost of shipping a working ORT CUDA EP; the release-size cost is
# accepted so GPU acceleration works out-of-the-box.
WINDOWS_CUDA_RUNTIME_REQUIRED_SNIPPETS = (
    "Pull + bundle CUDA 11.8 + cuDNN 8.9 runtime DLLs (x86_64 only)",
    "bash scripts/windows-cuda-runtime.sh pull",
    "bash scripts/windows-cuda-runtime.sh verify",
    "if: matrix.arch == 'x86_64'",
    "release\\\\cudart64_110.dll",
    "release\\\\cublas64_11.dll",
    "release\\\\cublasLt64_11.dll",
    "release\\\\cufft64_10.dll",
    "release\\\\cudnn64_8.dll",
    "release\\\\cudnn_cnn_infer64_8.dll",
    "release\\\\cudnn_ops_infer64_8.dll",
    "release\\\\cudnn_adv_infer64_8.dll",
)
# The fetch script must pin the exact NVIDIA wheel versions it pulls from, so a
# silent upstream wheel bump (e.g. cuDNN 8.x -> 9.x, which is ABI-incompatible
# with ORT 1.18.x CUDA-11.x) cannot slip in unnoticed.
WIN_CUDA_RUNTIME_SCRIPT_REQUIRED_SNIPPETS = (
    "nvidia-cuda-runtime-cu11|11.8.89|",
    "nvidia-cublas-cu11|11.11.3.6|",
    "nvidia-cufft-cu11|10.9.0.58|",
    "nvidia-cudnn-cu11|8.9.5.29|",
)


def check_contains(path: Path, snippet: str, errors: list[str]) -> None:
    content = path.read_text(encoding="utf-8")
    if snippet not in content:
        errors.append(f"{path}: missing required snippet: {snippet!r}")


def check_count_at_least(path: Path, snippet: str, minimum: int, errors: list[str]) -> None:
    content = path.read_text(encoding="utf-8")
    count = content.count(snippet)
    if count < minimum:
        errors.append(
            f"{path}: expected snippet {snippet!r} at least {minimum} times, found {count}"
        )


def check_not_contains(path: Path, snippet: str, errors: list[str]) -> None:
    content = path.read_text(encoding="utf-8")
    if snippet in content:
        errors.append(f"{path}: forbidden snippet present: {snippet!r}")


def main() -> int:
    errors: list[str] = []

    for required_file in (
        WINDOWS_WORKFLOW,
        LINUX_WORKFLOW,
        MACOS_WORKFLOW,
        RELEASE_WORKFLOW,
        WINDOWS_REQUIREMENTS,
        LINUX_BUILD_REQUIREMENTS,
        LINUX_PYINSTALLER_SCRIPT,
        BUNDLE_VERIFY_SCRIPT,
        LD_ANALYSE_EXPORT_DIALOG,
        TBC_VIDEO_EXPORT_OPTS_FFMPEG,
        TBC_VIDEO_EXPORT_FIELD_ORDER,
        TESTS_WORKFLOW,
        CUDA_CLOSURE_CACHE_SCRIPT,
        WIN_CUDA_RUNTIME_SCRIPT,
    ):
        if not required_file.exists():
            errors.append(f"missing required file: {required_file}")

    if errors:
        print("CI contract checks failed:")
        for error in errors:
            print(f" - {error}")
        return 1

    for snippet in WINDOWS_REQUIRED_SNIPPETS:
        check_contains(WINDOWS_WORKFLOW, snippet, errors)

    for snippet in LINUX_REQUIRED_SNIPPETS:
        check_contains(LINUX_WORKFLOW, snippet, errors)
    for snippet in MACOS_REQUIRED_SNIPPETS:
        check_contains(MACOS_WORKFLOW, snippet, errors)
    for snippet in MACOS_FORBIDDEN_SNIPPETS:
        check_not_contains(MACOS_WORKFLOW, snippet, errors)

    for snippet in RELEASE_REQUIRED_SNIPPETS:
        check_contains(RELEASE_WORKFLOW, snippet, errors)

    for snippet in BUNDLE_VERIFY_REQUIRED_SNIPPETS:
        check_contains(BUNDLE_VERIFY_SCRIPT, snippet, errors)

    # PyInstaller build step must appear in both Linux jobs (x86 container + arm64).
    check_count_at_least(LINUX_WORKFLOW, "Build self-contained tbc-video-export (PyInstaller)", 2, errors)

    requirements_content = WINDOWS_REQUIREMENTS.read_text(encoding="utf-8")
    requirements_lines = {
        line.strip()
        for line in requirements_content.splitlines()
        if line.strip() and not line.strip().startswith("#")
    }
    for package in WINDOWS_REQUIRED_PACKAGES:
        if package not in requirements_lines:
            errors.append(
                f"{WINDOWS_REQUIREMENTS}: missing required package line: {package}"
            )

    for runtime_lib in XCB_RUNTIME_LIBS:
        check_count_at_least(LINUX_WORKFLOW, runtime_lib, 2, errors)
        check_contains(BUNDLE_VERIFY_SCRIPT, runtime_lib, errors)

    for snippet in LD_ANALYSE_FORBIDDEN_SNIPPETS:
        check_not_contains(LD_ANALYSE_EXPORT_DIALOG, snippet, errors)
    for snippet in LD_ANALYSE_REQUIRED_SNIPPETS:
        check_contains(LD_ANALYSE_EXPORT_DIALOG, snippet, errors)
    for snippet in TBC_VIDEO_EXPORT_REQUIRED_SNIPPETS:
        check_contains(TBC_VIDEO_EXPORT_OPTS_FFMPEG, snippet, errors)
    check_contains(TBC_VIDEO_EXPORT_FIELD_ORDER, "def compute_is_tff", errors)
    check_contains(TBC_VIDEO_EXPORT_FIELD_ORDER, "def compute_top_pad_lines", errors)

    # Linux x86_64 CI must self-serve the pinned CUDA 11.8 closure from
    # tbc-tools-ci-cache (best-effort, with cache.nixos.org fallback).
    for snippet in LINUX_CUDA_CACHE_REQUIRED_SNIPPETS:
        check_contains(LINUX_WORKFLOW, snippet, errors)
    for snippet in TESTS_CUDA_CACHE_REQUIRED_SNIPPETS:
        check_contains(TESTS_WORKFLOW, snippet, errors)
    # Windows arm64 must pre-fetch gas-preprocessor.pl (insulate from
    # raw.githubusercontent.com 429). Step is gated to arm64 and must appear
    # exactly once in build_windows_tools.yml.
    for snippet in WINDOWS_GAS_PREPROCESSOR_REQUIRED_SNIPPETS:
        check_contains(WINDOWS_WORKFLOW, snippet, errors)
    gp_step_count = WINDOWS_WORKFLOW.read_text(encoding="utf-8").count(
        "Pre-fetch gas-preprocessor.pl for arm64 ffmpeg"
    )
    if gp_step_count != 1:
        errors.append(
            f"{WINDOWS_WORKFLOW}: gas-preprocessor pre-fetch step must appear exactly once "
            f"(arm64 only), found {gp_step_count}"
        )
    # Windows x86_64 must pull + bundle the CUDA/cuDNN runtime DLLs (gated to
    # x86_64; arm64 uses CPU-only ONNX Runtime). The step must appear exactly once.
    for snippet in WINDOWS_CUDA_RUNTIME_REQUIRED_SNIPPETS:
        check_contains(WINDOWS_WORKFLOW, snippet, errors)
    cuda_rt_step_count = WINDOWS_WORKFLOW.read_text(encoding="utf-8").count(
        "Pull + bundle CUDA 11.8 + cuDNN 8.9 runtime DLLs (x86_64 only)"
    )
    if cuda_rt_step_count != 1:
        errors.append(
            f"{WINDOWS_WORKFLOW}: CUDA runtime DLL bundle step must appear exactly once "
            f"(x86_64 only), found {cuda_rt_step_count}"
        )
    # The fetch script must pin the exact wheel versions (guards against a silent
    # cuDNN 8.x -> 9.x bump, which would break the ORT 1.18.x CUDA-11.x EP).
    for snippet in WIN_CUDA_RUNTIME_SCRIPT_REQUIRED_SNIPPETS:
        check_contains(WIN_CUDA_RUNTIME_SCRIPT, snippet, errors)
    # The CUDA closure cache is self-built and unsigned, so the restore must
    # import it with require-sigs disabled or Nix refuses the paths with
    # "cannot add path ... because it lacks a signature by a trusted key" and
    # CI silently falls back to cache.nixos.org (no insulation).
    check_contains(CUDA_CLOSURE_CACHE_SCRIPT, "--option require-sigs false", errors)
    # The cache restore step must only appear once in build_linux_tools.yml
    # (x86_64 CUDA job) -- never in the arm64 job, where enableCuda=false and
    # restoring an x86_64 closure would fail. Exactly one occurrence.
    check_count_at_least(
        LINUX_WORKFLOW, "Restore pinned CUDA 11.8 closure from tbc-tools-ci-cache", 1, errors
    )
    linux_step_count = LINUX_WORKFLOW.read_text(encoding="utf-8").count(
        "Restore pinned CUDA 11.8 closure from tbc-tools-ci-cache"
    )
    if linux_step_count != 1:
        errors.append(
            f"{LINUX_WORKFLOW}: CUDA cache restore step must appear exactly once "
            f"(x86_64 job only), found {linux_step_count}"
        )

    if errors:
        print("CI contract checks failed:")
        for error in errors:
            print(f" - {error}")
        return 1

    print("CI contract checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
