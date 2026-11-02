from __future__ import annotations

import os
import stat
from pathlib import Path
from tempfile import NamedTemporaryFile
from typing import TYPE_CHECKING

import pytest

from tbc_video_export.common import exceptions
from tbc_video_export.common.enums import ProcessName, TBCType, VideoSystem
from tbc_video_export.common.file_helper import FileHelper
from tbc_video_export.common.utils import files

from .conftest import FileHelperTestCase

if TYPE_CHECKING:
    from collections.abc import Callable

    from tbc_video_export.program_state import ProgramState


def _converter_available() -> bool:
    """Return True if the tbc-metadata-converter binary can be found."""
    try:
        files.find_binary(
            "tbc-metadata-converter.exe" if os.name == "nt" else "tbc-metadata-converter"
        )
    except exceptions.FileIOError:
        return False
    return True


class TestTBCJsonSqliteAutoConvert:
    """Tests for auto-converting SQLite metadata to JSON on load."""

    SQLITE_FIXTURE = Path("tests/files/orc_db_only.tbc.db")

    def _stage_sqlite_only(
        self, tmp_path: Path, read_only: bool = False
    ) -> Path:
        """Stage a decode-orc Y/C capture with SQLite-only metadata.

        Returns the .tbcy input path inside the staged directory.
        """
        (tmp_path / "cap.tbcy").touch()
        (tmp_path / "cap.tbcc").touch()
        shutil_copy = Path(__file__).parent / "files" / "orc_db_only.tbc.db"
        target_db = tmp_path / "cap.tbc.db"
        target_db.write_bytes(shutil_copy.read_bytes())

        if read_only:
            os.chmod(
                tmp_path,
                stat.S_IRUSR
                | stat.S_IXUSR
                | stat.S_IRGRP
                | stat.S_IXGRP
                | stat.S_IROTH
                | stat.S_IXOTH,
            )

        return tmp_path / "cap.tbcy"

    def test_sqlite_auto_converts_to_json(
        self,
        program_state: Callable[[list[str], Path], ProgramState],
        tmp_path: Path,
    ) -> None:
        if not _converter_available():
            pytest.skip("tbc-metadata-converter not found in PATH or script dir.")

        input_tbc = self._stage_sqlite_only(tmp_path)
        state = program_state([], input_tbc)
        helper = FileHelper(state.opts, state.config)

        assert helper.tbc_json.video_system == VideoSystem.PAL

        # The converted json is placed next to the source for reuse.
        converted = tmp_path / "cap.tbc.json"
        assert converted.is_file()
        assert helper.tbc_json.file_name == converted

    def test_existing_json_takes_precedence_over_sqlite(
        self,
        program_state: Callable[[list[str], Path], ProgramState],
        tmp_path: Path,
    ) -> None:
        input_tbc = self._stage_sqlite_only(tmp_path)
        (tmp_path / "cap.tbc.json").write_text(
            Path("tests/files/pal_svideo.tbc.json").read_text()
        )

        state = program_state([], input_tbc)
        helper = FileHelper(state.opts, state.config)

        assert helper.tbc_json.file_name == tmp_path / "cap.tbc.json"
        assert helper.tbc_json.video_system == VideoSystem.PAL

    def test_input_tbc_json_pointing_at_db_converts(
        self,
        program_state: Callable[[list[str], Path], ProgramState],
        tmp_path: Path,
    ) -> None:
        if not _converter_available():
            pytest.skip("tbc-metadata-converter not found in PATH or script dir.")

        input_tbc = self._stage_sqlite_only(tmp_path)
        state = program_state(
            ["--input-tbc-json", str(tmp_path / "cap.tbc.db")], input_tbc
        )
        helper = FileHelper(state.opts, state.config)

        # The .db itself cannot be overwritten, so the converted json is a
        # temporary file that exists and parses.
        assert helper.tbc_json.file_name.suffix == ".json"
        assert helper.tbc_json.file_name.is_file()
        assert helper.tbc_json.file_name != tmp_path / "cap.tbc.db"
        assert helper.tbc_json.video_system == VideoSystem.PAL

    def test_read_only_source_dir_falls_back_to_temp(
        self,
        program_state: Callable[[list[str], Path], ProgramState],
        tmp_path: Path,
    ) -> None:
        if os.name == "nt":
            pytest.skip("read-only directory simulation is not reliable on Windows.")
        if not _converter_available():
            pytest.skip("tbc-metadata-converter not found in PATH or script dir.")

        input_tbc = self._stage_sqlite_only(tmp_path, read_only=True)
        try:
            state = program_state([], input_tbc)
            helper = FileHelper(state.opts, state.config)

            assert helper.tbc_json.video_system == VideoSystem.PAL
            # Nothing could be written next to the source; the json is a
            # temporary file outside the read-only directory.
            assert helper.tbc_json.file_name.parent != tmp_path
            assert not (tmp_path / "cap.tbc.json").exists()
        finally:
            # Restore permissions so pytest can clean up tmp_path.
            os.chmod(tmp_path, stat.S_IRWXU | stat.S_IRWXG | stat.S_IRWXO)


class TestTBCJson:
    """Tests for tbc json helper."""

    test_cases = [
        FileHelperTestCase(
            id="pal svideo",
            input_tbc=Path("tests/files/pal_svideo.tbc"),
            input_name=Path("tests/files/pal_svideo"),
            luma_tbc=Path("tests/files/pal_svideo.tbc"),
            output_name=Path("out_file"),
            output_container="mkv",
            output_video_file=Path("out_file.mkv"),
            output_video_file_luma=Path("out_file.luma.mkv"),
            is_ld=False,
            efm_file=None,
            ffmetadata_file=Path("out_file.ffmetadata"),
            cc_file=Path("out_file.scc"),
            tbc_types=TBCType.LUMA | TBCType.CHROMA,
        ),
        FileHelperTestCase(
            id="pal composite",
            input_tbc=Path("tests/files/pal_composite.tbc"),
            input_name=Path("tests/files/pal_composite"),
            luma_tbc=Path("tests/files/pal_composite.tbc"),
            output_name=Path("out_file"),
            output_container="mkv",
            output_video_file=Path("out_file.mkv"),
            output_video_file_luma=Path("out_file.luma.mkv"),
            is_ld=False,
            efm_file=None,
            ffmetadata_file=Path("out_file.ffmetadata"),
            cc_file=Path("out_file.scc"),
            tbc_types=TBCType.COMBINED,
        ),
        FileHelperTestCase(
            id="pal composite (ld)",
            input_tbc=Path("tests/files/pal_composite_ld.tbc"),
            input_name=Path("tests/files/pal_composite_ld"),
            luma_tbc=Path("tests/files/pal_composite_ld.tbc"),
            output_name=Path("out_file"),
            output_container="mkv",
            output_video_file=Path("out_file.mkv"),
            output_video_file_luma=Path("out_file.luma.mkv"),
            is_ld=True,
            efm_file=Path("tests/files/pal_composite_ld.efm"),
            ffmetadata_file=Path("out_file.ffmetadata"),
            cc_file=Path("out_file.scc"),
            tbc_types=TBCType.COMBINED,
        ),
        # decode-orc Y/C naming (issue #30): .tbcy luma + .tbcc chroma.
        FileHelperTestCase(
            id="decode-orc yc (tbcy + tbcc)",
            input_tbc=Path("tests/files/orc_yc.tbcy"),
            input_name=Path("tests/files/orc_yc"),
            luma_tbc=Path("tests/files/orc_yc.tbcy"),
            output_name=Path("out_file"),
            output_container="mkv",
            output_video_file=Path("out_file.mkv"),
            output_video_file_luma=Path("out_file.luma.mkv"),
            is_ld=False,
            efm_file=None,
            ffmetadata_file=Path("out_file.ffmetadata"),
            cc_file=Path("out_file.scc"),
            tbc_types=TBCType.LUMA | TBCType.CHROMA,
            chroma_tbc=Path("tests/files/orc_yc.tbcc"),
        ),
        # decode-orc luma-only Y/C capture: a lone .tbcy is a luma TBC and
        # must never be treated as combined.
        FileHelperTestCase(
            id="decode-orc luma only (tbcy)",
            input_tbc=Path("tests/files/orc_y_only.tbcy"),
            input_name=Path("tests/files/orc_y_only"),
            luma_tbc=Path("tests/files/orc_y_only.tbcy"),
            output_name=Path("out_file"),
            output_container="mkv",
            output_video_file=Path("out_file.mkv"),
            output_video_file_luma=Path("out_file.luma.mkv"),
            is_ld=False,
            efm_file=None,
            ffmetadata_file=Path("out_file.ffmetadata"),
            cc_file=Path("out_file.scc"),
            tbc_types=TBCType.LUMA,
        ),
        # alternate Y/C naming also accepted by tbc-analyse: .ytbc + .ctbc.
        FileHelperTestCase(
            id="alternate yc (ytbc + ctbc)",
            input_tbc=Path("tests/files/orc_legacy.ytbc"),
            input_name=Path("tests/files/orc_legacy"),
            luma_tbc=Path("tests/files/orc_legacy.ytbc"),
            output_name=Path("out_file"),
            output_container="mkv",
            output_video_file=Path("out_file.mkv"),
            output_video_file_luma=Path("out_file.luma.mkv"),
            is_ld=False,
            efm_file=None,
            ffmetadata_file=Path("out_file.ffmetadata"),
            cc_file=Path("out_file.scc"),
            tbc_types=TBCType.LUMA | TBCType.CHROMA,
            chroma_tbc=Path("tests/files/orc_legacy.ctbc"),
        ),
        # a stem carrying both .tbc and .tbcy prefers the conventional .tbc
        # (and without a separate chroma stream it stays combined).
        FileHelperTestCase(
            id="mixed stem prefers .tbc",
            input_tbc=Path("tests/files/orc_mixed.tbcy"),
            input_name=Path("tests/files/orc_mixed"),
            luma_tbc=Path("tests/files/orc_mixed.tbc"),
            output_name=Path("out_file"),
            output_container="mkv",
            output_video_file=Path("out_file.mkv"),
            output_video_file_luma=Path("out_file.luma.mkv"),
            is_ld=False,
            efm_file=None,
            ffmetadata_file=Path("out_file.ffmetadata"),
            cc_file=Path("out_file.scc"),
            tbc_types=TBCType.COMBINED,
        ),
    ]

    @pytest.mark.parametrize(
        "test_case",
        (pytest.param(test_case, id=test_case.id) for test_case in test_cases),
    )
    def test_paths(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path], ProgramState],
        test_case: FileHelperTestCase,
    ) -> None:
        state = program_state([], test_case.input_tbc)
        helper = FileHelper(state.opts, state.config)

        assert test_case.input_name == helper.input_name
        assert test_case.luma_tbc == helper.tbc_luma
        assert test_case.output_name == helper.output_name
        assert test_case.output_container == helper.output_container
        assert test_case.output_video_file == helper.output_video_file
        assert test_case.output_video_file_luma == helper.output_video_file_luma
        assert test_case.is_ld == helper.is_combined_ld
        assert test_case.efm_file == helper.efm_file
        assert test_case.ffmetadata_file == helper.ffmetadata_file
        assert test_case.cc_file == helper.cc_file
        assert test_case.tbc_types == helper.tbc_types
        if test_case.chroma_tbc is not None:
            assert test_case.chroma_tbc == helper.tbcs[TBCType.CHROMA]

    def test_setting_json(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path], ProgramState],
    ) -> None:
        state = program_state([], Path("tests/files/pal_svideo.tbc"))
        helper = FileHelper(state.opts, state.config)

        tbc_json_helper = helper.tbc_json
        assert tbc_json_helper.video_system == VideoSystem.PAL

        helper.tbc_json = Path("tests/files/ntsc_svideo.tbc.json")
        tbc_json_helper = helper.tbc_json
        assert tbc_json_helper.video_system == VideoSystem.NTSC

    def test_missing_tbc(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path], ProgramState],
    ) -> None:
        state = program_state([], Path("tests/files/pal_svideo.tbc"))
        helper = FileHelper(state.opts, state.config)

        # Clear tbc locations to cause exception
        helper.tbcs.clear()
        with pytest.raises(exceptions.TBCError) as e:
            _ = helper.tbc_luma

            assert e.value == "Unable to find luma TBC."

        with (
            NamedTemporaryFile(suffix="_chroma.tbc") as file,
            pytest.raises(exceptions.TBCError) as e,
        ):
            state = program_state([], Path(file.name.replace("_chroma.tbc", "")))
            helper = FileHelper(state.opts, state.config)

            assert e.value == "TBC not found at location."

        with pytest.raises(exceptions.TBCError) as e:
            state = program_state([], Path("tests/files/invalid"))
            helper = FileHelper(state.opts, state.config)

            assert e.value == "Location contains chroma TBC but no luma TBC."

        # A lone decode-orc chroma file (.tbcc) without any luma TBC must be
        # rejected the same way as a lone _chroma.tbc (issue #30).
        with pytest.raises(exceptions.TBCError) as e:
            state = program_state([], Path("tests/files/orc_chroma_only.tbcc"))
            helper = FileHelper(state.opts, state.config)

            assert e.value == "Location contains chroma TBC but no luma TBC."

    procs = [
        ProcessName.FFMPEG,
        ProcessName.LD_CHROMA_DECODER,
        ProcessName.LD_DROPOUT_CORRECT,
        ProcessName.LD_EXPORT_METADATA,
        ProcessName.LD_PROCESS_EFM,
        ProcessName.LD_PROCESS_VBI,
    ]

    tbc_types = [
        TBCType.CHROMA,
        TBCType.COMBINED,
        TBCType.LUMA,
    ]

    @pytest.mark.parametrize("proc", procs)
    @pytest.mark.parametrize("tbc_type", tbc_types)
    def test_log_files(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path], ProgramState],
        proc: ProcessName,
        tbc_type: TBCType,
    ) -> None:
        state = program_state([], Path("tests/files/pal_svideo.tbc"))
        helper = FileHelper(state.opts, state.config)

        timestamp = "__timestamp__"

        assert helper.get_log_file(proc, tbc_type, timestamp) == Path(
            f"__timestamp___{helper.input_name.stem}_{str(proc)}_{str(tbc_type)}.log"
        )

    @pytest.mark.parametrize("proc", procs)
    def test_tools(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path], ProgramState],
        proc: ProcessName,
    ) -> None:
        state = program_state(
            [
                "--process-efm",
                "--process-vbi",
                "--export-metadata",
            ],
            Path("tests/files/pal_svideo.tbc"),
        )
        helper = FileHelper(state.opts, state.config)

        assert helper.tools[proc] == Path(str(proc))

    appimage_tbc_tools_procs = [
        ProcessName.LD_CHROMA_DECODER,
        ProcessName.LD_DROPOUT_CORRECT,
        ProcessName.LD_EXPORT_METADATA,
        ProcessName.LD_PROCESS_EFM,
        ProcessName.LD_PROCESS_VBI,
    ]

    def test_tools_appimage(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path], ProgramState],
    ) -> None:
        with NamedTemporaryFile() as file:
            state = program_state(
                [
                    "--process-efm",
                    "--process-vbi",
                    "--export-metadata",
                    "--tbc-tools-appimage",
                    file.name,
                ],
                Path("tests/files/pal_svideo.tbc"),
            )
            helper = FileHelper(state.opts, state.config)

            # ensure appimage only used for tbc-tools
            for k, v in helper.tools.items():
                if k in self.appimage_tbc_tools_procs:
                    assert v == [Path(file.name), str(k)]
                else:
                    assert v == Path(str(k))

    def test_out_file_dir(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path, str], ProgramState],
    ) -> None:
        state = program_state(
            [
                "--process-efm",
                "--process-vbi",
                "--export-metadata",
            ],
            Path("tests/files/pal_svideo.tbc"),
            "invalid_dir/test",
        )
        helper = FileHelper(state.opts, state.config)

        with pytest.raises(exceptions.FileIOError) as e:
            helper.check_output_dir()

            assert e.value == "Output directory does not exist (invalid_dir/test)."

    def test_out_file(  # noqa: D102
        self,
        program_state: Callable[[list[str], Path, str], ProgramState],
    ) -> None:
        with NamedTemporaryFile(suffix=".mkv") as file:
            state = program_state(
                [
                    "--process-efm",
                    "--process-vbi",
                    "--export-metadata",
                ],
                Path("tests/files/pal_svideo.tbc"),
                file.name,
            )
            helper = FileHelper(state.opts, state.config)

            with pytest.raises(exceptions.FileIOError) as e:
                helper.check_output_file()

                assert (
                    e.value == f"{file.name} exists, use --overwrite or move the file."
                )

        with NamedTemporaryFile(suffix=".luma.mkv") as file:
            state = program_state(
                [
                    "--process-efm",
                    "--process-vbi",
                    "--export-metadata",
                    "--two-step",
                ],
                Path("tests/files/pal_svideo.tbc"),
                file.name,
            )
            helper = FileHelper(state.opts, state.config)

            with pytest.raises(exceptions.FileIOError) as e:
                helper.check_output_file()

                assert (
                    e.value == f"{file.name} exists, use --overwrite or move the file."
                )
