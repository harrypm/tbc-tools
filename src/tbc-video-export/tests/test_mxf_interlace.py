from __future__ import annotations

import shutil
import struct
import subprocess
from typing import TYPE_CHECKING

import pytest

from tbc_video_export.common.enums import VideoSystem
from tbc_video_export.common.mxf_interlace import fix_mxf_interlace
from tbc_video_export.config.config import GetProfileFilter
from tbc_video_export.program_state import ProgramState  # noqa: TC001

if TYPE_CHECKING:
    from collections.abc import Callable
    from pathlib import Path

from tests.conftest import get_path

ffmpeg = shutil.which("ffmpeg")
ffprobe = shutil.which("ffprobe")
needs_ffmpeg = pytest.mark.skipif(ffmpeg is None or ffprobe is None, reason="ffmpeg/ffprobe not found")

# FFmpeg's field_order labels name the stored field first; map them to the
# field that is displayed first.
DISPLAYED_FIRST = {"tt": "tff", "bt": "tff", "bb": "bff", "tb": "bff"}


def _encode(tmp_path: Path, codec: list[str], setfield: str = "tff") -> Path:
    out = tmp_path / "test.mxf"
    subprocess.run(  # noqa: S603
        [str(ffmpeg), "-v", "error", "-y", "-f", "lavfi", "-i", "testsrc2=s=720x576:r=25",
         "-frames:v", "2", "-vf", f"setfield={setfield},format=yuv422p10le",
         "-flags", "+ilme+ildct", *codec, str(out)],
        check=True,
    )
    return out


def _probe(path: Path) -> tuple[str, str]:
    result = subprocess.run(  # noqa: S603
        [str(ffprobe), "-v", "error", "-select_streams", "v", "-show_entries",
         "stream=field_order,height", "-of", "csv=p=0", str(path)],
        check=True, capture_output=True, text=True,
    )
    height, order = result.stdout.strip().split(",")[:2]
    return order, height


def _frames_md5(path: Path) -> str:
    return subprocess.run(  # noqa: S603
        [str(ffmpeg), "-v", "error", "-i", str(path), "-map", "0:v", "-f", "md5", "-"],
        check=True, capture_output=True, text=True,
    ).stdout


@needs_ffmpeg
def test_prores_full_frame_becomes_interlaced_tff(tmp_path: Path) -> None:  # noqa: D103
    mxf = _encode(tmp_path, ["-c:v", "prores_ks", "-profile:v", "3"])
    assert _probe(mxf)[0] == "progressive"  # FFmpeg's MXF muxer drops it
    size, picture = mxf.stat().st_size, _frames_md5(mxf)

    changes = fix_mxf_interlace(mxf, top_field_first=True, line_map=(23, 336))

    assert any("separate fields" in c for c in changes)
    assert any("field dominance added" in c for c in changes)
    order, height = _probe(mxf)
    assert DISPLAYED_FIRST[order] == "tff"
    assert height == "576"  # readers still see the full frame
    # header metadata only: same size, identical decoded picture
    assert mxf.stat().st_size == size
    assert _frames_md5(mxf) == picture
    # idempotent
    assert fix_mxf_interlace(mxf, top_field_first=True, line_map=(23, 336)) == []


@needs_ffmpeg
@pytest.mark.parametrize(
    ("line_map", "tff"),
    [((23, 336), True), ((23, 336), False), ((21, 333), False), ((21, 333), True)],
)
def test_field_dominance_follows_storage_parity(  # noqa: D103
    tmp_path: Path, line_map: tuple[int, int], tff: bool
) -> None:
    # FieldDominance is relative to the stored-first field, which readers take
    # from the line map parity; the displayed order must match either way.
    mxf = _encode(tmp_path, ["-c:v", "prores_ks", "-profile:v", "3"])
    fix_mxf_interlace(mxf, top_field_first=tff, line_map=line_map)
    assert DISPLAYED_FIRST[_probe(mxf)[0]] == ("tff" if tff else "bff")


@needs_ffmpeg
def test_ffv1_bottom_first_dominance_is_corrected(tmp_path: Path) -> None:  # noqa: D103
    mxf = _encode(tmp_path, ["-c:v", "ffv1"])
    # FFmpeg flags TFF as "tb" -> FieldDominance 2 (bottom displayed first)
    assert DISPLAYED_FIRST[_probe(mxf)[0]] == "bff"

    changes = fix_mxf_interlace(mxf, top_field_first=True, line_map=(23, 336))

    assert changes == ["field dominance 2 -> 1"]  # FFmpeg's 23/336 map is kept
    assert DISPLAYED_FIRST[_probe(mxf)[0]] == "tff"


def test_refuses_without_header_fill(tmp_path: Path) -> None:  # noqa: D103
    # A full-frame descriptor followed directly by essence: inserting the
    # FieldDominance tag would have to move essence bytes, so it must refuse.
    tags = b"".join(
        struct.pack(">HH", tag, len(value)) + value
        for tag, value in (
            (0x3202, (576).to_bytes(4, "big")),
            (0x320C, b"\x00"),
            (0x320D, struct.pack(">IIii", 2, 4, 46, 0)),
        )
    )
    descriptor = bytes.fromhex("060e2b34025301010d01010101012800") + bytes([0x83, 0, 0, len(tags)]) + tags
    essence = bytes.fromhex("060e2b34010201010d01030115011700") + bytes([0x83, 0, 0, 4]) + b"DATA"
    mxf = tmp_path / "nofill.mxf"
    mxf.write_bytes(descriptor + essence)

    with pytest.raises(ValueError, match="no KLV fill"):
        fix_mxf_interlace(mxf, top_field_first=True, line_map=(23, 336))
    assert mxf.read_bytes() == descriptor + essence  # untouched


@pytest.mark.parametrize("system", [VideoSystem.SECAM, VideoSystem.MESECAM])
def test_secam_uses_pal_geometry_variant(  # noqa: D103
    program_state: Callable[[list[str], str, str | None], ProgramState],
    system: VideoSystem,
) -> None:
    # D10 only has PAL/NTSC variants; SECAM-family sources are 625-line.
    state = program_state([], f"{get_path('pal_svideo')}.tbc", "out_file")
    profile = state.config.get_profile(GetProfileFilter("d10", None, system))
    assert profile.video_profile.name == "d10_mpeg2_pal"
    # a dedicated secam variant still wins where one exists
    web = state.config.get_profile(GetProfileFilter("h264_web", None, system))
    assert web.video_profile.name == "x264_web_secam"
