from __future__ import annotations

import asyncio
from contextlib import suppress
from functools import cached_property
from pathlib import Path
from tempfile import gettempdir
from typing import TYPE_CHECKING
from uuid import uuid4

from tbc_video_export.common.enums import PipeType, ProcessName, TBCType
from tbc_video_export.common.utils import FlatList
from tbc_video_export.process.wrapper.wrapper import Wrapper

if TYPE_CHECKING:
    from tbc_video_export.process.wrapper.pipe import Pipe
    from tbc_video_export.process.wrapper.wrapper_config import WrapperConfig
    from tbc_video_export.program_state import ProgramState


class WrapperLDDropoutCorrect(Wrapper):
    """Wrapper for tbc-dropout-correct."""

    def __init__(self, state: ProgramState, config: WrapperConfig[None, Pipe]) -> None:
        self._config = config
        self._output_metadata_file = self._create_output_metadata_file()
        super().__init__(state, config)

    def post_fn(self) -> None:  # noqa: D102
        with suppress(FileNotFoundError, PermissionError):
            Path.unlink(self._output_metadata_file)

    @property
    def command(self) -> FlatList:  # noqa: D102
        return FlatList(
            (
                self.binary,
                self._get_thread_opts(),
                self._get_range_opts(),
                self._state.file_helper.tbcs[self._config.tbc_type],
                None if self.dropout_interfield_correction else "-i",
                "--overcorrect" if self.overcorrect else None,
                "--input-json",
                self._state.file_helper.tbc_json.file_name,
                "--output-json",
                self._output_metadata_file,
                self._config.output_pipes.out_path,
            )
        )

    def _create_output_metadata_file(self) -> Path:
        return Path(gettempdir()).joinpath(
            f"tbc-video-export-{self._config.tbc_type}-{uuid4().hex}.dropout.db"
        )

    def _get_range_opts(self) -> FlatList | None:
        """Only correct the frames a ranged export uses."""
        if (frame_range := self._state.dropout_correct_range) is None:
            return None
        start, length = frame_range
        opts = FlatList()
        if start > 1:
            opts.append(("-s", start))
        if length is not None:
            opts.append(("-l", length))
        return opts

    def _get_thread_opts(self) -> FlatList | None:
        thread_count = self._state.opts.threads

        if (t := self._state.opts.dropout_correct_threads) is not None:
            thread_count = t

        if thread_count != 0:
            return FlatList(("-t", thread_count))

        return None

    @cached_property
    def overcorrect(self) -> bool:
        """Heavy dropout correction: as requested, else on for tape sources."""
        if self._state.opts.overcorrect is not None:
            return self._state.opts.overcorrect
        return self._state.file_helper.tbc_json.is_tape_source

    @cached_property
    def process_name(self) -> ProcessName:  # noqa: D102
        return ProcessName.LD_DROPOUT_CORRECT

    @cached_property
    def supported_pipe_types(self) -> PipeType:  # noqa: D102
        return PipeType.NULL | PipeType.OS

    @cached_property
    def stdin(self) -> int | None:  # noqa: D102
        return None

    @cached_property
    def stdout(self) -> int | None:  # noqa: D102
        return self._config.output_pipes.out_handle

    @cached_property
    def stderr(self) -> int | None:  # noqa: D102
        return asyncio.subprocess.PIPE

    @cached_property
    def log_output(self) -> bool:  # noqa: D102
        return True

    @cached_property
    def log_stdout(self) -> bool:  # noqa: D102
        return False

    @cached_property
    def env(self) -> dict[str, str] | None:  # noqa: D102
        return None
    
    @cached_property
    def dropout_interfield_correction(self) -> bool:  # noqa: D102
        # when `dropout_interfield_correction` set to NONE, disable, else
        #   when tbc_type is LUMA or CHROMA and `dropout_interfield_correction`` matches, enable
        #   when tbc_type is COMBINED or `dropout_interfield_correction` is COMBINED, enable
        return self._state.opts.dropout_interfield_correction != TBCType.NONE and (
            self._state.opts.dropout_interfield_correction == self.tbc_type
            or self._state.opts.dropout_interfield_correction == TBCType.COMBINED
            or self.tbc_type == TBCType.COMBINED
        )

    @cached_property
    def ignore_error(self) -> bool:  # noqa: D102
        # tbc-dropout-correct does not support -l or -s flags and will
        # be killed when other procs are finished.
        # This is unfortunate as we would have to use more
        # complicated logic to determine a real crash from success.
        # Currently we do not check any errors from tbc-dropout-correct
        # and assume all returncodes are success.
        return True

    @cached_property
    def stop_on_last_alive(self) -> bool:  # noqa: D102
        # On NT systems closing an os.pipe() does not kill the procs using
        # the pipe. The result of this is tbc-dropout-correct continuing
        # to run after other procs have finished when using -l or -s.
        # This will signal to the proc killer to kill the process if it
        # is the only remaining proc type running.
        # This is essentially a workaround until tbc-dropout-correct
        # supports -s/-l or properly supports named pipes.
        return True
