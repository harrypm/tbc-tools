"""Field-order computation for tbc-video-export.

Replicates the field-order logic from ld-chroma-decoder's OutputWriter
(``outputwriter.cpp``): the weaved output frame is Top-Field-First (TFF) when
``firstActiveFrameLine % 2 == topPadLines % 2``, otherwise Bottom-Field-First
(BFF).

tbc-video-export feeds the chroma decoder the active-line range, padding, and
full-frame flag, so it can compute the same ``topPadLines`` the decoder will
use and thus the correct field order for the ``setfield`` filter (and so that
``bwdif`` deinterlacing uses the correct parity instead of jittering).

These helpers are pure functions so they can be unit-tested independently of
ProgramState.
"""
from __future__ import annotations

# Default output padding used by ld-chroma-decoder when neither the user nor
# the active-line preset overrides it (OutputWriter::Configuration::paddingAmount).
DEFAULT_PADDING = 8


def compute_top_pad_lines(
    first_active_frame_line: int,
    last_active_frame_line: int,
    padding: int,
    trim_to_active: bool,
) -> int:
    """Return the number of top padding lines the chroma decoder will add.

    Mirrors the padding loop in ``OutputWriter::updateConfiguration``
    (outputwriter.cpp:96-108). Padding is only applied when trimming to the
    active region and the padding factor is greater than 1.
    """
    if not trim_to_active or padding <= 1:
        return 0

    active_height = last_active_frame_line - first_active_frame_line
    top_pad_lines = 0
    bottom_pad_lines = 0

    while True:
        output_height = top_pad_lines + active_height + bottom_pad_lines
        if (output_height % padding) == 0:
            break
        # Add lines to the bottom and top in turn to keep the active area centred.
        if (output_height % 2) == 0:
            bottom_pad_lines += 1
        else:
            top_pad_lines += 1

    return top_pad_lines


def compute_is_tff(first_active_frame_line: int, top_pad_lines: int) -> bool:
    """Return True when the weaved output is Top-Field-First.

    Mirrors ``outputwriter.cpp:174``:
    ``firstActiveFrameLine % 2 ^ topPadLines % 2`` -> BFF when truthy.
    """
    return (first_active_frame_line % 2) == (top_pad_lines % 2)
