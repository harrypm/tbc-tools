from __future__ import annotations

import json
from functools import cached_property
from pathlib import Path
from typing import Any

from tbc_video_export.common import exceptions
from tbc_video_export.common.enums import VideoSystem


class TBCJsonHelper:
    """Handles parsing the TBC json."""

    def __init__(self, file_name: Path | None, json_data: Any = None) -> None:
        if file_name is not None:
            self.file_name = file_name

            try:
                with Path.open(file_name, mode="r", encoding="utf-8") as json_file:
                    self._json_data = json.load(json_file)
            except FileNotFoundError as e:
                raise exceptions.TBCError(f"TBC json not found ({file_name}).") from e
            except PermissionError as e:
                raise exceptions.TBCError(
                    f"Permission denied opening TBC json ({file_name})."
                ) from e
            except json.JSONDecodeError as e:
                raise exceptions.TBCError(
                    f"Unable to parse TBC json ({file_name})."
                ) from e
        else:
            self._json_data = json.loads(json_data)

    @property
    def file_name(self) -> Path:
        """Return tbc json file name."""
        return self._file_name

    @file_name.setter
    def file_name(self, file_name: str | Path) -> None:
        self._file_name = Path(file_name)

    @cached_property
    def is_widescreen(self) -> bool:
        """Returns whether the json TBC flags widescreen."""
        return (
            "isWidescreen" in self._json_data["videoParameters"]
            and self._json_data["videoParameters"]["isWidescreen"]
        )

    def _get_video_parameter(self, key: str) -> str | None:
        """Return a string value from videoParameters when available."""
        video_parameters = self._json_data.get("videoParameters", {})
        if not isinstance(video_parameters, dict):
            return None
        value = video_parameters.get(key)
        if value is None:
            return None
        value_str = str(value).strip()
        return value_str if value_str else None

    def _get_video_parameter_int(self, key: str) -> int | None:
        """Return an int value from videoParameters when available."""
        value_str = self._get_video_parameter(key)
        if value_str is None:
            return None
        try:
            return int(value_str)
        except (TypeError, ValueError):
            return None

    @cached_property
    def git_branch(self) -> str | None:
        """Return decode git branch from videoParameters when available."""
        return self._get_video_parameter("gitBranch")

    @cached_property
    def git_commit(self) -> str | None:
        """Return decode git commit from videoParameters when available."""
        return self._get_video_parameter("gitCommit")

    @cached_property
    def chroma_decoder(self) -> str | None:
        """Return source chroma decoder from videoParameters when available."""
        return self._get_video_parameter("chromaDecoder")

    @cached_property
    def black_16b_ire(self) -> str | None:
        """Return source black level from videoParameters when available."""
        return self._get_video_parameter("black16bIre")

    @cached_property
    def white_16b_ire(self) -> str | None:
        """Return source white level from videoParameters when available."""
        return self._get_video_parameter("white16bIre")

    @cached_property
    def blanking_16b_ire(self) -> str | None:
        """Return source blanking level from videoParameters when available."""
        return self._get_video_parameter("blanking16bIre")

    @cached_property
    def chroma_gain(self) -> str | None:
        """Return source chroma gain from videoParameters when available."""
        return self._get_video_parameter("chromaGain")

    @cached_property
    def chroma_phase(self) -> str | None:
        """Return source chroma phase from videoParameters when available."""
        return self._get_video_parameter("chromaPhase")

    @cached_property
    def luma_nr(self) -> str | None:
        """Return source luma NR from videoParameters when available."""
        return self._get_video_parameter("lumaNR")

    @cached_property
    def source_video_system(self) -> str | None:
        """Return source video-system string from videoParameters when available."""
        return self._get_video_parameter("system")

    @cached_property
    def first_active_frame_line(self) -> int | None:
        """Return firstActiveFrameLine from videoParameters when available.

        Used to derive the correct output field order (TFF/BFF), matching the
        logic in ld-chroma-decoder's OutputWriter (outputwriter.cpp:174).
        """
        return self._get_video_parameter_int("firstActiveFrameLine")

    @cached_property
    def last_active_frame_line(self) -> int | None:
        """Return lastActiveFrameLine from videoParameters when available."""
        return self._get_video_parameter_int("lastActiveFrameLine")

    @cached_property
    def source_video_system_normalized(self) -> str | None:
        """Return normalized source video-system string from metadata."""
        if (system := self.source_video_system) is None:
            return None
        return system.replace("-", "_").lower()

    @cached_property
    def is_secam_system(self) -> bool:
        """Return true when metadata declares SECAM/MESECAM."""
        return self.source_video_system_normalized in {"secam", "mesecam"}

    @cached_property
    def video_system(self) -> VideoSystem:
        """Return VideoSystem from TBC json."""
        if (system := self.source_video_system) is not None:
            normalized_system = self.source_video_system_normalized

            # SECAM/MESECAM are first-class VideoSystem members (not mapped to
            # PAL), so the wrapper can validate SECAM-only decoders against them.
            match normalized_system:
                case VideoSystem.PAL.value:
                    return VideoSystem.PAL

                case VideoSystem.PAL_M.value:
                    return VideoSystem.PAL_M

                case VideoSystem.NTSC.value:
                    return VideoSystem.NTSC

                case VideoSystem.SECAM.value | "mesecam":
                    return (
                        VideoSystem.SECAM
                        if normalized_system == VideoSystem.SECAM.value
                        else VideoSystem.MESECAM
                    )

                case _:
                    raise exceptions.TBCError(f"System unsupported ({system}).")

        raise exceptions.TBCError("Unable to read video system from TBC json.")

    @cached_property
    def field_count(self) -> int:
        """Get total # of fields in TBC."""
        return len(self._json_data["fields"])
    @cached_property
    def pcm_audio_sample_rate(self) -> float | None:
        """Return PCM audio sample rate from metadata when available."""
        pcm_audio_parameters = self._json_data.get("pcmAudioParameters")
        if not isinstance(pcm_audio_parameters, dict):
            return None

        sample_rate = pcm_audio_parameters.get("sampleRate")
        if sample_rate is None:
            return None

        try:
            parsed_sample_rate = float(sample_rate)
        except (TypeError, ValueError):
            return None

        return parsed_sample_rate if parsed_sample_rate > 0 else None

    @cached_property
    def audio_samples_per_field(self) -> list[int] | None:
        """Return per-field PCM audio sample counts when available."""
        fields = self._json_data.get("fields")
        if not isinstance(fields, list) or not fields:
            return None

        samples: list[int] = []
        has_non_zero_value = False
        for field in fields:
            if not isinstance(field, dict):
                return None
            audio_samples = field.get("audioSamples")
            if audio_samples is None:
                return None
            if not isinstance(audio_samples, int):
                return None
            if audio_samples < 0:
                return None
            if audio_samples > 0:
                has_non_zero_value = True
            samples.append(audio_samples)

        return samples if has_non_zero_value else None

    @cached_property
    def audio_samples_prefix_sums(self) -> list[int] | None:
        """Return prefix sums for per-field audio samples when available."""
        if (samples := self.audio_samples_per_field) is None:
            return None

        prefix_sums = [0]
        running_total = 0
        for value in samples:
            running_total += value
            prefix_sums.append(running_total)

        return prefix_sums

    @cached_property
    def frame_count(self) -> int:
        """Get total # of frames in TBC."""
        return int(self.field_count / 2)

    def get_audio_trim_seconds(
        self, start_frame_one_based: int, total_frames: int
    ) -> tuple[float, float] | None:
        """Return (start_seconds, duration_seconds) from per-field audio metadata."""
        if start_frame_one_based < 1 or total_frames < 1:
            return None

        sample_rate = self.pcm_audio_sample_rate
        prefix_sums = self.audio_samples_prefix_sums
        if sample_rate is None or prefix_sums is None:
            return None

        # Fields are zero-based in this calculation; each frame has 2 fields.
        start_field_index = (start_frame_one_based - 1) * 2
        end_field_index = start_field_index + (total_frames * 2)
        available_fields = len(prefix_sums) - 1
        if start_field_index >= available_fields:
            return None

        clamped_end_field_index = min(end_field_index, available_fields)
        start_samples = prefix_sums[start_field_index]
        duration_samples = (
            prefix_sums[clamped_end_field_index] - prefix_sums[start_field_index]
        )
        if duration_samples <= 0:
            return None

        return start_samples / sample_rate, duration_samples / sample_rate

    @cached_property
    def timecode(self) -> str:
        """Attempt to read a VITC timecode for the first frame.

        Return starting timecode if no VITC data found.
        """
        if (
            not self._json_data["fields"]
            or "vitc" not in self._json_data["fields"][0]
            or "vitcData" not in self._json_data["fields"][0]["vitc"]
        ):
            return "00:00:00:00"

        is_valid = True
        is_30_frame = self.video_system is not VideoSystem.PAL
        vitc_data = self._json_data["fields"][0]["vitc"]["vitcData"]

        def decode_bcd(tens: int, units: int) -> int:
            nonlocal is_valid

            if tens > 9:
                is_valid = False
                tens = 9

            if units > 9:
                is_valid = False
                units = 9

            return (tens * 10) + units

        hour = decode_bcd(vitc_data[7] & 0x03, vitc_data[6] & 0x0F)
        minute = decode_bcd(vitc_data[5] & 0x07, vitc_data[4] & 0x0F)
        second = decode_bcd(vitc_data[3] & 0x07, vitc_data[2] & 0x0F)
        frame = decode_bcd(vitc_data[1] & 0x03, vitc_data[0] & 0x0F)

        invalid_time = hour > 23 or minute > 59 or second > 59

        if (
            invalid_time
            or (is_30_frame and frame > 29)
            or (not is_30_frame and frame > 24)
        ):
            is_valid = False

        is_drop_frame = (vitc_data[1] & 0x04) != 0 if is_30_frame else False

        if not is_valid:
            return "00:00:00:00"

        sep = ";" if is_drop_frame else ":"

        return f"{hour:02d}:{minute:02d}:{second:02d}{sep}{frame:02d}"
