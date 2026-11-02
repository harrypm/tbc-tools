from __future__ import annotations

import atexit
import logging
import os
import shutil
import subprocess
import tempfile
from functools import cached_property
from pathlib import Path
from typing import TYPE_CHECKING

from tbc_video_export.common import consts, exceptions
from tbc_video_export.common.enums import FlagHelper, ProcessName, TBCType
from tbc_video_export.common.tbc_json_helper import TBCJsonHelper
from tbc_video_export.common.utils import files
from tbc_video_export.config.config import GetProfileFilter

if TYPE_CHECKING:
    from tbc_video_export.config.config import Config
    from tbc_video_export.opts import Opts


class FileHelper:
    """Helper for files.

    Handles reading the TBC json, checking if files exist and generating paths.
    """

    def __init__(self, opts: Opts, config: Config) -> None:
        self._opts = opts
        self._config = config

        self._profile = self._config.get_profile(GetProfileFilter(self._opts.profile))

        # overwrite audio profile if opt set
        if self._opts.audio_profile is not None:
            self._profile.audio_profile = config.get_audio_profile(
                self._opts.audio_profile
            )
        # initially set both input and output files to the input file
        # file without file extension
        self._input_path = self._output_path = Path(self._opts.input_file).parent

        # path to file
        self._input_file_name = self._output_file_name = Path(
            self._opts.input_file
        ).stem

        # set output file path/name if set to new path
        if self._opts.output_file is not None:
            self._output_path = Path(self._opts.output_file).parent
            self._output_file_name = Path(self._opts.output_file).stem

        self.tools = self._get_tool_paths()
        self.tbcs = self._set_tbc_files()

    @cached_property
    def input_name(self) -> Path:
        """Return absolute path to input file without file extension."""
        return Path(self._input_path).joinpath(self._input_file_name)

    @cached_property
    def output_name(self) -> Path:
        """Return absolute path to output without file extension."""
        return Path(self._output_path).joinpath(self._output_file_name)

    @cached_property
    def is_combined_ld(self) -> bool:
        """Returns True if the TBC is for LaserDisc.

        This only checks if the TBC type is combined and an EFM file is
        located. There may be a more reliable way of doing this.
        """
        return TBCType.COMBINED in self.tbcs and self.efm_file is not None

    @property
    def efm_file(self) -> Path | None:
        """Returns absolute path to EFM file if it exists."""
        if (file := self.get_input_file_from_ext("efm")).is_file():
            return file
        return None

    @property
    def ffmetadata_file(self) -> Path:
        """Returns absolute path fo metadata file."""
        return self.get_output_file_from_ext("ffmetadata")

    @property
    def cc_file(self) -> Path:
        """Returns absolute path to subtitle file ."""
        return self.get_output_file_from_ext("scc")

    @property
    def tbc_types(self) -> TBCType:
        """Returns all TBC types found."""
        types = TBCType.NONE

        for t in self.tbcs:
            types |= t

        # remove none if others set
        if types is not TBCType.NONE:
            types &= ~TBCType.NONE

        return types

    @property
    def tbc_json(self) -> TBCJsonHelper:
        """Returns TBCJson helper.

        This will create the helper if it does not exist.
        """
        if not getattr(
            self,
            "_tbc_json",
            False,
        ):
            self._tbc_json = TBCJsonHelper(self._resolve_tbc_json_path())

        return self._tbc_json

    @tbc_json.setter
    def tbc_json(self, file_name: Path) -> None:
        """Set the TBCJson Helper.

        This can be used when tbc-process-vbi generates a new JSON file.
        """
        self._tbc_json = TBCJsonHelper(file_name)

    def _resolve_tbc_json_path(self) -> Path:
        """Resolve the TBC json path, converting SQLite metadata if needed.

        When the json does not exist but a SQLite .db does (the same path with
        a .db suffix, or --input-tbc-json pointing directly at a .db), the db
        is auto-converted to JSON via tbc-metadata-converter so the
        JSON-based toolchain can consume it (decode-orc captures ship SQLite
        metadata at the base .tbc name, e.g. foo.tbc.db next to foo.tbcy).
        """
        json_path = (
            Path(self._opts.input_tbc_json)
            if self._opts.input_tbc_json is not None
            else Path(f"{self.input_name}.tbc.json")
        )

        # --input-tbc-json pointing directly at a SQLite .db converts it (the
        # suffix check must come first: a .db path is_file() too).
        if json_path.suffix == ".db":
            if json_path.is_file():
                return self._convert_sqlite_to_json(json_path, json_path)
            return json_path

        if json_path.is_file():
            return json_path

        db_path = json_path.with_suffix(".db")
        if db_path.is_file():
            return self._convert_sqlite_to_json(db_path, json_path)

        # No json and no db: return the original path so TBCJsonHelper raises
        # its usual 'TBC json not found' error.
        return json_path

    def _convert_sqlite_to_json(self, db_path: Path, preferred_json_path: Path) -> Path:
        """Convert a SQLite metadata file to JSON using tbc-metadata-converter.

        The conversion writes to a temporary file first, then moves the result
        next to the source (preferred_json_path) when that location is
        writable, so subsequent runs reuse it and other JSON-only tools can
        consume it too. A read-only source directory (or a --input-tbc-json
        that points at the .db itself) keeps the temporary file, which is
        removed when the program exits.
        """
        temp_file = tempfile.NamedTemporaryFile(
            prefix="tbc-video-export-converted-", suffix=".tbc.json", delete=False
        )
        temp_file.close()
        temp_path = Path(temp_file.name)

        converter = files.find_binary(
            "tbc-metadata-converter.exe" if os.name == "nt" else "tbc-metadata-converter"
        )

        logging.getLogger("console").info(
            "Only SQLite metadata found (%s); auto-converting to JSON", db_path
        )

        result = subprocess.run(
            [
                str(converter),
                "--direction",
                "sqlite-to-json",
                "--input-sqlite",
                str(db_path),
                "--output-json",
                str(temp_path),
            ],
            capture_output=True,
            text=True,
            check=False,
        )

        if result.returncode != 0 or not temp_path.is_file():
            detail = (result.stderr or result.stdout or "").strip()
            temp_path.unlink(missing_ok=True)
            raise exceptions.TBCError(
                f"Failed to auto-convert SQLite metadata {db_path} to JSON using "
                f"{converter}."
                + (f" ({detail})" if detail else "")
            )

        atexit.register(lambda p=temp_path: p.unlink(missing_ok=True))

        if preferred_json_path != db_path:
            try:
                shutil.move(str(temp_path), str(preferred_json_path))
                logging.getLogger("console").info(
                    "Converted metadata saved to %s", preferred_json_path
                )
                return preferred_json_path
            except OSError:
                # Read-only source directory: keep using the temporary file.
                pass

        return temp_path

    @cached_property
    def tbc_luma(self) -> Path:
        """Return the absolute path to the luma (or combined) TBC."""
        if TBCType.LUMA in self.tbcs:
            return self.tbcs[TBCType.LUMA]

        if TBCType.COMBINED in self.tbcs:
            return self.tbcs[TBCType.COMBINED]

        raise exceptions.TBCError("Unable to find luma TBC.")

    @cached_property
    def output_container(self) -> str:
        """Return container used for output video file."""
        return (
            self._opts.profile_container
            if self._opts.profile_container is not None
            else self._profile.video_profile.container
        )

    @cached_property
    def output_video_file(self) -> Path:
        """Return absolute path to the output video file."""
        return self.get_output_file_from_ext(self.output_container)

    @cached_property
    def output_video_file_luma(self) -> Path:
        """Return absolute path to the output video file for luma.

        This is used when two-step is enabled when merging.
        """
        return self.get_output_file_from_ext(
            f"{consts.TWO_STEP_OUT_FILE_LUMA_SUFFIX}.{self.output_container}"
        )

    def get_log_file(
        self,
        process_name: ProcessName,
        tbc_type: TBCType,
        timestamp: str = consts.CURRENT_TIMESTAMP,
    ):
        """Return absolute path to log file for process/tbc type."""
        return Path(self._output_path).joinpath(
            f"{timestamp}_{self._input_file_name}_{process_name}"
            f"_{FlagHelper.get_flags_str(tbc_type, '_')}.log"
        )

    def get_output_file_from_ext(self, extension: Path | str) -> Path:
        """Return absolute path to output file with extension."""
        return Path(f"{self.output_name}.{extension}")

    def get_input_file_from_ext(self, extension: Path | str) -> Path:
        """Return absolute path to input file with extension."""
        return Path(f"{self.input_name}.{extension}")

    def check_output_dir(self) -> None:
        """Check if output directory exists.

        This throws an exception if output directory does not exist.
        """
        if not Path(self._output_path).is_dir():
            raise exceptions.FileIOError(
                f"Output directory does not exist ({self._output_path})."
            )

    def check_output_file(self) -> None:
        """Check if output file exists.

        This throws an exception if output file exists.
        """
        if not self._opts.overwrite:
            files = [self.output_video_file]

            if self._opts.two_step:
                files.append(self.output_video_file_luma)

            for file in files:
                if file.is_file():
                    raise exceptions.FileIOError(
                        f"{file} exists, use --overwrite or move the file."
                    )

    def _set_tbc_files(self) -> dict[TBCType, Path]:
        """Create a dict containing the absolute path to the TBC files based on type."""
        tbcs: dict[TBCType, Path] = {}

        # Luma/combined candidates, most conventional first. Besides the
        # standard .tbc, recognise the Y/C (S-Video) luma extensions written
        # by decode-orc and accepted by tbc-analyse: .tbcy and .ytbc. A Y/C
        # luma file can never be 'combined' - its chroma always lives in a
        # separate file - so only a plain .tbc without a separate chroma
        # stream is treated as COMBINED (issue #30).
        luma_candidates: list[tuple[str, bool]] = [
            (f"{self.input_name}.tbc", True),
            (f"{self.input_name}.tbcy", False),
            (f"{self.input_name}.ytbc", False),
        ]

        # Chroma candidates: the standard _chroma.tbc suffix first, then the
        # Y/C chroma extensions (.tbcc pairs with .tbcy, .ctbc pairs with
        # .ytbc - the same pairings tbc-analyse accepts).
        chroma_candidates: list[str] = [
            f"{self.input_name}_chroma.tbc",
            f"{self.input_name}.tbcc",
            f"{self.input_name}.ctbc",
        ]

        luma_file = next(
            (Path(candidate) for candidate, _ in luma_candidates if Path(candidate).is_file()),
            None,
        )
        chroma_file = next(
            (Path(candidate) for candidate in chroma_candidates if Path(candidate).is_file()),
            None,
        )

        if chroma_file is not None:
            tbcs[TBCType.CHROMA] = chroma_file

        if luma_file is not None:
            # Only a plain .tbc (candidate marked may_be_combined) without a
            # separate chroma file is a combined (CVBS) capture.
            may_be_combined = next(
                combined for candidate, combined in luma_candidates if candidate == str(luma_file)
            )
            if TBCType.CHROMA in tbcs or not may_be_combined:
                tbcs[TBCType.LUMA] = luma_file
            else:
                tbcs[TBCType.COMBINED] = luma_file

        # ensure tbcs exist
        if len(tbcs) == 0:
            raise exceptions.TBCError("TBC not found at location.")

        # if for some reason we found a chroma TBC but no luma TBC
        if TBCType.CHROMA in tbcs and TBCType.LUMA not in tbcs:
            raise exceptions.TBCError("Location contains chroma TBC but no luma TBC.")

        return tbcs

    def _get_tool_paths(self) -> dict[ProcessName, Path | list[Path | str]]:
        """Get required tool paths from PATH or script path."""
        tools: dict[ProcessName, Path | list[Path | str]] = {}
        tools[ProcessName.FFMPEG] = self._get_tool_path(ProcessName.FFMPEG)

        if not self._opts.no_dropout_correct and not self._opts.luma_4fsc:
            tools[ProcessName.LD_DROPOUT_CORRECT] = self._get_tbc_tool_path(
                ProcessName.LD_DROPOUT_CORRECT
            )

        if not self._opts.luma_4fsc:
            tools[ProcessName.LD_CHROMA_DECODER] = self._get_tbc_tool_path(
                ProcessName.LD_CHROMA_DECODER
            )

        if self._opts.process_vbi:
            tools[ProcessName.LD_PROCESS_VBI] = self._get_tbc_tool_path(
                ProcessName.LD_PROCESS_VBI
            )

        if self._opts.process_efm:
            tools[ProcessName.LD_PROCESS_EFM] = self._get_tbc_tool_path(
                ProcessName.LD_PROCESS_EFM
            )

        if self._opts.export_metadata:
            tools[ProcessName.LD_EXPORT_METADATA] = self._get_tbc_tool_path(
                ProcessName.LD_EXPORT_METADATA
            )

        return tools

    def _get_tool_path(self, tool_name: ProcessName) -> Path:
        if os.name == "nt":
            # append .exe on NT
            return files.find_binary(f"{tool_name}.exe")

        return files.find_binary(str(tool_name))

    def _get_tbc_tool_path(self, tool_name: ProcessName) -> Path | list[Path | str]:
        if self._opts.tbc_tools_appimage:
            # return list with appimage file and tool name
            return [files.find_binary(self._opts.tbc_tools_appimage), str(tool_name)]

        return self._get_tool_path(tool_name)
