from __future__ import annotations

from tbc_video_export.common.field_order import (
    compute_is_tff,
    compute_top_pad_lines,
)


class TestFieldOrder:
    """Tests for the field-order helpers (replicate outputwriter.cpp)."""

    def test_no_padding_when_not_trimming(self) -> None:  # noqa: D102
        assert compute_top_pad_lines(44, 620, 8, trim_to_active=False) == 0
        # full-frame / 4fsc paths never pad, so field order follows first line parity
        assert compute_is_tff(44, 0) is True
        assert compute_is_tff(45, 0) is False

    def test_no_padding_when_padding_le_one(self) -> None:  # noqa: D102
        # padding factor of 1 means no divisibility constraint -> no pad lines
        assert compute_top_pad_lines(122, 448, 1, trim_to_active=True) == 0

    def test_pal_default_is_tff(self) -> None:  # noqa: D102
        # PAL default: first=44, last=620, pad=8 -> activeHeight=576, 576%8==0 -> top=0
        top = compute_top_pad_lines(44, 620, 8, trim_to_active=True)
        assert top == 0
        assert compute_is_tff(44, top) is True

    def test_ntsc_default_is_tff(self) -> None:  # noqa: D102
        # NTSC default: first=40, last=525, pad=8 -> top=2
        top = compute_top_pad_lines(40, 525, 8, trim_to_active=True)
        assert top == 2
        assert compute_is_tff(40, top) is True

    def test_ntsc_vbi_is_bff(self) -> None:  # noqa: D102
        # NTSC VBI preset: first=17, last=525, pad=2 -> activeHeight=508
        # 508 % 2 == 0 -> top=0
        top = compute_top_pad_lines(17, 525, 2, trim_to_active=True)
        assert top == 0
        # 17 % 2 == 1, 0 % 2 == 0 -> BFF
        assert compute_is_tff(17, top) is False

    def test_pal_full_vertical_is_bff(self) -> None:  # noqa: D102
        # PAL full_vertical: first=2, last=620, pad=8 -> top=3
        top = compute_top_pad_lines(2, 620, 8, trim_to_active=True)
        assert top == 3
        # 2 % 2 == 0, 3 % 2 == 1 -> BFF
        assert compute_is_tff(2, top) is False

    def test_odd_first_line_with_even_top_pad_is_bff(self) -> None:  # noqa: D102
        # Custom active lines: first=45, last=621, pad=8 -> activeHeight=576, top=0
        top = compute_top_pad_lines(45, 621, 8, trim_to_active=True)
        assert top == 0
        assert compute_is_tff(45, top) is False
