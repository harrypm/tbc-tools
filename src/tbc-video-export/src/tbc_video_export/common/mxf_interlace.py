"""In-place MXF interlace metadata tidy-up.

FFmpeg's MXF muxer (7.1) mis-signals interlaced exports in the picture
descriptor even with ``setfield`` and ``-flags +ilme+ildct`` set:

* ProRes and JPEG 2000 essence is always described as a full progressive
  frame (FrameLayout 0, full-frame heights), so MXF readers/NLEs treat the
  interlaced picture as progressive.
* Codecs it does describe as interlaced (FFV1, ...) get a FieldDominance
  derived from FFmpeg's internal "TB" label (top coded first, bottom
  *displayed* first), i.e. BFF for top-field-first video.

The exporter knows the real field order, so this rewrites the CDCI/RGBA
picture descriptors to what FFmpeg writes for correctly-flagged interlaced
essence (as for D10/MPEG-2): FrameLayout 1 (separate fields), per-field
stored/sampled/display heights, the SMPTE video line map, and FieldDominance
(1 = field 1/top first, 2 = bottom first).

All edits stay inside header metadata. Fixed-size values are patched in
place; a missing FieldDominance tag (5 bytes) is inserted by shifting the
bytes up to the next KLV Fill item in the same header metadata and shrinking
that fill, so partition byte counts, essence offsets and index tables are
untouched (sets reference each other by UUID, not offset).
"""

from __future__ import annotations

import struct
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from pathlib import Path

# Header metadata local-set keys: 06.0e.2b.34.02.53.01.01.0d.01.01.01.01.01.xx.00
_LOCAL_SET_PREFIX = bytes.fromhex("060e2b34025301010d0101010101")
# CDCI (0x28) and RGBA (0x29) picture essence descriptors
_PICTURE_DESCRIPTOR_IDS = (0x28, 0x29)
_FILL_KEY = bytes.fromhex("060e2b34010101020301021001000000")

_TAG_STORED_HEIGHT = 0x3202
_TAG_SAMPLED_HEIGHT = 0x3204
_TAG_DISPLAY_HEIGHT = 0x3208
_TAG_FRAME_LAYOUT = 0x320C
_TAG_VIDEO_LINE_MAP = 0x320D
_TAG_FIELD_DOMINANCE = 0x3212

_FRAME_LAYOUT_FULL_FRAME = 0
_FRAME_LAYOUT_SEPARATE_FIELDS = 1



def _read_ber(data: bytes | bytearray, pos: int) -> tuple[int, int, int]:
    """Return (length, value offset, length-field size) of a BER length."""
    first = data[pos]
    if first < 0x80:
        return first, pos + 1, 1
    size = first & 0x7F
    return int.from_bytes(data[pos + 1 : pos + 1 + size], "big"), pos + 1 + size, 1 + size


def _write_ber(data: bytearray, pos: int, field_size: int, length: int) -> None:
    """Rewrite a BER length in place, keeping its encoded width."""
    if field_size == 1:
        if length >= 0x80:
            raise ValueError("short-form BER length overflow")
        data[pos] = length
    else:
        data[pos + 1 : pos + field_size] = length.to_bytes(field_size - 1, "big")


def _local_tags(data: bytes | bytearray, start: int, end: int) -> dict[int, tuple[int, int]]:
    """Map local tag -> (value offset, value length) inside a local set."""
    tags: dict[int, tuple[int, int]] = {}
    pos = start
    while pos + 4 <= end:
        tag, length = struct.unpack(">HH", data[pos : pos + 4])
        tags[tag] = (pos + 4, length)
        pos += 4 + length
    return tags


def fix_mxf_interlace(path: Path, top_field_first: bool, line_map: tuple[int, int]) -> list[str]:
    """Rewrite the MXF picture descriptors of `path` as interlaced.

    `line_map` is the (field 1, field 2) first broadcast line of the stored
    frame. It is written where FFmpeg wrote a full-frame map or none (0/0;
    FFmpeg only knows a few standard heights): MXF readers derive the field
    order from FieldDominance and a non-zero line map together. A map FFmpeg
    computed for interlaced essence (e.g. D10's 7/320) is kept.

    Returns a description of each change made (empty when the file already
    signalled the requested field order).
    """
    data = bytearray(path.read_bytes())
    changes: list[str] = []

    pos = 0
    while pos + 17 <= len(data):
        key = data[pos : pos + 16]
        length, value_pos, ber_size = _read_ber(data, pos + 16)
        if not (key[:14] == _LOCAL_SET_PREFIX and key[14] in _PICTURE_DESCRIPTOR_IDS):
            pos = value_pos + length
            continue

        tags = _local_tags(data, value_pos, value_pos + length)
        layout_at = tags.get(_TAG_FRAME_LAYOUT)
        if layout_at is None:
            pos = value_pos + length
            continue

        converted = data[layout_at[0]] == _FRAME_LAYOUT_FULL_FRAME
        if converted:
            # Full progressive frame -> separate fields with per-field heights.
            data[layout_at[0]] = _FRAME_LAYOUT_SEPARATE_FIELDS
            for tag in (_TAG_STORED_HEIGHT, _TAG_SAMPLED_HEIGHT, _TAG_DISPLAY_HEIGHT):
                if (at := tags.get(tag)) is not None and at[1] == 4:
                    height = int.from_bytes(data[at[0] : at[0] + 4], "big")
                    data[at[0] : at[0] + 4] = ((height + 1) // 2).to_bytes(4, "big")
            changes.append("frame layout full frame -> separate fields (per-field heights)")
        final_map = tuple(line_map)
        if (at := tags.get(_TAG_VIDEO_LINE_MAP)) is not None and at[1] >= 16:
            count, _size = struct.unpack(">II", data[at[0] : at[0] + 8])
            if count == 2:
                current = struct.unpack(">ii", data[at[0] + 8 : at[0] + 16])
                if (converted or current == (0, 0)) and current != final_map:
                    data[at[0] + 8 : at[0] + 16] = struct.pack(">ii", *final_map)
                    changes.append(f"video line map {list(current)} -> {list(final_map)}")
                else:
                    final_map = current

        # FieldDominance is relative to storage order: 1 = the field stored
        # first is displayed first. Readers (FFmpeg mxfdec, MediaInfo) take
        # the stored-first field from the line map parity (odd sum -> top
        # field stored first), so derive it the same way.
        top_stored_first = (final_map[0] + final_map[1]) % 2 == 1
        dominance = 1 if top_stored_first == top_field_first else 2

        dominance_at = tags.get(_TAG_FIELD_DOMINANCE)
        if dominance_at is not None:
            if data[dominance_at[0]] != dominance:
                changes.append(f"field dominance {data[dominance_at[0]]} -> {dominance}")
                data[dominance_at[0]] = dominance
        else:
            # Insert the tag at the end of the set; absorb the 5 bytes in the
            # next KLV Fill so nothing outside header metadata moves.
            insert = struct.pack(">HHB", _TAG_FIELD_DOMINANCE, 1, dominance)
            set_end = value_pos + length
            # The fill must belong to this header metadata: only local sets
            # may sit between it and the descriptor (never a partition pack
            # or essence, which must not move).
            fill_pos = set_end
            while data[fill_pos : fill_pos + 16] != _FILL_KEY:
                if data[fill_pos + 4 : fill_pos + 6] != b"\x02\x53" or fill_pos + 17 > len(data):
                    raise ValueError("no KLV fill in the header metadata to absorb the edit")
                skip, skip_value_pos, _ = _read_ber(data, fill_pos + 16)
                fill_pos = skip_value_pos + skip
            fill_length, fill_value_pos, fill_ber_size = _read_ber(data, fill_pos + 16)
            if fill_length < len(insert):
                raise ValueError("KLV fill too small to absorb the edit")
            fill_end = fill_value_pos + fill_length
            between = bytes(data[set_end:fill_pos])
            _write_ber(data, pos + 16, ber_size, length + len(insert))
            _write_ber(data, fill_pos + 16, fill_ber_size, fill_length - len(insert))
            new_fill_header = bytes(data[fill_pos:fill_value_pos])
            region = insert + between + new_fill_header + bytes(fill_length - len(insert))
            data[set_end:fill_end] = region
            length += len(insert)
            changes.append(f"field dominance added ({dominance})")

        pos = value_pos + length

    if changes:
        path.write_bytes(data)
    return changes
