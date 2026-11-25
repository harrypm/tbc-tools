from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import TYPE_CHECKING

from tbc_video_export.process.process_handler import ProcessHandler
from tests.conftest import get_path

if TYPE_CHECKING:
    from collections.abc import Callable

    import pytest
    from pytest_mock import MockFixture

    from tbc_video_export.program_state import ProgramState

PROBE_ANAMORPHIC = {
    "streams": [
        {
            "index": 0,
            "width": 1136,
            "height": 626,
            "sample_aspect_ratio": "5:6",
        }
    ]
}

PROBE_SQUARE = {
    "streams": [
        {
            "index": 0,
            "width": 1136,
            "height": 626,
            "sample_aspect_ratio": "1:1",
        }
    ]
}


def _make_output_file(state: ProgramState) -> Path:
    output_file = state.file_helper.output_video_file
    output_file.write_bytes(b"ORIGINAL")
    return output_file


def _patch_process_tools(  # noqa: D103
    mocker: MockFixture,
    probe: dict,
    mkvmerge_available: bool,
    mkvpropedit_available: bool = True,
) -> list[list[str]]:
    """Patch ffprobe/mkvmerge subprocesses and return the commands run."""
    commands: list[list[str]] = []

    def fake_run(command: list[str], **_kwargs: object):
        commands.append(command)

        if Path(command[0]).name == "ffprobe":
            return subprocess.CompletedProcess(
                command, 0, stdout=json.dumps(probe), stderr=""
            )

        if Path(command[0]).name == "mkvpropedit":
            return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
        # mkvmerge: create the file it was told to write
        out_path = Path(command[command.index("--output") + 1])
        out_path.write_bytes(b"REMUXED")
        return subprocess.CompletedProcess(command, 0, stdout="", stderr="")

    which_results = {
        "ffprobe": "/usr/bin/ffprobe",
        "mkvmerge": "/usr/bin/mkvmerge" if mkvmerge_available else None,
        "mkvpropedit": "/usr/bin/mkvpropedit" if mkvpropedit_available else None,
    }

    mocker.patch(
        "tbc_video_export.process.process_handler.shutil.which",
        side_effect=lambda name, **_kwargs: which_results.get(name),
    )
    mocker.patch(
        "tbc_video_export.process.process_handler.subprocess.run",
        side_effect=fake_run,
    )

    return commands


def test_normalize_mkv_display_aspect_anamorphic(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
    mocker: MockFixture,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.chdir(tmp_path)
    state = program_state([], f"{get_path('pal_svideo')}.tbc", "out_file")
    output_file = _make_output_file(state)

    commands = _patch_process_tools(mocker, PROBE_ANAMORPHIC, True)
    ProcessHandler(state)._normalize_mkv_display_aspect()  # noqa: SLF001

    # ffprobe ran, then mkvmerge with pixel-unit display dimensions
    assert len(commands) == 2
    assert commands[1][commands[1].index("--timestamp-scale") + 1] == "1"
    assert commands[1][commands[1].index("--display-dimensions") + 1] == "0:947x626"

    # output atomically replaced with the remuxed file, no temp leftovers
    assert output_file.read_bytes() == b"REMUXED"
    assert not list(tmp_path.glob("*.remux-*.mkv"))

    assert any(
        "Normalized MKV display dimensions to 947x626" in message.message
        for message in state.export.messages
    )


PROBE_ANAMORPHIC_TFF = {
    "streams": [dict(PROBE_ANAMORPHIC["streams"][0], field_order="tb")]
}


def test_normalize_mkv_display_aspect_keeps_field_order(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
    mocker: MockFixture,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.chdir(tmp_path)
    state = program_state([], f"{get_path('pal_svideo')}.tbc", "out_file")
    output_file = _make_output_file(state)

    commands = _patch_process_tools(mocker, PROBE_ANAMORPHIC_TFF, True)
    ProcessHandler(state)._normalize_mkv_display_aspect()  # noqa: SLF001

    # ffprobe, mkvmerge, then mkvpropedit restoring what mkvmerge drops
    assert [Path(c[0]).name for c in commands] == ["ffprobe", "mkvmerge", "mkvpropedit"]
    assert commands[2][commands[2].index("--edit") + 1] == "track:v1"
    assert "interlaced=1" in commands[2]
    assert "field-order=9" in commands[2]
    assert ".remux-" in commands[2][2]
    assert output_file.read_bytes() == b"REMUXED"


def test_normalize_mkv_display_aspect_skips_without_mkvpropedit(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
    mocker: MockFixture,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.chdir(tmp_path)
    state = program_state([], f"{get_path('pal_svideo')}.tbc", "out_file")
    output_file = _make_output_file(state)

    commands = _patch_process_tools(mocker, PROBE_ANAMORPHIC_TFF, True, mkvpropedit_available=False)
    ProcessHandler(state)._normalize_mkv_display_aspect()  # noqa: SLF001

    # interlaced output keeps FFmpeg's field-order-flagged file untouched
    assert len(commands) == 1
    assert output_file.read_bytes() == b"ORIGINAL"
    assert any("preserve the field order" in m.message for m in state.export.messages)


def test_normalize_mkv_display_aspect_skips_square(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
    mocker: MockFixture,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.chdir(tmp_path)
    state = program_state([], f"{get_path('pal_svideo')}.tbc", "out_file")
    output_file = _make_output_file(state)

    # ProgramState.export is a class-level shared singleton; start clean
    state.export.messages.clear()

    commands = _patch_process_tools(mocker, PROBE_SQUARE, True)
    ProcessHandler(state)._normalize_mkv_display_aspect()  # noqa: SLF001

    # only ffprobe ran, file untouched, no messages
    assert len(commands) == 1
    assert output_file.read_bytes() == b"ORIGINAL"
    assert not state.export.messages


def test_normalize_mkv_display_aspect_warns_without_mkvmerge(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
    mocker: MockFixture,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.chdir(tmp_path)
    state = program_state([], f"{get_path('pal_svideo')}.tbc", "out_file")
    output_file = _make_output_file(state)

    # ProgramState.export is a class-level shared singleton; start clean
    state.export.messages.clear()

    commands = _patch_process_tools(mocker, PROBE_ANAMORPHIC, False)
    ProcessHandler(state)._normalize_mkv_display_aspect()  # noqa: SLF001

    # only ffprobe ran, file untouched, warning message added
    assert len(commands) == 1
    assert output_file.read_bytes() == b"ORIGINAL"
    assert any(
        "mkvmerge not found" in message.message for message in state.export.messages
    )


def test_normalize_mkv_display_aspect_skips_non_mkv(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
    mocker: MockFixture,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.chdir(tmp_path)

    # force a mov container
    state = program_state(
        ["--profile", "prores_hq"], f"{get_path('pal_svideo')}.tbc", "out_file"
    )
    commands = _patch_process_tools(mocker, PROBE_ANAMORPHIC, True)

    ProcessHandler(state)._normalize_mkv_display_aspect()  # noqa: SLF001

    assert not commands


def test_mono_chroma_decoder_on_yc_source_exports_luma_only(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
) -> None:
    # MONO on a split Y+C source means "process the Y TBC only": luma export
    # mode, so no chroma pass (MONO cannot decode one) and no error.
    from tbc_video_export.common.enums import ExportMode

    state = program_state(["--chroma-decoder", "mono"], f"{get_path('pal_svideo')}.tbc", "out_file")
    assert state.export_mode is ExportMode.LUMA

    # without MONO the same source still merges Y and C
    state = program_state([], f"{get_path('pal_svideo')}.tbc", "out_file")
    assert state.export_mode is ExportMode.CHROMA_MERGE
