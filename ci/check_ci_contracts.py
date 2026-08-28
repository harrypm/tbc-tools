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
CUDA_PLUGIN_PUBLISH_WORKFLOW = ROOT / ".github/workflows/publish_cuda_plugin.yml"
CUDA_PLUGIN_PACKAGE_SCRIPT = ROOT / "scripts/cuda-plugin-package.sh"
WINDOWS_REQUIREMENTS = ROOT / "src/tbc-video-export/pyinstaller/requirements-build-windows.txt"
LINUX_BUILD_REQUIREMENTS = ROOT / "src/tbc-video-export/pyinstaller/requirements-build-linux.txt"
LINUX_PYINSTALLER_SCRIPT = ROOT / "src/tbc-video-export/pyinstaller/build_linux.py"
BUNDLE_VERIFY_SCRIPT = ROOT / "ci/verify_linux_bundle.sh"
AAA_LINUX_BUILD_SCRIPT = ROOT / "scripts/build-aaa-linux.sh"
AAA_LINUX_PACKAGE_SCRIPT = ROOT / "scripts/package-aaa-appimage.sh"
LD_ANALYSE_EXPORT_DIALOG = ROOT / "src/ld-analyse/exportdialog.cpp"
TBC_VIDEO_EXPORT_OPTS_FFMPEG = ROOT / "src/tbc-video-export/src/tbc_video_export/opts/opts_ffmpeg.py"
TBC_VIDEO_EXPORT_FIELD_ORDER = ROOT / "src/tbc-video-export/src/tbc_video_export/common/field_order.py"
AGENTS_RULES_FILE = ROOT / "AGENTS.md"

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
    # The arm64 runner must not drift back to `macos-latest`: that label migrated
    # to macos-26 (Tahoe) on 2026-07-15, and the flake.lock-pinned nixpkgs Qt uic
    # crashes with SIGTRAP on macOS 26 (CMake AUTOUIC -> "CMake Generate step
    # failed"). The explanatory comment may mention macos-latest, so forbid the
    # precise `runner: macos-latest` assignment, not the bare label.
    "runner: macos-latest",
    # The ffmpeg fallback must not revert to the live channel: Nixpkgs 26.11
    # dropped x86_64-darwin, so `nix build nixpkgs#ffmpeg.bin` throws on the
    # macos-15-intel runner. Use the flake's pinned .#ffmpeg^bin instead. The
    # comment may mention nixpkgs#ffmpeg.bin, so forbid the precise `nix build
    # nixpkgs#ffmpeg.bin` invocation, not the bare attribute.
    "nix build nixpkgs#ffmpeg.bin",
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
    # AAA is built from the vendored C# source and packaged into a self-
    # contained AppImage in both Linux jobs, replacing the prebuilt Windows
    # .exe so no host Mono is needed at runtime.
    "Build and package native AAA AppImage from vendored source",
    "bash scripts/build-aaa-linux.sh",
    "bash scripts/package-aaa-appimage.sh",
)

MACOS_REQUIRED_SNIPPETS = (
    "workflow_dispatch:",
    "workflow_call:",
    # Both macOS runners must be pinned to macos-15: macos-latest migrated to
    # macos-26 on 2026-07-15, where the flake.lock-pinned nixpkgs Qt uic crashes
    # (SIGTRAP) during CMake AUTOUIC. macos-15-intel is the x86_64 runner;
    # `runner: macos-15` is the arm64 pin (see MACOS_FORBIDDEN_SNIPPETS for the
    # matching `runner: macos-latest` prohibition).
    "macos-15-intel",
    "runner: macos-15",
    # ffmpeg must come from the flake's pinned nixpkgs (.#ffmpeg^bin), not the
    # live channel (nixpkgs#ffmpeg.bin) which dropped x86_64-darwin in Nixpkgs
    # 26.11. See MACOS_FORBIDDEN_SNIPPETS for the matching prohibition.
    "nix build .#ffmpeg^bin",
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
    # Manual releases must run from main only, block existing-tag rebuilds by
    # default, validate the tag commit against the intended source commit, and
    # force make_latest=true so the GitHub "latest release" pointer tracks the
    # newest tag (a prior release was cut from stale code; these guards prevent
    # that recurrence).
    "allow_existing_tag_rebuild:",
    "Manual release mode is only allowed from refs/heads/main",
    "Refusing to rebuild by default to prevent stale releases.",
    "Release integrity check failed:",
    'gh release edit "$RELEASE_TAG" --repo "${{ github.repository }}" --latest',
)

BUNDLE_VERIFY_REQUIRED_SNIPPETS = (
    'run_smoke_test "x86-appimage-extract-and-run-tbc-video-export"',
    'run_smoke_test "x86-appimage-apprun-tbc-video-export"',
    'run_smoke_test "arm64-launcher-tbc-video-export"',
    # AAA is shipped as a self-contained AppImage (built from vendored source
    # in the Linux jobs) that bundles Mono, so the bundle verifier checks the
    # AppImage is present, executable, and runs without host mono.
    'run_smoke_test "x86-appimage-aaa-no-host-mono"',
    'run_smoke_test "arm64-aaa-no-host-mono"',
    # AAA detection: the verifier must confirm ld-analyse's appDir-relative
    # resolver path actually reaches the AAA AppImage (not just that the file
    # exists at an absolute path) and launches it via the resolver's
    # `env APPIMAGE_EXTRACT_AND_RUN=1` mechanism. Catches a bundle that placed
    # AAA at the wrong relative location (detection would fail at runtime even
    # though the file is present).
    'run_smoke_test "x86-appimage-aaa-detection"',
    'run_smoke_test "arm64-aaa-detection"',
    'require_executable "$ROOT/usr/bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage"',
    'require_executable "$TARGET/bin/vendor/vhs_decode_auto_audio_align/vhs-decode-aaa.AppImage"',
    'require_path "$ROOT/usr/bin/tbc-video-export"',
    'require_path "$TARGET/bin/tbc-video-export"',
    'require_path "$ROOT/usr/bin/vendor/vhs-teletext/teletext/__main__.py"',
    'require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext-noscanlines.css"',
    'require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext2.ttf"',
    'require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext4.ttf"',
    'require_path "$TARGET/bin/vendor/vhs-teletext/teletext/__main__.py"',
    'require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext-noscanlines.css"',
    'require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext2.ttf"',
    'require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext4.ttf"',
    'tbc-video-export is not an ELF binary',
)

WINDOWS_REQUIRED_PACKAGES = (
    "pyinstaller",
    "pyinstaller-versionfile",
    "dunamai",
    "pywin32",
)

# The dedicated Windows cache repo push must neutralize checkout-injected
# github.com auth headers (github-actions[bot]) or those credentials can
# override the PAT-in-URL and cause 403 on cross-repo pushes.
WINDOWS_CACHE_PUSH_AUTH_REQUIRED_SNIPPETS = (
    "config --local --unset-all http.https://github.com/.extraheader",
    "config --local credential.helper \"\"",
    "remote set-url origin \"https://x-access-token:$cacheToken@github.com/${{ env.CACHE_REPOSITORY }}.git\"",
)

# Linux AAA source builds must stay compatible across both toolchains:
# Ubuntu arm64 has no apt msbuild package; Oracle Linux 8 uses xbuild 14.0,
# which requires TargetFrameworkVersion=v4.5 for the vendored AAA project.
LINUX_AAA_XBUILD_REQUIRED_SNIPPETS = (
    "MSBUILD_BASENAME=",
    "MSBUILD_FRAMEWORK_ARGS=()",
    "[ \"$MSBUILD_BASENAME\" = \"xbuild\" ]",
    "/p:TargetFrameworkVersion=v4.5",
    "\"${MSBUILD_FRAMEWORK_ARGS[@]}\"",
)
LINUX_AAA_FORBIDDEN_SNIPPETS = (
    "apt-get install -y --no-install-recommends mono-devel msbuild unzip",
)

# Keep AGENTS hard rules aligned with CI-enforced guardrails.
AGENTS_HARD_RULE_REQUIRED_SNIPPETS = (
    "Hard rule: Windows dedicated cache repo pushes must clear checkout-injected github.com auth headers before pull/push",
    "Hard rule: Linux AAA source builds must stay xbuild-compatible and must not require apt msbuild on arm64",
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
# CUDA is stripped from default releases (flake.nix packages.default builds with
# -DLDCHROMA_ENABLE_CUDA=OFF), so the default Linux/Windows/tests CI jobs no
# longer restore the CUDA closure or bundle CUDA runtime DLLs. GPU acceleration
# for nnTransform3D is an opt-in runtime plugin (a CUDA plugin package published
# to tbc-tools-ci-cache Releases; Phase 4 adds the plugin-publish job + a
# contract for it). The CUDA closure cache script (scripts/cuda-closure-cache.sh)
# and Windows CUDA runtime script (scripts/windows-cuda-runtime.sh) remain
# required infrastructure for the plugin-publish job; their content is still
# validated below (require-sigs + pinned wheel versions).
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
# The fetch script must pin the exact NVIDIA wheel versions it pulls from, so a
# silent upstream wheel bump (e.g. cuDNN 8.x -> 9.x, which is ABI-incompatible
# with ORT 1.18.x CUDA-11.x) cannot slip in unnoticed.
WIN_CUDA_RUNTIME_SCRIPT_REQUIRED_SNIPPETS = (
    "nvidia-cuda-runtime-cu11|11.8.89|",
    "nvidia-cublas-cu11|11.11.3.6|",
    "nvidia-cufft-cu11|10.9.0.58|",
    "nvidia-cudnn-cu11|8.9.5.29|",
)
# The AAA Linux build script must restore the Binah NuGet dep from the vendored
# local source only (no network fetch) and build only the main project (not the
# .sln), so the test-only NUnit/NSubstitute deps are never needed. The package
# script must bundle the distro Mono runtime so the AppImage needs no host Mono.
AAA_LINUX_BUILD_SCRIPT_REQUIRED_SNIPPETS = (
    # The Binah nupkg must be vendored (no network fetch) and restored without
    # the nuget CLI (nuget is not in EPEL 8): the script extracts it to the
    # packages/ path the .csproj HintPath expects, then builds the main project.
    "nuget/Binah.2.0.4.nupkg",
    "packages/Binah.2.0.4/lib/net45/Binah.dll",
    "VhsDecodeAutoAudioAlign/VhsDecodeAutoAudioAlign.csproj",
)
AAA_LINUX_PACKAGE_SCRIPT_REQUIRED_SNIPPETS = (
    "APPIMAGE_EXTRACT_AND_RUN=1",
    "$APPDIR/usr/bin/mono",
    "show-build-info",
)
# The CUDA plugin publish workflow (.github/workflows/publish_cuda_plugin.yml)
# builds + publishes the opt-in CUDA runtime plugin packages + manifests to
# tbc-tools-ci-cache Releases under a cuda-plugin-vX tag. It must use the
# cuda-plugin-package.sh script, build the Nix-store deps for Linux, produce
# both platform packages + their manifests, and publish via gh release.
CUDA_PLUGIN_PUBLISH_REQUIRED_SNIPPETS = (
    "workflow_dispatch:",
    'cuda-plugin-v*',
    "bash scripts/cuda-plugin-package.sh build-all",
    "nix build .#cuda-plugin-linux-deps",
    "tbc-tools-cuda-plugin-linux-x86_64.tar.gz",
    "tbc-tools-cuda-plugin-windows-x86_64.zip",
    "tbc-cuda-plugin-linux-x86_64-manifest.json",
    "tbc-cuda-plugin-windows-x86_64-manifest.json",
    "gh release create",
    "CACHE_REPOSITORY",
    "CI_CACHE_REPO_TOKEN",
)
# The packaging script must pin the same NVIDIA wheel versions + the ORT version,
# and produce the trimmed 7-file set (cudnn_adv_infer dropped as unused by the
# conv-only chroma_net_v2.onnx model).
CUDA_PLUGIN_PACKAGE_SCRIPT_REQUIRED_SNIPPETS = (
    "nvidia-cuda-runtime-cu11|11.8.89",
    "nvidia-cublas-cu11|11.11.3.6",
    "nvidia-cufft-cu11|10.9.0.58",
    "nvidia-cudnn-cu11|8.9.5.29",
    "ORT_VERSION=",
    "libonnxruntime_providers_cuda.so",
    "onnxruntime_providers_cuda.dll",
    "--deps-dir",
)
# Windows release must bundle the vendored vhs-teletext Python tree at
# release\vendor\vhs-teletext so ld-process-vbi's teletextintegration.cpp
# resolveTeletextVendorDirectory() finds teletext\__main__.py next to the exe.
# The "Copy binaries" step flattens only *.exe/*.dll to the release root and
# drops the Python source tree, so an explicit restore step is required (same
# pattern as the AAA vendor payload). Without it, ld-analyse's Process VBI ->
# --teletext-html-dir export fails on a clean Windows install with "Could not
# locate vendored vhs-teletext runtime directory." The step is not arch-gated
# (the tree is arch-independent Python source, same as AAA) and must appear
# exactly once in build_windows_tools.yml.
WINDOWS_TELETEXT_VENDOR_REQUIRED_SNIPPETS = (
    "Copy vhs-teletext vendor payload to release directory",
    "vhs-teletext vendor payload missing under",
    "Bundled vhs-teletext vendor payload to $vendorDst",
    "release\\\\vendor\\\\vhs-teletext\\\\teletext\\\\__main__.py",
    "release\\\\vendor\\\\vhs-teletext\\\\misc\\\\teletext-noscanlines.css",
    "release\\\\vendor\\\\vhs-teletext\\\\misc\\\\teletext2.ttf",
    "release\\\\vendor\\\\vhs-teletext\\\\misc\\\\teletext4.ttf",
)
# macOS .app must bundle the same vendored vhs-teletext tree at
# Contents/MacOS/vendor/vhs-teletext. The "Build with Nix" loop uses
# `[ -f "$item" ]` which skips the result/bin/vendor directory (same gap as
# AAA), so an explicit restore is required or the teletext HTML export fails
# with "Could not locate vendored vhs-teletext runtime directory."
MACOS_TELETEXT_VENDOR_REQUIRED_SNIPPETS = (
    "Bundled vhs-teletext vendor payload to $TELETEXT_VENDOR_DST",
    "result/bin/vendor/vhs-teletext",
    "dist/tbc-tools.app/Contents/MacOS/vendor/vhs-teletext",
    "Missing vhs-teletext vendor payload: dist/tbc-tools.app/Contents/MacOS/vendor/vhs-teletext/teletext/__main__.py",
)
# The universal macOS merge must preserve vendored payloads from the per-arch
# app bundles and keep tbc-video-export universalized; otherwise final release
# assets can pass per-arch checks but ship a broken universal app.
MACOS_UNIVERSAL_VENDOR_REQUIRED_SNIPPETS = (
    "Validate universal bundled vendor payloads",
    "$APP_UNI/Contents/MacOS/vendor/vhs_decode_auto_audio_align/VhsDecodeAutoAudioAlign.exe",
    "$APP_UNI/Contents/MacOS/vendor/vhs_decode_auto_audio_align/Binah.dll",
    "$APP_UNI/Contents/MacOS/vendor/vhs-teletext/teletext/__main__.py",
    "$APP_UNI/Contents/MacOS/vendor/vhs-teletext/misc/teletext-noscanlines.css",
    "$APP_UNI/Contents/MacOS/vendor/vhs-teletext/misc/teletext2.ttf",
    "$APP_UNI/Contents/MacOS/vendor/vhs-teletext/misc/teletext4.ttf",
    "Missing universal bundled payload: $required_payload",
    "Merged bundled exporter is not a Mach-O binary: $EXPORTER_PATH",
    "Universal tbc-video-export archs:",
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
        AAA_LINUX_BUILD_SCRIPT,
        AAA_LINUX_PACKAGE_SCRIPT,
        LD_ANALYSE_EXPORT_DIALOG,
        TBC_VIDEO_EXPORT_OPTS_FFMPEG,
        TBC_VIDEO_EXPORT_FIELD_ORDER,
        AGENTS_RULES_FILE,
        TESTS_WORKFLOW,
        CUDA_CLOSURE_CACHE_SCRIPT,
        WIN_CUDA_RUNTIME_SCRIPT,
        CUDA_PLUGIN_PUBLISH_WORKFLOW,
        CUDA_PLUGIN_PACKAGE_SCRIPT,
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
    for snippet in WINDOWS_CACHE_PUSH_AUTH_REQUIRED_SNIPPETS:
        check_contains(WINDOWS_WORKFLOW, snippet, errors)

    for snippet in LINUX_REQUIRED_SNIPPETS:
        check_contains(LINUX_WORKFLOW, snippet, errors)
    for snippet in LINUX_AAA_FORBIDDEN_SNIPPETS:
        check_not_contains(LINUX_WORKFLOW, snippet, errors)
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
    # The AAA build-from-source + package-AppImage step must appear in both Linux
    # jobs (x86 container + arm64) so neither ships the prebuilt Windows .exe.
    check_count_at_least(LINUX_WORKFLOW, "Build and package native AAA AppImage from vendored source", 2, errors)

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
    # Windows release must bundle the vendored vhs-teletext Python tree
    # (release\vendor\vhs-teletext) so ld-process-vbi's teletext HTML export can
    # resolve the vendor directory next to the exe. The step is not arch-gated
    # (arch-independent Python source) and must appear exactly once.
    for snippet in WINDOWS_TELETEXT_VENDOR_REQUIRED_SNIPPETS:
        check_contains(WINDOWS_WORKFLOW, snippet, errors)
    win_teletext_step_count = WINDOWS_WORKFLOW.read_text(encoding="utf-8").count(
        "Copy vhs-teletext vendor payload to release directory"
    )
    if win_teletext_step_count != 1:
        errors.append(
            f"{WINDOWS_WORKFLOW}: vhs-teletext vendor copy step must appear exactly once, "
            f"found {win_teletext_step_count}"
        )
    # macOS .app must bundle the same vendored vhs-teletext tree at
    # Contents/MacOS/vendor/vhs-teletext (the Nix copy loop skips directories).
    for snippet in MACOS_TELETEXT_VENDOR_REQUIRED_SNIPPETS:
        check_contains(MACOS_WORKFLOW, snippet, errors)
    macos_teletext_restore_count = MACOS_WORKFLOW.read_text(encoding="utf-8").count(
        "Bundled vhs-teletext vendor payload to $TELETEXT_VENDOR_DST"
    )
    if macos_teletext_restore_count != 1:
        errors.append(
            f"{MACOS_WORKFLOW}: vhs-teletext vendor restore must appear exactly once, "
            f"found {macos_teletext_restore_count}"
        )
    # The universal macOS merge must preserve vendor payloads and the bundled
    # exporter architecture checks; guard the explicit validation block.
    for snippet in MACOS_UNIVERSAL_VENDOR_REQUIRED_SNIPPETS:
        check_contains(MACOS_WORKFLOW, snippet, errors)
    macos_universal_vendor_check_count = MACOS_WORKFLOW.read_text(encoding="utf-8").count(
        "Validate universal bundled vendor payloads"
    )
    if macos_universal_vendor_check_count != 1:
        errors.append(
            f"{MACOS_WORKFLOW}: universal vendor payload validation block must appear exactly once, "
            f"found {macos_universal_vendor_check_count}"
        )
    # The fetch script must pin the exact wheel versions (guards against a silent
    # cuDNN 8.x -> 9.x bump, which would break the ORT 1.18.x CUDA-11.x EP).
    for snippet in WIN_CUDA_RUNTIME_SCRIPT_REQUIRED_SNIPPETS:
        check_contains(WIN_CUDA_RUNTIME_SCRIPT, snippet, errors)
    # The CUDA plugin publish workflow must build + publish both platform
    # packages + manifests to tbc-tools-ci-cache Releases.
    for snippet in CUDA_PLUGIN_PUBLISH_REQUIRED_SNIPPETS:
        check_contains(CUDA_PLUGIN_PUBLISH_WORKFLOW, snippet, errors)
    # The packaging script must pin the NVIDIA wheel versions + produce the
    # trimmed provider set.
    for snippet in CUDA_PLUGIN_PACKAGE_SCRIPT_REQUIRED_SNIPPETS:
        check_contains(CUDA_PLUGIN_PACKAGE_SCRIPT, snippet, errors)
    # The CUDA closure cache is self-built and unsigned, so the restore must
    # import it with require-sigs disabled or Nix refuses the paths with
    # "cannot add path ... because it lacks a signature by a trusted key" and
    # CI silently falls back to cache.nixos.org (no insulation).
    check_contains(CUDA_CLOSURE_CACHE_SCRIPT, "--option require-sigs false", errors)
    # The AAA Linux build script must restore Binah from the vendored local
    # nupkg (no network fetch) and build only the main project; the package
    # script must bundle the distro Mono runtime so the AppImage runs without
    # host Mono (the "mono not found on Ubuntu" fix).
    for snippet in AAA_LINUX_BUILD_SCRIPT_REQUIRED_SNIPPETS:
        check_contains(AAA_LINUX_BUILD_SCRIPT, snippet, errors)
    for snippet in LINUX_AAA_XBUILD_REQUIRED_SNIPPETS:
        check_contains(AAA_LINUX_BUILD_SCRIPT, snippet, errors)
    for snippet in AAA_LINUX_PACKAGE_SCRIPT_REQUIRED_SNIPPETS:
        check_contains(AAA_LINUX_PACKAGE_SCRIPT, snippet, errors)

    for snippet in AGENTS_HARD_RULE_REQUIRED_SNIPPETS:
        check_contains(AGENTS_RULES_FILE, snippet, errors)

    win_cache_auth_scrub_count = WINDOWS_WORKFLOW.read_text(encoding="utf-8").count(
        "config --local --unset-all http.https://github.com/.extraheader"
    )
    if win_cache_auth_scrub_count != 1:
        errors.append(
            f"{WINDOWS_WORKFLOW}: windows cache auth header scrub must appear exactly once, "
            f"found {win_cache_auth_scrub_count}"
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
