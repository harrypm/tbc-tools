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

    def test_release_contract_wires_all_platform_packaging_workflows(self) -> None:
        expected = {
            "uses: ./.github/workflows/build_linux_tools.yml",
            "uses: ./.github/workflows/build_windows_tools.yml",
            "uses: ./.github/workflows/build_macos_tools.yml",
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


if __name__ == "__main__":
    unittest.main()
