from __future__ import annotations

import asyncio
import copy
import json
import logging
import shutil
from pathlib import Path
from typing import TYPE_CHECKING

from tbc_video_export.common import FileHelper, exceptions
from tbc_video_export.common.enums import ProcessName
from tbc_video_export.common.utils import interrupts
from tbc_video_export.process.process_handler import ProcessHandler
from tbc_video_export.program_state import ProgramState

if TYPE_CHECKING:
    from argparse import ArgumentParser

    from tbc_video_export.config.config import Config
    from tbc_video_export.opts.opts import Opts


def _base_active_lines(state: ProgramState) -> tuple[int, int, int, int]:
    """Return the global (ffll, lfll, ffrl, lfrl) the segments are offset from.

    Resolved as the chroma decoder wrapper does: user opts, then a non-default
    preset, then the TBC json, then the video system default.
    """
    opts = state.opts
    default = state.video_system_data.active_lines["default"]
    if opts.contains_active_line_opts():
        return (
            opts.first_active_field_line or default.first_field,
            opts.last_active_field_line or default.last_field,
            opts.first_active_frame_line or default.first_frame,
            opts.last_active_frame_line or default.last_frame,
        )
    if state.decoder_line_preset != default:
        preset = state.decoder_line_preset
        return preset.first_field, preset.last_field, preset.first_frame, preset.last_frame
    json = state.tbc_json
    return (
        json.first_active_field_line or default.first_field,
        json.last_active_field_line or default.last_field,
        json.first_active_frame_line or default.first_frame,
        json.last_active_frame_line or default.last_frame,
    )


def _shift_filter(offset: int, axis: str) -> str:
    """Return an ffmpeg filter that moves the picture by |offset| along |axis|.

    The output keeps its size: the side the picture moves away from is cropped
    and the other padded black, as a window moved past the picture edge shows.
    """
    amount = abs(offset)
    if axis == "x":
        crop_x = amount if offset > 0 else 0
        pad_x = 0 if offset > 0 else amount
        return f"crop=iw-{amount}:ih:{crop_x}:0,pad=iw+{amount}:ih:{pad_x}:0"
    crop_y = amount if offset > 0 else 0
    pad_y = 0 if offset > 0 else amount
    return f"crop=iw:ih-{amount}:0:{crop_y},pad=iw:ih+{amount}:0:{pad_y}"


def _segment_opts(state: ProgramState, segment: dict[str, int], output: Path) -> Opts:
    """Return a copy of the opts that exports one segment with its framing.

    An offset moves the global active area: inside the picture the decoder
    decodes the moved window; past the picture edge the global window is decoded
    and moved in ffmpeg with black fill. Either way the size stays the same.
    """
    opts = copy.copy(state.opts)
    opts.output_file = str(output)
    opts.start = segment["start"]
    opts.length = segment["end"] - segment["start"] + 1
    opts.ignore_segments = True
    opts.overwrite = True
    # the joined file carries the json once
    opts.no_attach_json = True
    shift_filters: list[str] = []

    if (offset_x := segment["offset_x"]) != 0:
        start = state.opts.active_video_start or state.tbc_json.active_video_start
        end = state.opts.active_video_end or state.tbc_json.active_video_end
        width = state.tbc_json.field_width
        if start is not None and end is not None and width is not None and (
            0 <= start + offset_x and end + offset_x <= width
        ):
            opts.active_video_start = start + offset_x
            opts.active_video_end = end + offset_x
        else:
            shift_filters.append(_shift_filter(offset_x, "x"))

    if (offset_y := segment["offset_y"]) != 0:
        if offset_y % 2 != 0:
            raise exceptions.InvalidOptsError(
                f"Segment {segment['start']}-{segment['end']} vertical offset {offset_y} must "
                "be even: an odd line shift swaps the field order."
            )
        ffll, lfll, ffrl, lfrl = _base_active_lines(state)
        default = state.video_system_data.active_lines["default"]
        field_offset = offset_y // 2
        if (
            ffll + field_offset >= 1
            and lfll + field_offset <= default.last_field
            and ffrl + offset_y >= 1
            and lfrl + offset_y <= default.last_frame
        ):
            # frame lines move by the offset, field lines by half of it
            opts.first_active_field_line = ffll + field_offset
            opts.last_active_field_line = lfll + field_offset
            opts.first_active_frame_line = ffrl + offset_y
            opts.last_active_frame_line = lfrl + offset_y
        else:
            shift_filters.append(_shift_filter(offset_y, "y"))

    opts.segment_shift_filter = ",".join(shift_filters) if shift_filters else None
    return opts


async def run_segmented_export(
    state: ProgramState, parser: ArgumentParser, config: Config
) -> bool:
    """Export each segment with its framing and join them into one file.

    Returns True on success.
    """
    from tbc_video_export.opts import opt_validators

    console = logging.getLogger("console")
    segments = state.tbc_json.user_segments
    final_output = state.file_helper.output_video_file
    if final_output.exists() and not state.opts.overwrite:
        raise exceptions.FileIOError(
            f"{final_output} exists, use --overwrite or move them."
        )

    work_dir = final_output.parent / f".{final_output.stem}.segments"
    work_dir.mkdir(parents=True, exist_ok=True)
    parts: list[Path] = []
    try:
        for index, segment in enumerate(segments, start=1):
            console.info(
                f"Segment {index}/{len(segments)}: frames {segment['start']}-{segment['end']}"
                f" (offset x {segment['offset_x']}, y {segment['offset_y']})"
            )
            opts = _segment_opts(state, segment, work_dir / f"segment_{index:03d}")
            files = FileHelper(opts, config)
            segment_state = ProgramState(opts, config, files)
            opt_validators.validate_opts(segment_state, parser, opts)
            handler = ProcessHandler(segment_state)
            with interrupts.InterruptHandler(handler):
                await handler.run()
            if not handler.completed_successfully:
                console.error(f"Segment {index} failed.")
                return False
            parts.append(files.output_video_file)

        if state.opts.dry_run:
            return True

        list_file = work_dir / "segments.txt"
        list_file.write_text(
            "".join(f"file '{p.resolve().as_posix()}'\n" for p in parts), encoding="utf-8"
        )
        recorded = json.dumps(
            [
                {
                    "start": s["start"],
                    "end": s["end"],
                    "offsetX": s["offset_x"],
                    "offsetY": s["offset_y"],
                }
                for s in segments
            ],
            separators=(",", ":"),
        )
        command = [
            str(state.file_helper.tools[ProcessName.FFMPEG]),
            "-hide_banner", "-loglevel", "error", "-y",
            "-f", "concat", "-safe", "0", "-i", str(list_file),
        ]
        if state.file_helper.output_container.lower() == "mkv" and not state.opts.no_attach_json:
            command += [
                "-attach", str(state.file_helper.tbc_json.file_name),
                "-metadata:s:t:0", "mimetype=application/json",
            ]
        command += ["-map", "0", "-c", "copy", "-metadata", f"tbc_segments={recorded}",
                    str(final_output)]

        console.info(f"Joining {len(parts)} segments into {final_output}")
        process = await asyncio.create_subprocess_exec(*command)
        if await process.wait() != 0:
            console.error("Joining the segments failed.")
            return False
        return True
    finally:
        if not state.opts.dry_run:
            shutil.rmtree(work_dir, ignore_errors=True)
