from __future__ import annotations

import asyncio
from fractions import Fraction
from functools import cached_property
from pathlib import Path
from typing import TYPE_CHECKING

from tbc_video_export.common import consts, exceptions
from tbc_video_export.common.enums import (
    ExportMode,
    FieldOrder,
    HardwareAccelType,
    PipeType,
    ProcessName,
    TBCType,
    VideoFormatType,
    VideoSystem,
)
from tbc_video_export.common.utils import FlatList, ansi
from tbc_video_export.process.wrapper.wrapper import Wrapper

if TYPE_CHECKING:
    from tbc_video_export.config.profile import Profile, ProfileVideo
    from tbc_video_export.process.wrapper.pipe import Pipe
    from tbc_video_export.process.wrapper.wrapper import WrapperConfig
    from tbc_video_export.program_state import ProgramState


class WrapperFFmpeg(Wrapper):
    """Wrapper for ffmpeg that generates commands for encoding."""

    def __init__(
        self, state: ProgramState, config: WrapperConfig[tuple[Pipe], None]
    ) -> None:
        self._additional_vopts: FlatList = FlatList()
        self._hwaccel_opts: FlatList = FlatList()
        self._hwaccel_filter: str = ""
        super().__init__(state, config)
        self._config = config
        self._parse_hwaccel()

    def post_fn(self) -> None:  # noqa: D102
        pass

    @property
    def command(self) -> FlatList:  # noqa: D102
        return FlatList(
            (
                self.binary,
                self._get_verbosity_opts(),
                self._get_misc_opts(),
                self._get_hwaccel_opts(),
                self._get_input_opts(),
                self._get_filter_complex_opts(),
                self._get_map_opts(),
                self._get_timecode_opt(),
                self._get_framerate_opt(),
                self._get_codec_opts(),
                self._get_metadata_opts(),
                self._get_output_opt(),
            )
        )

    def _get_verbosity_opts(self) -> FlatList:
        """Return opts for verbosity."""
        verbosity_opts = FlatList("-hide_banner")

        # enable progress reporting if we are not displaying output
        if not self._state.opts.show_process_output:
            verbosity_opts.append(
                (
                    "-loglevel",
                    "error",
                    "-progress",
                    "pipe:2",
                )
            )
        else:
            verbosity_opts.append(("-loglevel", "verbose"))

        return verbosity_opts

    def _get_misc_opts(self) -> FlatList:
        opts = FlatList((self._state.opts.convert_opt("overwrite", "-y"),))

        thread_count = self._state.opts.threads

        if (t := self._state.opts.ffmpeg_threads) is not None:
            thread_count = t

        if thread_count != 0:
            opts.append(("-threads", thread_count))

        return opts

    def _parse_hwaccel(self) -> None:
        """Parse hardware acceleration opts."""
        match self._get_profile().video_profile.hardware_accel:
            case HardwareAccelType.VAAPI:
                self._hwaccel_opts.append(
                    ("-hwaccel", "vaapi", "-hwaccel_output_format", "vaapi")
                )

                if (hwaccel_device := self._state.opts.hwaccel_device) is not None:
                    self._hwaccel_opts.append(("-vaapi_device", hwaccel_device))

                self._hwaccel_filter = "hwupload"

            case HardwareAccelType.NVENC:
                if (hwaccel_device := self._state.opts.hwaccel_device) is not None:
                    self._additional_vopts.append(("-gpu", hwaccel_device))

            case HardwareAccelType.QUICKSYNC:
                self._hwaccel_opts.append(
                    (
                        "-hwaccel",
                        "qsv",
                        "-hwaccel_output_format",
                        "qsv",
                    )
                )

                if (hwaccel_device := self._state.opts.hwaccel_device) is not None:
                    self._hwaccel_opts.append(("-qsv_device", hwaccel_device))

            case HardwareAccelType.AMF:
                if self._state.opts.hwaccel_device is not None:
                    raise exceptions.InvalidProfileError(
                        "Unable to set device for AMD AMF encoding due to FFmpeg "
                        "limitiations."
                    )

            case _:
                pass

    def _get_hwaccel_opts(self) -> FlatList:
        return self._hwaccel_opts

    def _get_thread_queue_size_opt(self) -> FlatList:
        """Return opts for thread queue size."""
        return FlatList(("-thread_queue_size", str(self._state.opts.thread_queue_size)))

    def _get_input_opts(self) -> FlatList:
        """Return opts for all inputs."""
        return FlatList(
            (
                "-nostdin",
                self._get_video_input_opts(),
                self._get_audio_input_opts(),
                self._get_metadata_input_opts(),
            )
        )

    def _get_video_input_opts(self) -> FlatList:
        """Return opts for video input."""
        input_opts = FlatList()
        video_system_data = self._state.video_system_data

        if self._config.export_mode == ExportMode.LUMA_4FSC:
            size = video_system_data.size["4fsc"]

            input_opts.append(
                (
                    "-f",
                    "rawvideo",
                    "-pix_fmt",
                    VideoFormatType.GRAY.value.get(16),
                    "-framerate",
                    self._get_framerate(),
                    "-video_size",
                    f"{size.width}x{size.height}",
                )
            )

        inputs: list[str] = []

        if self._is_two_step_merge_mode():
            # add the luma file as an input
            inputs.append(str(self._state.file_helper.output_video_file_luma))

        # pipes
        for i in self._config.input_pipes:
            inputs.append(str(i.in_path))

        input_opts.append(
            (
                self._get_thread_queue_size_opt(),
                "-i",
                i,
            )
            for i in inputs
        )

        return input_opts

    def _get_audio_trim_opts(self) -> tuple[str | None, str | None]:
        """Return -ss and -t values for trimming audio inputs to match --start/--length."""  # noqa: E501
        if self._state.opts.start is None and self._state.opts.length is None:
            return None, None
        start_frame_one_based = self._state.opts.start
        if start_frame_one_based is None:
            start_frame_one_based = 1

        if (
            audio_trim_seconds := self._state.tbc_json.get_audio_trim_seconds(
                start_frame_one_based=start_frame_one_based,
                total_frames=self._state.total_frames,
            )
        ) is not None:
            audio_start_seconds, audio_duration_seconds = audio_trim_seconds
            ss = f"{audio_start_seconds:.6f}" if audio_start_seconds > 0 else None
            t = f"{audio_duration_seconds:.6f}" if audio_duration_seconds > 0 else None
            return ss, t

        fps = self._state.video_system_data.fps_fraction
        start_frame_zero_based = (
            max(0, start_frame_one_based - 1)
            if start_frame_one_based is not None
            else 0
        )

        ss = (
            f"{float(Fraction(start_frame_zero_based) / fps):.6f}"
            if start_frame_zero_based > 0
            else None
        )
        t = (
            f"{float(Fraction(self._state.total_frames) / fps):.6f}"
            if self._state.total_frames > 0
            else None
        )
        return ss, t

    def _get_audio_input_opts(self) -> FlatList:
        """Return opts for audio input."""
        input_opts = FlatList()
        audio_ss, audio_t = self._get_audio_trim_opts()

        # audio
        for track in self._state.opts.audio_track:
            if audio_ss is not None:
                input_opts.append(("-ss", audio_ss))
            if audio_t is not None:
                input_opts.append(("-t", audio_t))

            if (offset := track.offset) is not None:
                input_opts.append(("-itsoffset", offset))

            if (sample_format := track.sample_format) is not None:
                input_opts.append(("-f", sample_format))

            if (rate := track.rate) is not None:
                input_opts.append(("-ar", rate))

            if (channels := track.channels) is not None:
                input_opts.append(("-ac", channels))

            input_opts.append(("-i", track.file_name))

            # if the track does not exist, we add a warning to the message log as
            # the file may be generated by ld-process-efm and will exist by the time
            # FFmpeg runs
            # we should perhaps only init a wrapper when it is time to run?
            if not Path(track.file_name).is_file():
                self._state.export.append_message(
                    ansi.error_color(
                        f"FFmpeg track {track.file_name} does not currently exist."
                    )
                )

        return input_opts

    def _get_metadata_input_opts(self) -> FlatList:
        """Return opts for metadata input."""
        input_opts = FlatList()
        if self._state.opts.export_metadata:
            # subtitles
            if self._state.opts.dry_run:
                input_opts.append(("{-i", "[SUBTITLE_FILE]}"))
            elif (cc_file := self._state.file_helper.cc_file).is_file():
                input_opts.append(("-i", cc_file))

            # metadata
            if self._state.opts.dry_run:
                input_opts.append(("{-i", "[METADATA_FILE]}"))
            elif (ffmetadata := self._state.file_helper.ffmetadata_file).is_file():
                input_opts.append(("-i", ffmetadata))

        for ffmetadata in self._state.opts.metadata_file:
            input_opts.append(("-i", ffmetadata))

        return input_opts

    def _get_filters(self) -> tuple[list[str], list[str]]:
        """Return tuple containing video and other filters."""
        video_filters: list[str] = []
        other_filters: list[str] = []

        _vf, _of = self._state.config.get_profile_filters(self._get_profile())

        # set video filters
        video_filters.append(f"setfield={self._get_field_order()}")
        video_filters += _vf

        # override profile colorlevels if set with opt
        if self._state.opts.force_black_level is not None:
            video_filters.append(
                "colorlevels="
                f"rimin={self._state.opts.force_black_level[0]}/255:"
                f"gimin={self._state.opts.force_black_level[1]}/255:"
                f"bimin={self._state.opts.force_black_level[2]}/255"
            )
        if (standard_filter := self._get_standard_output_filter()) is not None:
            video_filters.append(standard_filter)

        if self._state.opts.append_video_filter is not None:
            video_filters.append(self._state.opts.append_video_filter)
        if (chroma_alignment_filter := self._get_chroma_alignment_filter()) is not None:
            video_filters.append(chroma_alignment_filter)
        if (aspect_ratio_filter := self._get_aspect_ratio_filter()) is not None:
            # Keep DAR/SAR adjustments near the end so earlier scale/pad filters
            # cannot override them.
            video_filters.append(aspect_ratio_filter)

        video_filters.append(f"format={self._get_profile_video_format()}")
        video_filters.append(self._get_setparams_filter())

        if self._state.opts.hwaccel_type is not None and self._hwaccel_filter:
            video_filters.append(self._hwaccel_filter)

        # set other filters
        other_filters += _of

        if self._state.opts.append_other_filter is not None:
            other_filters.append(self._state.opts.append_other_filter)

        return video_filters, other_filters

    def _get_filter_complex_opts(self) -> FlatList:  # noqa: C901, PLR0912
        """Return opts for filter complex."""
        video_filters, other_filters = self._get_filters()
        video_filters_str = ",".join(video_filters)
        other_filters_str = f",{of}" if (of := ",".join(other_filters)) else ""

        match self._config.export_mode:
            case ExportMode.CHROMA_MERGE:
                # merge Y/C from separate Y+C inputs

                # using mergeplanes 0x001112 with pipe+pipe input works but there
                # seems to be an issue when merging gray16le and 16-bit yuv(??) formats.
                # safer to extract the 2x inputs (file+pipe/pipe+pipe) into y/u/v planes
                # and merge to avoid any issues.

                mergeplanes = (
                    "0x001020"
                    if consts.FFMPEG_USE_OLD_MERGEPLANES
                    else "map1s=1:map2s=2"
                )

                complex_filter = (
                    f"[0:v]extractplanes=y[y];[1:v]extractplanes=u+v[u][v];"
                    f"[y][u][v]mergeplanes={mergeplanes}:format={consts.FFMPEG_DEFAULT_CHROMA_FORMAT},"
                )

            case ExportMode.LUMA_EXTRACTED:
                # extract Y from a Y/C input
                complex_filter = "[0:v]extractplanes=y,"

            case ExportMode.LUMA_4FSC:
                # interleve tbc fields
                complex_filter = "[0:v]il=l=i:c=i,"

            case _:
                complex_filter = "[0:v]"

                # luma step in two-step should only use setfield and setparams filters
                if self._is_two_step_luma_mode():
                    video_filters_str = ",".join(
                        [
                            f"setfield={self._get_field_order()}",
                            self._get_setparams_filter(),
                        ]
                    )
                    other_filters_str = ""

        return FlatList(
            (
                "-filter_complex",
                complex_filter
                + video_filters_str
                + consts.FFMPEG_VIDEO_MAP
                + other_filters_str,
            )
        )

    def _get_map_opts(self) -> FlatList:
        """Return FFmpeg video map opts."""
        # video
        input_opts = FlatList(("-map", f"{consts.FFMPEG_VIDEO_MAP}"))
        input_count = len(self._config.input_pipes)

        # audio
        input_opts.append(
            ("-map", f"{i + input_count}:a")
            for i in range(len(self._state.opts.audio_track))
        )
        input_count += len(self._state.opts.audio_track)

        if self._state.opts.export_metadata:
            # subtitles
            if self._state.opts.dry_run:
                input_opts.append(("{-map", "[SUBTITLE_INDEX]:s}"))
            elif self._state.file_helper.cc_file.is_file():
                input_opts.append(("-map", f"{input_count}:s"))
                input_count += 1

            # metadata
            if self._state.opts.dry_run:
                input_opts.append(("{-map_metadata", "[METADATA_INDEX]}"))
                input_opts.append(("{-map_chapters", "[METADATA_INDEX]}"))
            elif self._state.file_helper.ffmetadata_file.is_file():
                input_opts.append(("-map_metadata", input_count))
                input_opts.append(("-map_chapters", input_count))
                input_count += 1

        for _ in self._state.opts.metadata_file:
            input_opts.append(("-map_metadata", input_count))
            input_count += 1

        return input_opts

    def _get_timecode_opt(self) -> FlatList:
        """Return opts for timecode."""
        if self._state.opts.export_metadata:
            return FlatList()
        return FlatList(("-timecode", self._state.file_helper.tbc_json.timecode))

    def _get_framerate(self) -> str:
        """Return rate based on video system."""
        return self._state.video_system_data.ffmpeg_config.fps

    def _get_framerate_opt(self) -> FlatList:
        """Return opts for rate."""
        return FlatList(
            (
                "-framerate",
                self._get_framerate(),
            )
        )

    def _get_aspect_ratio_filter(self) -> str | None:
        """Return filter for aspect ratios."""
        if self._state.is_widescreen or self._state.opts.letterbox:
            # Explicitly target 16:9 DAR and let FFmpeg derive SAR for
            # non-square-pixel outputs.
            return "setdar=16/9"

        return None  # do not return default ar

    def _get_standard_output_filter(self) -> str | None:
        """Return auto D1 output sizing filter for --standard/--d1."""
        if not self._state.opts.standard:
            return None

        if (
            self._state.opts.vbi
            or self._state.opts.full_vertical
            or self._state.opts.letterbox
            or self._state.opts.full_frame
            or self._state.opts.luma_4fsc
        ):
            return None

        return (
            "scale=720:576:flags=lanczos:interl=1,setsar=128/117"
            if self._state.video_system is VideoSystem.PAL
            else "scale=720:486:flags=lanczos:interl=1,setsar=12/13"
        )

    def _is_web_profile(self) -> bool:
        """Return True when encoding with a web/deinterlaced profile."""
        profile_name = self._get_profile().name.lower()
        return profile_name == "web" or profile_name.endswith("_web")

    def _get_chroma_alignment_filter(self) -> str | None:
        """Return padding filter when selected codec/pix_fmt requires even dimensions."""
        codec = self._get_video_profile().codec.lower()
        if not (
            codec.startswith("libx264")
            or codec.startswith("h264_")
            or codec.startswith("libx265")
            or codec.startswith("hevc_")
            or codec in {"libsvtav1", "libaom-av1"}
        ):
            return None

        video_format = self._get_profile_video_format().lower()
        is_h264_family = codec.startswith("libx264") or codec.startswith("h264_")

        # 4:2:0 needs even width and height.
        if "420" in video_format:
            # Interlaced H.264 commonly requires height divisible by 4.
            # Web profiles are deinterlaced and only require even height.
            if is_h264_family:
                if self._is_web_profile():
                    return "pad=ceil(iw/2)*2:ceil(ih/2)*2:(ow-iw)/2:(oh-ih)/2"
                return "pad=ceil(iw/2)*2:ceil(ih/4)*4:(ow-iw)/2:(oh-ih)/2"
            return "pad=ceil(iw/2)*2:ceil(ih/2)*2:(ow-iw)/2:(oh-ih)/2"

        # 4:2:2 needs even width.
        if "422" in video_format:
            return "pad=ceil(iw/2)*2:ceil(ih/2)*2:(ow-iw)/2:(oh-ih)/2"

        return None

    def _get_setparams_filter(self) -> str:
        """Return filter for setparams."""
        ffmpeg_config = self._state.video_system_data.ffmpeg_config
        return (
            f"setparams="
            f"range={ffmpeg_config.color_range}:"
            f"colorspace={ffmpeg_config.color_space}:"
            f"color_primaries={ffmpeg_config.color_primaries}:"
            f"color_trc={ffmpeg_config.color_trc}"
        )

    def _get_codec_opts(self) -> FlatList:
        """Return opts containing codecs for inputs."""
        codec_opts = FlatList(
            (
                "-c:v",
                self._get_profile().video_profile.codec,
                self._get_video_codec_profile_opts(),
                self._additional_vopts,
            )
        )

        if (audio_profile := self._get_profile().audio_profile) is not None:
            codec_opts.append(
                (
                    "-c:a",
                    audio_profile.codec,
                    audio_profile.opts,
                )
            )

            if self._state.opts.export_metadata:
                if self._state.opts.dry_run:
                    codec_opts.append(("{-c:s", self._get_subtitle_format() + "}"))
                elif (
                    self._state.opts.export_metadata
                    and self._state.file_helper.cc_file.is_file()
                ):
                    codec_opts.append(("-c:s", self._get_subtitle_format()))

        return codec_opts

    def _get_video_codec_profile_opts(self) -> FlatList | None:
        """Return codec opts from profile with runtime compatibility adjustments."""
        if (opts := self._get_profile().video_profile.opts) is None:
            return None
        if (
            self._get_video_profile().codec.lower() != "ffv1"
            or not self._state.opts.full_frame
        ):
            return opts

        return self._get_ffv1_full_frame_compatible_opts(opts)

    def _get_ffv1_full_frame_compatible_opts(self, opts: FlatList) -> FlatList:
        """Ensure FFV1 full-frame exports use a slice count compatible with frame geometry."""
        compatible_slices = self._get_ffv1_full_frame_compatible_slices()
        if not compatible_slices:
            return opts

        recommended_slices = self._get_recommended_ffv1_full_frame_slices()
        if recommended_slices not in compatible_slices:
            recommended_slices = compatible_slices[0]

        adjusted_opts = list(opts.data)
        current_slices, slices_value_index = self._get_ffv1_slices_opt(adjusted_opts)

        if current_slices in compatible_slices:
            return opts

        if slices_value_index is None:
            adjusted_opts.extend(["-slices", str(recommended_slices)])
            self._state.export.append_message(
                f"FFV1 full-frame compatibility set slices to {recommended_slices} "
                f"for {str(self._state.video_system).upper()}."
            )
            return FlatList(adjusted_opts)

        previous_slices = adjusted_opts[slices_value_index]
        adjusted_opts[slices_value_index] = str(recommended_slices)
        self._state.export.append_message(
            f"FFV1 full-frame slices {previous_slices} are incompatible with "
            f"{str(self._state.video_system).upper()}; using {recommended_slices}."
        )
        return FlatList(adjusted_opts)

    def _get_ffv1_full_frame_compatible_slices(self) -> list[int]:
        """Return FFV1 slices known to be compatible with full-frame dimensions."""
        match self._state.video_system:
            case VideoSystem.PAL:
                return [6, 9, 15, 20, 25, 28]
            case VideoSystem.PAL_M:
                return [4, 6, 9]
            case VideoSystem.NTSC:
                return [4, 6, 9, 12, 15, 16, 20, 24, 25, 28, 30]
        return [4]

    def _get_recommended_ffv1_full_frame_slices(self) -> int:
        """Return recommended FFV1 slices value for full-frame exports."""
        return 20 if self._state.video_system is VideoSystem.PAL else 4

    def _get_ffv1_slices_opt(self, opts: list[str]) -> tuple[int | None, int | None]:
        """Return tuple containing current -slices value and its value index in opts."""
        for idx, value in enumerate(opts):
            if value != "-slices":
                continue
            if idx + 1 >= len(opts):
                return None, None
            try:
                return int(opts[idx + 1]), idx + 1
            except ValueError:
                return None, idx + 1
        return None, None

    def _get_metadata_opts(self) -> FlatList:
        """Return opts for metadata."""
        metadata_opts = FlatList(
            ("-metadata", f"{key}={value}")
            for key, value in self._get_automatic_metadata().items()
        )

        # custom metadata
        metadata_opts.append(
            ("-metadata", f"{data[0]}={data[1]}") for data in self._state.opts.metadata
        )

        # audio
        for idx, track in enumerate(self._state.opts.audio_track):
            if (title := track.title) is not None:
                metadata_opts.append((f"-metadata:s:a:{idx}", f"title={title}"))

            if (language := track.language) is not None:
                metadata_opts.append((f"-metadata:s:a:{idx}", f"language={language}"))

            if (layout := track.layout) is not None:
                metadata_opts.append((f"-channel_layout:a:{idx}", f"{layout}"))

        # attachment
        if self._get_supports_attachments() and not self._state.opts.no_attach_json:
            metadata_opts.append(
                (
                    "-attach",
                    self._state.file_helper.tbc_json.file_name,
                    "-metadata:s:t:0",
                    "mimetype=application/json",
                )
            )

        return metadata_opts

    def _get_automatic_metadata(self) -> dict[str, str]:
        """Return default metadata fields added to encoded outputs."""
        metadata: dict[str, str] = {
            "tbc_tools_application": consts.APPLICATION_NAME,
            "tbc_tools_version": consts.PROJECT_VERSION,
            "tbc_export_mode": self._config.export_mode.name.lower(),
            "tbc_export_chroma_decoder": self._get_export_chroma_decoder_metadata(),
        }

        if source_chroma_decoder := self._state.tbc_json.chroma_decoder:
            metadata["tbc_source_chroma_decoder"] = source_chroma_decoder

        if source_black_level := self._state.tbc_json.black_16b_ire:
            metadata["tbc_source_black_16b_ire"] = source_black_level

        if source_white_level := self._state.tbc_json.white_16b_ire:
            metadata["tbc_source_white_16b_ire"] = source_white_level

        if source_blanking_level := self._state.tbc_json.blanking_16b_ire:
            metadata["tbc_source_blanking_16b_ire"] = source_blanking_level

        if source_chroma_gain := self._state.tbc_json.chroma_gain:
            metadata["tbc_source_chroma_gain"] = source_chroma_gain

        if source_chroma_phase := self._state.tbc_json.chroma_phase:
            metadata["tbc_source_chroma_phase"] = source_chroma_phase

        if source_luma_nr := self._state.tbc_json.luma_nr:
            metadata["tbc_source_luma_nr"] = source_luma_nr

        if decode_git_branch := self._state.tbc_json.git_branch:
            metadata["tbc_decode_git_branch"] = decode_git_branch

        if decode_git_commit := self._state.tbc_json.git_commit:
            metadata["tbc_decode_git_commit"] = decode_git_commit

        if decode_version := self._get_decode_version_metadata():
            metadata["tbc_decode_version"] = decode_version

        if self._state.opts.force_black_level is not None:
            metadata["tbc_export_force_black_level"] = ",".join(
                str(v) for v in self._state.opts.force_black_level
            )

        if self._state.opts.chroma_gain is not None:
            metadata["tbc_export_chroma_gain"] = str(self._state.opts.chroma_gain)

        if self._state.opts.chroma_phase is not None:
            metadata["tbc_export_chroma_phase"] = str(self._state.opts.chroma_phase)

        if self._state.opts.luma_nr is not None:
            metadata["tbc_export_luma_nr"] = str(self._state.opts.luma_nr)

        if self._state.opts.chroma_nr is not None:
            metadata["tbc_export_chroma_nr"] = str(self._state.opts.chroma_nr)

        if self._state.opts.transform_threshold is not None:
            metadata["tbc_export_transform_threshold"] = str(
                self._state.opts.transform_threshold
            )

        if self._state.opts.transform_thresholds is not None:
            metadata["tbc_export_transform_thresholds"] = str(
                self._state.opts.transform_thresholds
            )

        if self._state.opts.ntsc_phase_comp is not None:
            metadata["tbc_export_ntsc_phase_compensation"] = str(
                self._state.opts.ntsc_phase_comp
            ).lower()

        return metadata

    def _get_decode_version_metadata(self) -> str | None:
        """Return a combined decode version from source metadata branch/commit."""
        decode_git_branch = self._state.tbc_json.git_branch
        decode_git_commit = self._state.tbc_json.git_commit
        if decode_git_branch and decode_git_commit:
            return f"{decode_git_branch}@{decode_git_commit}"
        return decode_git_branch or decode_git_commit

    def _get_export_chroma_decoder_metadata(self) -> str:
        """Return the chroma decoder metadata value for the current export mode."""
        match self._config.export_mode:
            case ExportMode.CHROMA_MERGE:
                return (
                    f"{self._state.decoder_luma.value}"
                    f"+{self._state.decoder_chroma.value}"
                )
            case ExportMode.CHROMA_COMBINED | ExportMode.CHROMA_COMBINED_LD:
                return self._state.decoder_chroma.value
            case ExportMode.LUMA | ExportMode.LUMA_EXTRACTED:
                return self._state.decoder_luma.value
            case ExportMode.LUMA_4FSC:
                return "none"
            case _:
                return "unknown"

    def _get_output_opt(self) -> FlatList:
        """Output opts for ffmpeg."""
        output_file = (
            self._state.file_helper.output_video_file_luma
            if self._is_two_step_luma_mode()
            else self._state.file_helper.output_video_file
        )

        # only set if the user has not manually specified a container
        output_format = (
            self._get_profile().video_profile.output_format
            if not self._state.opts.profile_container
            else None
        )

        if self._state.opts.checksum:
            checksum_output = (
                f"[f={output_format}]{output_file}"
                if output_format is not None
                else output_file
            )

            return FlatList(
                (
                    "-f",
                    "tee",
                    f"[f=streamhash]{output_file}.sha256|{checksum_output}",
                )
            )

        return (
            FlatList(("-f", output_format, output_file))
            if output_format is not None
            else FlatList(output_file)
        )

    def _get_profile(self) -> Profile:
        """Return the profile in state."""
        return self._state.profile

    def _get_video_profile(self) -> ProfileVideo:
        """Return the video profile in state."""
        return self._state.profile.video_profile

    def _get_profile_video_format(self) -> str:
        """Return the video format in state."""
        video_format = self._get_profile().video_profile.video_format

        # if two step, set to gray8/16le
        if self._is_two_step_luma_mode() or self._config.export_mode in (
            ExportMode.LUMA,
            ExportMode.LUMA_4FSC,
            ExportMode.LUMA_EXTRACTED,
        ):
            depth = (
                16
                if self._state.opts.video_bitdepth is None
                else self._state.opts.video_bitdepth
            )

            if (new_format := VideoFormatType.GRAY.value.get(depth)) is not None:
                video_format = new_format

        # check bitdepth override
        if (depth := self._state.opts.video_bitdepth) is not None and (
            new_format := VideoFormatType.get_new_format(video_format, depth)
        ) is not None:
            video_format = new_format

        # if override opts, ensure format for bitdepth and set
        # does not set the luma format when in two-step mode
        if (
            (vf := self._state.opts.video_format) is not None
            and (depth := self._state.opts.video_bitdepth) is not None
            and (new_format := vf.value.get(depth)) is not None
            and not self._is_two_step_luma_mode()
        ):
            video_format = new_format

        return video_format

    def _is_two_step_luma_mode(self) -> bool:
        """Return True if this wrapper is in luma mode while two-step is enabled."""
        return self._state.opts.two_step and self._config.export_mode is ExportMode.LUMA

    def _is_two_step_merge_mode(self) -> bool:
        """Return True if this wrapper is in merge mode while two-step is enabled."""
        return (
            self._state.opts.two_step
            and self._config.export_mode is ExportMode.CHROMA_MERGE
        )

    def _get_supports_attachments(self) -> bool:
        """Return True if attachments are supported by the profile container."""
        return self._state.file_helper.output_container.lower() == "mkv"

    def _get_subtitle_format(self) -> str:
        """Return subtitle format based on the profile container."""
        return (
            "srt"
            if self._state.file_helper.output_container.lower() != "mov"
            else "mov_text"
        )

    def _get_field_order(self) -> str:
        """Return the formatted field order to feed to ``setfield``.

        ``auto`` (the default) resolves to the source-derived TFF/BFF so that
        ``bwdif`` deinterlacing uses the correct parity instead of jittering
        on BFF sources. Explicit tff/bff/prog are passed through unchanged.
        """
        field_order = self._state.opts.field_order
        if field_order is FieldOrder.AUTO:
            field_order = self._state.source_field_order
        return field_order.name.lower()

    @cached_property
    def process_name(self) -> ProcessName:  # noqa: D102
        return ProcessName.FFMPEG

    @cached_property
    def supported_pipe_types(self) -> PipeType:  # noqa: D102
        return PipeType.NULL | PipeType.OS | PipeType.NAMED

    @cached_property
    def stdin(self) -> int | None:  # noqa: D102
        # if ffmpeg has a pipe with a handle, use it
        # we usually use named pipes that do not have handles
        return next(
            (
                pipe.in_handle
                for pipe in self._config.input_pipes
                if pipe.in_handle is not None
            ),
            None,
        )

    @cached_property
    def stdout(self) -> int | None:  # noqa: D102
        # if ffmpeg has a pipe with a handle, use it
        # we usually do not have any out pipes
        return next(
            (
                pipe.in_handle
                for pipe in self._config.input_pipes
                if pipe.in_handle is not None
            ),
            None,
        )

    @cached_property
    def stderr(self) -> int | None:  # noqa: D102
        return asyncio.subprocess.PIPE

    @cached_property
    def log_output(self) -> bool:  # noqa: D102
        # we use built in FFmpeg logging via env
        return False

    @cached_property
    def log_stdout(self) -> bool:  # noqa: D102
        return False

    @cached_property
    def env(self) -> dict[str, str] | None:  # noqa: D102
        if self._state.opts.log_process_output:
            file_name = self._state.file_helper.get_log_file(
                self.process_name, TBCType.NONE
            )

            return {"FFREPORT": f"file='{file_name}'"}
        return None

    @cached_property
    def ignore_error(self) -> bool:  # noqa: D102
        return False

    @cached_property
    def stop_on_last_alive(self) -> bool:  # noqa: D102
        return False
