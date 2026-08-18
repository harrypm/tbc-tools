from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from ci import check_ci_contracts


class SnippetHelperTests(unittest.TestCase):
    def test_check_contains_reports_missing_snippet(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("alpha beta", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_contains(fixture, "gamma", errors)

            self.assertEqual(len(errors), 1)
            self.assertIn("missing required snippet", errors[0])

    def test_check_contains_ignores_present_snippet(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("alpha beta gamma", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_contains(fixture, "beta", errors)

            self.assertEqual(errors, [])

    def test_check_count_at_least_reports_under_minimum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("token\ntoken\n", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_count_at_least(fixture, "token", 3, errors)

            self.assertEqual(len(errors), 1)
            self.assertIn("at least 3 times, found 2", errors[0])

    def test_check_count_at_least_accepts_exact_minimum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("token\ntoken\ntoken\n", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_count_at_least(fixture, "token", 3, errors)

            self.assertEqual(errors, [])

    def test_check_not_contains_reports_forbidden_snippet(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("alpha forbidden beta", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_not_contains(fixture, "forbidden", errors)

            self.assertEqual(len(errors), 1)
            self.assertIn("forbidden snippet present", errors[0])

    def test_check_not_contains_ignores_absent_snippet(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("alpha beta gamma", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_not_contains(fixture, "forbidden", errors)

            self.assertEqual(errors, [])


class ContractCoverageTests(unittest.TestCase):
    def test_windows_contract_covers_packaging_trigger_and_launch_check(self) -> None:
        expected = {
            "workflow_dispatch:",
            "workflow_call:",
            "tbc-video-export.exe --dump-default-config",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.WINDOWS_REQUIRED_SNIPPETS)))

    def test_macos_contract_covers_packaging_trigger_and_launch_check(self) -> None:
        expected = {
            "workflow_dispatch:",
            "workflow_call:",
            "tbc-tools.app/Contents/MacOS/tbc-video-export --version",
            "/nix/store/*)",
            "dep_unique_name()",
            "verify_bundled_dependencies()",
            "Unresolved bundled dependency:",
            "Bundled dependency verification failed.",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.MACOS_REQUIRED_SNIPPETS)))

    def test_macos_contract_covers_aaa_vendor_and_pyinstaller_self_containment(self) -> None:
        expected = {
            "Bundled AAA vendor payload to $AAA_VENDOR_DST",
            "Build self-contained tbc-video-export (PyInstaller)",
            "tbc-video-export is not a Mach-O binary",
            "Missing AAA vendor payload: dist/tbc-tools.app/Contents/MacOS/vendor/vhs_decode_auto_audio_align/VhsDecodeAutoAudioAlign.exe",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.MACOS_REQUIRED_SNIPPETS)))

    def test_windows_contract_covers_aaa_vendor_bundling(self) -> None:
        expected = {
            "Copy AAA (Auto Audio Align) vendor payload to release directory",
            "AAA vendor payload missing under",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.WINDOWS_REQUIRED_SNIPPETS)))

    def test_linux_contract_covers_pyinstaller_self_containment(self) -> None:
        expected = {
            "requirements-build-linux.txt",
            "pyinstaller/build_linux.py",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.LINUX_REQUIRED_SNIPPETS)))

    def test_macos_contract_forbids_host_dependency_capture(self) -> None:
        expected_forbidden = {
            "/usr/local/*|/opt/homebrew/*",
        }
        self.assertTrue(
            expected_forbidden.issubset(set(check_ci_contracts.MACOS_FORBIDDEN_SNIPPETS))
        )

    def test_macos_contract_pins_runners_and_uses_flake_ffmpeg(self) -> None:
        # Both macOS runners must be pinned to macos-15 (macos-latest migrated to
        # macos-26 on 2026-07-15, breaking the pinned nixpkgs Qt uic), and ffmpeg
        # must come from the flake's pinned nixpkgs (.#ffmpeg^bin) -- not the
        # live channel, which dropped x86_64-darwin in Nixpkgs 26.11.
        expected = {
            "macos-15-intel",
            "runner: macos-15",
            "nix build .#ffmpeg^bin",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.MACOS_REQUIRED_SNIPPETS)))

    def test_macos_contract_forbids_drifting_runner_and_live_channel_ffmpeg(self) -> None:
        # Forbid the precise code patterns (not the bare labels, which the
        # explanatory comments mention) so the arm64 runner cannot drift back to
        # macos-latest and the ffmpeg fallback cannot revert to the live channel.
        expected_forbidden = {
            "runner: macos-latest",
            "nix build nixpkgs#ffmpeg.bin",
        }
        self.assertTrue(
            expected_forbidden.issubset(set(check_ci_contracts.MACOS_FORBIDDEN_SNIPPETS))
        )

    def test_macos_workflow_uses_pinned_runner_and_flake_ffmpeg(self) -> None:
        # Integration-level: the actual workflow must contain the pinned arm64
        # runner + flake ffmpeg, and must NOT contain a drifting runner or a
        # live-channel ffmpeg invocation.
        content = check_ci_contracts.MACOS_WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("runner: macos-15", content)
        self.assertIn("nix build .#ffmpeg^bin", content)
        self.assertNotIn("runner: macos-latest", content)
        self.assertNotIn("nix build nixpkgs#ffmpeg.bin", content)

    def test_release_contract_wires_all_platform_packaging_workflows(self) -> None:
        expected = {
            "uses: ./.github/workflows/build_linux_tools.yml",
            "uses: ./.github/workflows/build_windows_tools.yml",
            "uses: ./.github/workflows/build_macos_tools.yml",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.RELEASE_REQUIRED_SNIPPETS)))

    def test_release_contract_requires_stale_release_guards(self) -> None:
        # Manual create_release must run from main, block existing-tag rebuilds
        # by default, validate the tag commit against the intended source commit,
        # emit a release commit provenance asset, and force make_latest=true so
        # the GitHub "latest release" pointer tracks the newest tag.
        expected = {
            "allow_existing_tag_rebuild:",
            "Manual release mode is only allowed from refs/heads/main",
            "Refusing to rebuild by default to prevent stale releases.",
            "Release integrity check failed:",
            'gh release edit "$RELEASE_TAG" --repo "${{ github.repository }}" --latest',
            "tbc-tools_${RELEASE_TAG}_commit.txt",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.RELEASE_REQUIRED_SNIPPETS)))

    def test_bundle_verifier_contract_includes_found_and_launch_checks(self) -> None:
        expected = {
            'run_smoke_test "x86-appimage-extract-and-run-tbc-video-export"',
            'run_smoke_test "x86-appimage-apprun-tbc-video-export"',
            'run_smoke_test "arm64-launcher-tbc-video-export"',
            'require_path "$ROOT/usr/bin/tbc-video-export"',
            'require_path "$TARGET/bin/tbc-video-export"',
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.BUNDLE_VERIFY_REQUIRED_SNIPPETS)))

    def test_bundle_verifier_contract_includes_teletext_vendor_checks(self) -> None:
        expected = {
            'require_path "$ROOT/usr/bin/vendor/vhs-teletext/teletext/__main__.py"',
            'require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext-noscanlines.css"',
            'require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext2.ttf"',
            'require_path "$ROOT/usr/bin/vendor/vhs-teletext/misc/teletext4.ttf"',
            'require_path "$TARGET/bin/vendor/vhs-teletext/teletext/__main__.py"',
            'require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext-noscanlines.css"',
            'require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext2.ttf"',
            'require_path "$TARGET/bin/vendor/vhs-teletext/misc/teletext4.ttf"',
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.BUNDLE_VERIFY_REQUIRED_SNIPPETS)))

    def test_bundle_verifier_contract_includes_aaa_vendor_and_elf_checks(self) -> None:
        expected = {
            'require_path "$ROOT/usr/bin/vendor/vhs_decode_auto_audio_align/VhsDecodeAutoAudioAlign.exe"',
            'require_path "$ROOT/usr/bin/vendor/vhs_decode_auto_audio_align/Binah.dll"',
            'require_path "$TARGET/bin/vendor/vhs_decode_auto_audio_align/VhsDecodeAutoAudioAlign.exe"',
            'require_path "$TARGET/bin/vendor/vhs_decode_auto_audio_align/Binah.dll"',
            'tbc-video-export is not an ELF binary',
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.BUNDLE_VERIFY_REQUIRED_SNIPPETS)))

    def test_ld_analyse_contract_forbids_hand_rolled_bwdif_parity_graph(self) -> None:
        expected_forbidden = {
            "bwdif=mode=send_frame:parity=auto:deint=all",
        }
        self.assertTrue(
            expected_forbidden.issubset(set(check_ci_contracts.LD_ANALYSE_FORBIDDEN_SNIPPETS))
        )

    def test_ld_analyse_contract_requires_tbc_video_export_web_profile_routing(self) -> None:
        expected = {
            "proxy deinterlace routed via tbc-video-export web profile",
            "proxyExportProfileName",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.LD_ANALYSE_REQUIRED_SNIPPETS)))

    def test_tbc_video_export_contract_requires_auto_field_order_default(self) -> None:
        expected = {
            "default=FieldOrder.AUTO",
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.TBC_VIDEO_EXPORT_REQUIRED_SNIPPETS))
        )

    def test_check_not_contains_flags_hand_rolled_bwdif_literal(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "exportdialog.cpp"
            fixture.write_text(
                'filterChain << QStringLiteral("bwdif=mode=send_frame:parity=auto:deint=all");',
                encoding="utf-8",
            )
            errors: list[str] = []
            check_ci_contracts.check_not_contains(
                fixture, "bwdif=mode=send_frame:parity=auto:deint=all", errors
            )
            self.assertEqual(len(errors), 1)
            self.assertIn("forbidden snippet present", errors[0])

    def test_check_contains_requires_proxy_web_profile_marker(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "exportdialog.cpp"
            fixture.write_text(
                "// proxy deinterlace routed via tbc-video-export web profile\n"
                "proxyExportProfileName(proxyCodecForCurrentRun);",
                encoding="utf-8",
            )
            errors: list[str] = []
            check_ci_contracts.check_contains(
                fixture, "proxy deinterlace routed via tbc-video-export web profile", errors
            )
            self.assertEqual(errors, [])

    def test_linux_cuda_cache_contract_requires_pull_restore_step(self) -> None:
        expected = {
            "Restore pinned CUDA 11.8 closure from tbc-tools-ci-cache",
            "bash scripts/cuda-closure-cache.sh pull",
            "bash scripts/cuda-closure-cache.sh restore",
            "tbc-tools-ci-cache",
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.LINUX_CUDA_CACHE_REQUIRED_SNIPPETS))
        )

    def test_tests_cuda_cache_contract_requires_pull_restore_step(self) -> None:
        expected = {
            "Restore pinned CUDA 11.8 closure from tbc-tools-ci-cache",
            "bash scripts/cuda-closure-cache.sh pull",
            "bash scripts/cuda-closure-cache.sh restore",
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.TESTS_CUDA_CACHE_REQUIRED_SNIPPETS))
        )

    def test_cuda_closure_cache_script_and_tests_workflow_are_required_files(self) -> None:
        # The contract must guard both the script and the tests workflow so the
        # CUDA closure restore cannot be silently removed from either path.
        required_paths = {
            check_ci_contracts.CUDA_CLOSURE_CACHE_SCRIPT,
            check_ci_contracts.TESTS_WORKFLOW,
        }
        for path in required_paths:
            self.assertTrue(path.exists(), f"required contract file missing: {path}")

    def test_linux_workflow_has_exactly_one_cuda_cache_restore_step(self) -> None:
        # The restore step must appear exactly once (x86_64 job only) -- never
        # in the arm64 job where enableCuda=false.
        content = check_ci_contracts.LINUX_WORKFLOW.read_text(encoding="utf-8")
        count = content.count("Restore pinned CUDA 11.8 closure from tbc-tools-ci-cache")
        self.assertEqual(
            count,
            1,
            f"expected exactly one CUDA cache restore step in Linux workflow, found {count}",
        )

    def test_windows_gas_preprocessor_contract_requires_insulation_step(self) -> None:
        expected = {
            "Pre-fetch gas-preprocessor.pl for arm64 ffmpeg",
            "vcpkg_find_acquire_program(GASPREPROCESSOR)",
            "cdn.jsdelivr.net/gh/FFmpeg/gas-preprocessor",
            "raw.githubusercontent.com/FFmpeg/gas-preprocessor",
            "Get-FileHash -Algorithm SHA512",
            "if: matrix.arch == 'arm64'",
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.WINDOWS_GAS_PREPROCESSOR_REQUIRED_SNIPPETS))
        )

    def test_windows_workflow_has_exactly_one_gas_preprocessor_step(self) -> None:
        # The gas-preprocessor pre-fetch step is gated to arm64 and must appear
        # exactly once in build_windows_tools.yml.
        content = check_ci_contracts.WINDOWS_WORKFLOW.read_text(encoding="utf-8")
        count = content.count("Pre-fetch gas-preprocessor.pl for arm64 ffmpeg")
        self.assertEqual(
            count,
            1,
            f"expected exactly one gas-preprocessor pre-fetch step in Windows workflow, found {count}",
        )

    def test_cuda_closure_restore_disables_signature_requirement(self) -> None:
        # The CUDA closure cache is self-built and unsigned; the restore must
        # import it with require-sigs disabled or Nix refuses the paths and CI
        # silently falls back to cache.nixos.org (no insulation).
        content = check_ci_contracts.CUDA_CLOSURE_CACHE_SCRIPT.read_text(encoding="utf-8")
        self.assertIn(
            "--option require-sigs false",
            content,
            "cuda-closure-cache.sh restore must use --option require-sigs false for the unsigned local cache",
        )

    def test_windows_cuda_runtime_contract_requires_bundle_step(self) -> None:
        expected = {
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
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.WINDOWS_CUDA_RUNTIME_REQUIRED_SNIPPETS))
        )

    def test_windows_workflow_has_exactly_one_cuda_runtime_bundle_step(self) -> None:
        # The CUDA runtime DLL bundle step is gated to x86_64 and must appear
        # exactly once in build_windows_tools.yml.
        content = check_ci_contracts.WINDOWS_WORKFLOW.read_text(encoding="utf-8")
        count = content.count(
            "Pull + bundle CUDA 11.8 + cuDNN 8.9 runtime DLLs (x86_64 only)"
        )
        self.assertEqual(
            count,
            1,
            f"expected exactly one CUDA runtime DLL bundle step in Windows workflow, found {count}",
        )

    def test_windows_cuda_runtime_script_pins_wheel_versions(self) -> None:
        # The fetch script must pin the exact NVIDIA wheel versions so a silent
        # upstream bump (e.g. cuDNN 8.x -> 9.x, ABI-incompatible with ORT 1.18.x
        # CUDA-11.x) cannot slip in unnoticed.
        expected = {
            "nvidia-cuda-runtime-cu11|11.8.89|",
            "nvidia-cublas-cu11|11.11.3.6|",
            "nvidia-cufft-cu11|10.9.0.58|",
            "nvidia-cudnn-cu11|8.9.5.29|",
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.WIN_CUDA_RUNTIME_SCRIPT_REQUIRED_SNIPPETS))
        )

    def test_windows_cuda_runtime_script_is_a_required_file(self) -> None:
        self.assertTrue(
            check_ci_contracts.WIN_CUDA_RUNTIME_SCRIPT.exists(),
            f"required contract file missing: {check_ci_contracts.WIN_CUDA_RUNTIME_SCRIPT}",
        )

    def test_windows_teletext_vendor_contract_requires_bundle_step(self) -> None:
        # The Windows release must bundle the vendored vhs-teletext Python tree
        # at release\vendor\vhs-teletext or ld-process-vbi's teletext HTML export
        # fails with "Could not locate vendored vhs-teletext runtime directory."
        expected = {
            "Copy vhs-teletext vendor payload to release directory",
            "vhs-teletext vendor payload missing under",
            "Bundled vhs-teletext vendor payload to $vendorDst",
            "release\\\\vendor\\\\vhs-teletext\\\\teletext\\\\__main__.py",
            "release\\\\vendor\\\\vhs-teletext\\\\misc\\\\teletext-noscanlines.css",
            "release\\\\vendor\\\\vhs-teletext\\\\misc\\\\teletext2.ttf",
            "release\\\\vendor\\\\vhs-teletext\\\\misc\\\\teletext4.ttf",
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.WINDOWS_TELETEXT_VENDOR_REQUIRED_SNIPPETS))
        )

    def test_windows_workflow_has_exactly_one_teletext_vendor_step(self) -> None:
        content = check_ci_contracts.WINDOWS_WORKFLOW.read_text(encoding="utf-8")
        count = content.count("Copy vhs-teletext vendor payload to release directory")
        self.assertEqual(
            count,
            1,
            f"expected exactly one vhs-teletext vendor copy step in Windows workflow, found {count}",
        )

    def test_macos_teletext_vendor_contract_requires_restore(self) -> None:
        expected = {
            "Bundled vhs-teletext vendor payload to $TELETEXT_VENDOR_DST",
            "result/bin/vendor/vhs-teletext",
            "dist/tbc-tools.app/Contents/MacOS/vendor/vhs-teletext",
            "Missing vhs-teletext vendor payload: dist/tbc-tools.app/Contents/MacOS/vendor/vhs-teletext/teletext/__main__.py",
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.MACOS_TELETEXT_VENDOR_REQUIRED_SNIPPETS))
        )

    def test_macos_workflow_has_exactly_one_teletext_vendor_restore(self) -> None:
        content = check_ci_contracts.MACOS_WORKFLOW.read_text(encoding="utf-8")
        count = content.count("Bundled vhs-teletext vendor payload to $TELETEXT_VENDOR_DST")
        self.assertEqual(
            count,
            1,
            f"expected exactly one vhs-teletext vendor restore in macOS workflow, found {count}",
        )

    def test_macos_universal_vendor_contract_requires_validation(self) -> None:
        expected = {
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
        }
        self.assertTrue(
            expected.issubset(set(check_ci_contracts.MACOS_UNIVERSAL_VENDOR_REQUIRED_SNIPPETS))
        )

    def test_macos_workflow_has_exactly_one_universal_vendor_validation_block(self) -> None:
        content = check_ci_contracts.MACOS_WORKFLOW.read_text(encoding="utf-8")
        count = content.count("Validate universal bundled vendor payloads")
        self.assertEqual(
            count,
            1,
            f"expected exactly one universal macOS vendor validation block, found {count}",
        )


if __name__ == "__main__":
    unittest.main()
