from __future__ import annotations

from typing import TYPE_CHECKING

from tbc_video_export.common.enums import ProcessName
from tbc_video_export.process.parser.parser_ffmpeg import ParserFFmpeg
from tbc_video_export.process.parser.parser_tbc_chroma_decoder import (
    ParserTBCChromaDecoder,
)
from tbc_video_export.process.parser.parser_tbc_dropout_correct import (
    ParserTBCDropoutCorrect,
)
from tbc_video_export.process.parser.parser_tbc_export_metadata import (
    ParserTBCExportMetadata,
)
from tbc_video_export.process.parser.parser_tbc_process_vbi import ParserTBCProcessVBI

if TYPE_CHECKING:
    from tbc_video_export.process.parser.parser import Parser


class ParserFactory:
    """Factory class for process parsing."""

    @classmethod
    def create(cls, process_name: ProcessName) -> Parser:
        """Create an output parser based on process name."""
        match process_name:
            case ProcessName.FFMPEG:
                return ParserFFmpeg(process_name)

            case ProcessName.TBC_CHROMA_DECODER:
                return ParserTBCChromaDecoder(process_name)

            case ProcessName.TBC_DROPOUT_CORRECT:
                return ParserTBCDropoutCorrect(process_name)

            case ProcessName.TBC_PROCESS_VBI:
                return ParserTBCProcessVBI(process_name)

            case ProcessName.TBC_EXPORT_METADATA:
                return ParserTBCExportMetadata(process_name)

            case _:
                raise NotImplementedError(
                    f"Parser for process {process_name} not implemented."
                )
