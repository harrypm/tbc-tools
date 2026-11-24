from __future__ import annotations

import dataclasses

from dataclasses import dataclass
from fractions import Fraction
from typing import Literal, TypeAlias

from tbc_video_export.common.enums import ChromaDecoder, ExportMode, VideoSystem


@dataclass(frozen=True, slots=True)
class VideoSystemData:
    """Base VideoSystem data class.

    Holds static values for the video system.

    Active line values from (default):
    vhs_decode/tools/library/tbc/lddecodemetadata.cpp

    Aspect ratio values from (default/widescreen):
    vhs_decode/tools/tbc-chroma-decoder/outputwriter.cpp
    """

    size: dict[VideoSizeType, Size]
    active_lines: dict[VideoActiveLinesType, ActiveLines]
    aspect_ratio: dict[VideoAspectRatioType, AspectRatio]
    chroma_decoder: dict[ExportMode, ChromaDecoder]
    ffmpeg_config: FFmpegConfig
    fps_fraction: Fraction

    @staticmethod
    def get(system: VideoSystem) -> VideoSystemData:
        """Returns VideoSystemData for the specified video format."""
        match system:
            case VideoSystem.PAL:
                return video_system_pal

            case VideoSystem.NTSC:
                return video_system_ntsc

            case VideoSystem.PAL_M:
                return video_system_palm

            case VideoSystem.SECAM | VideoSystem.MESECAM:
                # SECAM/MESECAM share PAL's 625-line geometry and 25 fps but
                # are FM-chroma systems: they default to the SECAM chroma
                # decoder (see video_system_secam below). MONO is opt-in.
                return video_system_secam

    @dataclass(frozen=True, slots=True)
    class Size:
        """Width/height data container."""

        width: int
        height: int

    @dataclass(frozen=True, slots=True)
    class AspectRatio:
        """Aspect ratio container."""

        horizontal: int
        vertical: int

    @dataclass(frozen=True, slots=True)
    class ActiveLines:
        """Active lines data container."""

        first_field: int
        last_field: int
        first_frame: int
        last_frame: int
        padding: int | None

    @dataclass(frozen=True, slots=True)
    class FFmpegConfig:
        """FFmpeg config data container."""

        color_range: str
        color_space: str
        color_primaries: str
        color_trc: str
        fps: str
        # Colour system signalled in the stream (H.264/H.265 VUI
        # video_format, MPEG-2 sequence_display_extension): pal/ntsc/secam.
        # Colour primaries/matrix/transfer codes (H.273) do not distinguish
        # SECAM from PAL; this is where SECAM is identified as SECAM.
        video_format: str


video_system_pal = VideoSystemData(
    size={
        "default": VideoSystemData.Size(928, 576),  # unused
        "4fsc": VideoSystemData.Size(1135, 626),
    },
    active_lines={
        "default": VideoSystemData.ActiveLines(22, 308, 44, 620, None),  # unused
        "full_vertical": VideoSystemData.ActiveLines(2, 308, 2, 620, None),
        "letterbox": VideoSystemData.ActiveLines(2, 308, 118, 548, None),
        "vbi": VideoSystemData.ActiveLines(12, 308, 12, 620, None),
    },
    aspect_ratio={
        "default": VideoSystemData.AspectRatio(259, 311),  # unused
        "widescreen": VideoSystemData.AspectRatio(865, 779),
        "letterbox": VideoSystemData.AspectRatio(16, 9),
    },
    chroma_decoder={
        # LUMA mode (luma-only export of an S-Video pair) never decodes
        # chroma, but the table must be total: decoder_chroma is evaluated
        # unconditionally in some code paths and a missing key would raise.
        ExportMode.LUMA: ChromaDecoder.MONO,
        ExportMode.LUMA_EXTRACTED: ChromaDecoder.MONO,
        ExportMode.CHROMA_MERGE: ChromaDecoder.PAL2D,
        ExportMode.CHROMA_COMBINED: ChromaDecoder.TRANSFORM3D,
        ExportMode.CHROMA_COMBINED_LD: ChromaDecoder.TRANSFORM3D,
    },
    ffmpeg_config=VideoSystemData.FFmpegConfig(
        "tv",
        "bt470bg",
        "bt470bg",
        "bt470bg",  # BT.470 2.8-gamma transfer (gamma28) for 625-line systems
        "pal",
        "pal",
    ),
    fps_fraction=Fraction(25),
)

video_system_ntsc = VideoSystemData(
    size={
        "default": VideoSystemData.Size(760, 488),  # unused
        "4fsc": VideoSystemData.Size(910, 526),
    },
    active_lines={
        "default": VideoSystemData.ActiveLines(20, 259, 40, 525, None),  # unused
        "full_vertical": VideoSystemData.ActiveLines(1, 259, 2, 525, None),
        "letterbox": VideoSystemData.ActiveLines(61, 224, 122, 448, 1),  # unsure!
        "vbi": VideoSystemData.ActiveLines(16, 259, 17, 525, 2),  # unsure!
    },
    aspect_ratio={
        "default": VideoSystemData.AspectRatio(352, 413),  # unused
        "widescreen": VideoSystemData.AspectRatio(25, 22),
        "letterbox": VideoSystemData.AspectRatio(16, 9),
    },
    chroma_decoder={
        ExportMode.LUMA: ChromaDecoder.MONO,
        ExportMode.LUMA_EXTRACTED: ChromaDecoder.MONO,
        ExportMode.CHROMA_MERGE: ChromaDecoder.NTSC2D,
        ExportMode.CHROMA_COMBINED: ChromaDecoder.NTSC3D,
        ExportMode.CHROMA_COMBINED_LD: ChromaDecoder.NTSC2D,
    },
    ffmpeg_config=VideoSystemData.FFmpegConfig(
        "tv",
        "smpte170m",
        "smpte170m",
        "bt709",
        "ntsc",
        "ntsc",
    ),
    fps_fraction=Fraction(30000, 1001),
)

video_system_palm = VideoSystemData(
    size={
        "default": VideoSystemData.Size(760, 488),  # unused
        "4fsc": VideoSystemData.Size(909, 526),
    },
    active_lines={
        "default": VideoSystemData.ActiveLines(20, 259, 40, 525, None),  # unused
        "full_vertical": VideoSystemData.ActiveLines(1, 259, 2, 525, None),
        "letterbox": VideoSystemData.ActiveLines(0, 0, 0, 0, 0),  # Sample required!
        "vbi": VideoSystemData.ActiveLines(16, 259, 17, 525, 2),  # unsure!
    },
    aspect_ratio={
        "default": VideoSystemData.AspectRatio(352, 413),  # unused
        "widescreen": VideoSystemData.AspectRatio(25, 22),
        "letterbox": VideoSystemData.AspectRatio(16, 9),
    },
    chroma_decoder={
        ExportMode.LUMA: ChromaDecoder.MONO,
        ExportMode.LUMA_EXTRACTED: ChromaDecoder.MONO,
        ExportMode.CHROMA_MERGE: ChromaDecoder.PAL2D,
        ExportMode.CHROMA_COMBINED: ChromaDecoder.TRANSFORM3D,
        ExportMode.CHROMA_COMBINED_LD: ChromaDecoder.TRANSFORM3D,
    },
    ffmpeg_config=VideoSystemData.FFmpegConfig(
        "tv",
        "bt470bg",
        "bt470bg",
        "bt709",
        "ntsc",
        "pal",  # PAL-M: PAL colour on 525/59.94
    ),
    fps_fraction=Fraction(30000, 1001),
)

VideoSizeType: TypeAlias = Literal["default", "4fsc"]
VideoActiveLinesType: TypeAlias = Literal[
    "default", "full_vertical", "letterbox", "vbi"
]
VideoAspectRatioType: TypeAlias = Literal["default", "widescreen", "letterbox"]

# SECAM/MESECAM: 625-line FM-chroma systems. They share PAL's line geometry,
# aspect ratios, active-line ranges, and ffmpeg colour config. Chroma modes
# default to the SECAM decoder — MONO cannot decode chroma (the merged export
# needs U/V planes from the chroma pass, and MONO emits GRAY16, which fails
# the merge and leaves an empty output file). Kept as a distinct
# VideoSystemData instance so VideoSystemData.get() can return it
# without special-casing, while still reflecting that SECAM is its own system.
video_system_secam = VideoSystemData(
    size=video_system_pal.size,
    active_lines=video_system_pal.active_lines,
    aspect_ratio=video_system_pal.aspect_ratio,
    chroma_decoder={
        ExportMode.LUMA: ChromaDecoder.MONO,
        ExportMode.LUMA_EXTRACTED: ChromaDecoder.MONO,
        ExportMode.CHROMA_MERGE: ChromaDecoder.SECAM,
        ExportMode.CHROMA_COMBINED: ChromaDecoder.SECAM,
        ExportMode.CHROMA_COMBINED_LD: ChromaDecoder.SECAM,
    },
    # Same 625-line colour tagging as PAL, but signalled as SECAM.
    ffmpeg_config=dataclasses.replace(video_system_pal.ffmpeg_config, video_format="secam"),
    fps_fraction=video_system_pal.fps_fraction,
)
