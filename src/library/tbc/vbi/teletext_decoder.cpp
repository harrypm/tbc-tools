/*
 * File:        teletext_decoder.cpp
 * Module:      tbc-library (shared VBI services)
 * Purpose:     WST (System B, Level 1) T42 -> TeletextPageSnapshot decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * The Level 1 spacing-attribute parsing follows ETSI EN 300 706 §12.2 Table 26
 * and matches the behaviour of the vendored vhs-teletext parser.py: the start
 * and end box codes (0/B, 0/A) only take effect on their second consecutive
 * occurrence, mosaic characters map to the Private Use Area (0xEE00 solid /
 * 0xEDE0 separated) for codes outside the 0x40-0x5F blast-through range, and
 * the set-after / set-at timing of each control is preserved. The Hamming 8/4
 * coding helpers and G0 -> Unicode mapping are reused from vbi_coding.h and
 * teletext_page.h.
 */

#include "teletext_decoder.h"

#include <algorithm>

namespace tbc::vbi {

namespace {

// Hamming 8/4 decode that mirrors vhs-teletext's hamming8_decode: a valid byte
// decodes to its 4-bit value, and an uncorrectable (double-bit) byte decodes to
// 0xF rather than failing. That matters for routing: an uncorrectable MRAG then
// resolves to a row >= 25 (control) and is dropped, instead of decoding to 0
// and being falsely treated as a row-0 header — which would flood the output
// with empty pages on a raw VBI dump whose non-teletext lines are mostly noise.
uint8_t hamming84(const uint8_t* bytes, size_t index) {
  const int value = teletext_hamming84_decode(bytes[index]);
  return value < 0 ? 0xF : static_cast<uint8_t>(value);
}

TeletextColour to_teletext_colour(int value) {
  value &= 7;
  return static_cast<TeletextColour>(value);
}

// Per-row Level 1 presentation state, mirroring vhs-teletext's parser state.
struct RowState {
  TeletextColour foreground = TeletextColour::White;
  TeletextColour background = TeletextColour::Black;
  bool double_height = false;
  bool double_width = false;
  bool mosaic = false;
  bool separated_mosaic = false;  // vhs-teletext "solid" is inverted: solid == !separated
  bool flash = false;
  bool conceal = false;
  bool boxed = false;
  bool hold_mosaic = false;
  bool escape = false;
  uint8_t held_mosaic = 0x20;
  bool held_separated = false;
  bool rendered = true;  // double-width cells render on alternate positions
};

}  // namespace

void TeletextDecoder::add_packet(const uint8_t* packet, size_t length) {
  ++stats_.packets_seen;
  if (length < kTeletextT42Bytes || packet == nullptr) {
    ++stats_.packets_short;
    return;
  }

  // vhs-teletext drops all-zero packets (is_padding = not any(array)): they are
  // not real transmissions and would otherwise Hamming-decode as magazine 8 /
  // page 00 headers, flooding the output with empty pages.
  bool all_zero = true;
  for (size_t i = 0; i < kTeletextT42Bytes; ++i) {
    if (packet[i] != 0) {
      all_zero = false;
      break;
    }
  }
  if (all_zero) {
    ++stats_.padding_packets;
    return;
  }

  // MRAG: byte 0 carries the magazine (bits 0-2) and the row LSB (bit 3);
  // byte 1 carries row bits 1-4. Magazine 0 is displayed as magazine 8.
  const uint8_t mrag_low = hamming84(packet, 0);
  const uint8_t mrag_high = hamming84(packet, 1);
  int magazine = mrag_low & 0x07;
  if (magazine == 0) {
    magazine = 8;
  }
  const int row = (mrag_low >> 3) | (mrag_high << 1);

  if (row == 0) {
    ++stats_.header_packets;
    begin_page(magazine, packet);
  } else if (row >= 1 && row <= 24) {
    ++stats_.display_packets;
    add_display_row(magazine, row, packet);
  } else {
    // Rows 25-31 carry packet 26/27/28/29/30/31 (Level 2.5+) and fastext.
    // Level 1 has no use for them; they are counted and dropped.
    ++stats_.rows_above_24;
  }
}

void TeletextDecoder::begin_page(int magazine, const uint8_t* packet) {
  // A new header closes the page that was open for this magazine.
  OpenPage* current = magazine_page(magazine);
  if (current != nullptr && current->open) {
    emit_page(magazine, true);
  }

  OpenPage* page = magazine_page(magazine);
  if (page == nullptr) {
    return;
  }

  page->open = true;
  page->snapshot = TeletextPageSnapshot{};
  page->row_bytes = {};

  TeletextPageSnapshot& snap = page->snapshot;
  snap.magazine = magazine;

  // Page number: two Hamming 8/4 bytes form the two BCD digits (§9.3.1.1).
  snap.page_number = (hamming84(packet, 2) | (hamming84(packet, 3) << 4)) & 0xFF;

  // Subcode S1-S4 (§9.3.1.2) and the control bits interleaved with them. The
  // 13-bit subcode is assembled the way the existing pipeline assembled it, so
  // debug output stays comparable; C4-C7 ride in the high bit of each subcode
  // nibble and are pulled out separately below.
  const uint8_t s1c4 = hamming84(packet, 4);
  const uint8_t s2c5 = hamming84(packet, 5);
  const uint8_t s3c6 = hamming84(packet, 6);
  const uint8_t s4c7 = hamming84(packet, 7);
  const uint16_t subLow = static_cast<uint16_t>(s1c4 | (s2c5 << 4));
  const uint16_t subHigh = static_cast<uint16_t>(s3c6 | (s4c7 << 4));
  snap.subcode = (subLow & 0x7F) | ((subHigh & 0x3F) << 8);

  // Control bits C4-C14 (§9.3.1.3 Table 2).
  snap.erase_page = (s1c4 >> 3) & 1;            // C4
  snap.newsflash = (s2c5 >> 3) & 1;             // C5
  snap.subtitle = (s3c6 >> 3) & 1;              // C6
  snap.suppress_header = (s4c7 >> 3) & 1;       // C7
  const uint8_t c8_c11 = hamming84(packet, 8);
  snap.update_indicator = c8_c11 & 1;           // C8
  snap.interrupted_sequence = (c8_c11 >> 1) & 1;  // C9
  snap.inhibit_display = (c8_c11 >> 2) & 1;     // C10
  snap.magazine_serial = (c8_c11 >> 3) & 1;     // C11
  const uint8_t c12_c14 = hamming84(packet, 9);
  const int c12 = c12_c14 & 1;
  const int c13 = (c12_c14 >> 1) & 1;
  const int c14 = (c12_c14 >> 2) & 1;
  // C12 is the most significant of the three (teletext_page.h).
  snap.national_option_subset = (c12 << 2) | (c13 << 1) | c14;

  // The configured default G0 set is a transmission property (§15.2); a Level
  // 1 service transmits no X/28 to override it, so the page carries it.
  snap.g0_set = settings_.g0_set;
  snap.second_g0_set = settings_.second_g0_set;
  // 525-line recordings read codes 4/0-5/F as mosaic blocks rather than
  // blast-through capitals; the default here matches the Level 1 625-line
  // convention and can be overridden via settings where the material differs.
  snap.mosaic_blast_through = true;

  // Header display row: 32 bytes from packet offset 10, placed at columns
  // 8-39 so the page-number prefix area (columns 0-7) is left blank for the
  // viewer to stamp with the page label, as the existing pipeline does.
  page->row_bytes[0].fill(0x20);
  for (size_t i = 0; i < 32 && (10 + i) < kTeletextT42Bytes; ++i) {
    page->row_bytes[0][8 + i] = packet[10 + i];
  }
  parse_display_row(*page, 0, page->row_bytes[0].data(),
                    page->row_bytes[0].size(), settings_);
  snap.row_received[0] = true;
  snap.row_copies[0] = 1;
}

void TeletextDecoder::add_display_row(int magazine, int row,
                                       const uint8_t* packet) {
  OpenPage* page = magazine_page(magazine);
  if (page == nullptr || !page->open) {
    // A display row for a magazine with no open header is a recording that
    // started part way through a page; there is nothing to attach it to.
    return;
  }

  // The display payload of rows 1-24 is the 40 bytes after the MRAG.
  std::array<uint8_t, TeletextPageSnapshot::kColumns> bytes{};
  for (size_t i = 0; i < bytes.size() && (2 + i) < kTeletextT42Bytes; ++i) {
    bytes[i] = packet[2 + i];
  }

  // Row squashing: a second copy of a row that matches the first is a vote of
  // confidence, not a redraw. A differing copy overwrites — Level 1 has no
  // per-byte confidence to do better than that, and a header has already
  // committed the page to this magazine.
  const bool already = page->snapshot.row_received[row];
  if (already && page->row_bytes[row] == bytes) {
    page->snapshot.row_copies[row] =
        std::min(page->snapshot.row_copies[row] + 1, 255);
  } else {
    page->row_bytes[row] = bytes;
    parse_display_row(*page, row, bytes.data(), bytes.size(), settings_);
    page->snapshot.row_received[row] = true;
    page->snapshot.row_copies[row] = already
                                         ? page->snapshot.row_copies[row]
                                         : 1;
  }
}

void TeletextDecoder::emit_page(int magazine, bool complete) {
  OpenPage* page = magazine_page(magazine);
  if (page == nullptr || !page->open || !callback_) {
    if (page != nullptr) {
      page->open = false;
    }
    return;
  }
  page->snapshot.transmission_complete = complete;
  // header_field_index/last_field_index are not recoverable from a T42 stream
  // alone (they need the field numbering the slicer had); left at 0 for the
  // on-device .t34 path, where there is no field context to carry.
  callback_(page->snapshot);
  ++stats_.pages_emitted;
  page->open = false;
}

void TeletextDecoder::flush() {
  for (int magazine = 1; magazine <= 8; ++magazine) {
    OpenPage* page = magazine_page(magazine);
    if (page != nullptr && page->open) {
      emit_page(magazine, false);
    }
  }
}

void TeletextDecoder::parse_display_row(OpenPage& page, int row,
                                         const uint8_t* bytes, size_t length,
                                         const TeletextDecoderSettings& settings) {
  TeletextPageSnapshot& snap = page.snapshot;
  RowState state;
  // The page's default G0 set, and the second set the ESC spacing attribute
  // toggles to (§12.2 1/B). Where no second set is designated, ESC does
  // nothing and every cell reads in the default.
  const bool have_second = settings.second_g0_set.has_value();
  uint8_t previous_code = 0;
  bool have_previous = false;

  auto emitCell = [&](uint8_t code) {
    if (state.double_width) {
      state.rendered = !state.rendered;
    } else {
      state.rendered = true;
    }
  };

  auto emitControlPlaceholder = [&]() {
    if (state.hold_mosaic) {
      const bool prev_sep = state.separated_mosaic;
      state.separated_mosaic = state.held_separated;
      emitCell(state.held_mosaic);
      state.separated_mosaic = prev_sep;
    } else {
      emitCell(0x20);
    }
  };

  const size_t columns = std::min<size_t>(length, TeletextPageSnapshot::kColumns);
  for (size_t index = 0; index < TeletextPageSnapshot::kColumns; ++index) {
    uint8_t code = 0x20;
    if (index < columns) {
      code = static_cast<uint8_t>(bytes[index]) & 0x7F;
    }
    const uint8_t high = code & 0xF0;
    const uint8_t low = code & 0x0F;

    TeletextPageCell& cell = snap.cells[row][index];
    cell = TeletextPageCell{};
    cell.foreground = state.foreground;
    cell.background = state.background;
    cell.mosaic = state.mosaic;
    cell.separated_mosaic = state.separated_mosaic;
    cell.flash = state.flash;
    cell.conceal = state.conceal;
    cell.boxed = state.boxed;

    // G0 set resolution: ESC toggles between the page default and its second
    // set. g0_set_uncertain is left false — a T42 stream has no per-byte
    // damage signal to mark the cells after a suspect ESC.
    if (state.escape && have_second) {
      cell.g0_set = settings.second_g0_set->g0_set;
      cell.national_option_subset = settings.second_g0_set->national_option_subset;
    } else {
      cell.g0_set = settings.g0_set;
      cell.national_option_subset = snap.national_option_subset;
    }

    if (high == 0x00) {
      if (low < 0x08) {
        // 0/0-0/7: alpha foreground colour (set after the placeholder).
        emitControlPlaceholder();
        cell.character = 0x20;
        state.foreground = to_teletext_colour(low);
        state.mosaic = false;
        state.conceal = false;
        state.held_mosaic = 0x20;
      } else if (low == 0x08) {
        emitControlPlaceholder();
        cell.character = 0x20;
        state.flash = true;
      } else if (low == 0x09) {
        state.flash = false;
        emitControlPlaceholder();
        cell.character = 0x20;
      } else if (low == 0x0A) {
        // End box: takes effect on the second consecutive 0/A (vhs-teletext).
        if (have_previous && previous_code == 0x0A) {
          state.boxed = false;
          emitControlPlaceholder();
        } else {
          emitControlPlaceholder();
        }
        cell.character = 0x20;
      } else if (low == 0x0B) {
        // Start box: takes effect on the second consecutive 0/B (vhs-teletext).
        if (have_previous && previous_code == 0x0B) {
          state.boxed = true;
          emitControlPlaceholder();
        } else {
          emitControlPlaceholder();
        }
        cell.character = 0x20;
      } else {
        // 0/C-0/F: normal/double height/width/size (set after if a size is
        // requested, set at otherwise — vhs-teletext).
        const bool dh = (low & 0x01) != 0;
        const bool dw = (low & 0x02) != 0;
        if (dh || dw) {
          emitControlPlaceholder();
          cell.character = 0x20;
          state.double_height = dh;
          state.double_width = dw;
          state.held_mosaic = 0x20;
        } else {
          state.double_height = false;
          state.double_width = false;
          state.held_mosaic = 0x20;
          emitControlPlaceholder();
          cell.character = 0x20;
        }
      }
      cell.double_height = state.double_height;
      have_previous = true;
      previous_code = code;
      continue;
    }

    if (high == 0x10) {
      if (low < 0x08) {
        // 1/0-1/7: mosaic foreground colour (set after).
        emitControlPlaceholder();
        cell.character = 0x20;
        state.foreground = to_teletext_colour(low);
        state.mosaic = true;
        state.conceal = false;
      } else if (low == 0x08) {
        state.conceal = true;
        emitControlPlaceholder();
        cell.character = 0x20;
        cell.conceal = state.conceal;
      } else if (low == 0x09) {
        state.separated_mosaic = false;  // contiguous (solid)
        emitControlPlaceholder();
        cell.character = 0x20;
        cell.separated_mosaic = state.separated_mosaic;
      } else if (low == 0x0A) {
        state.separated_mosaic = true;  // separated
        emitControlPlaceholder();
        cell.character = 0x20;
        cell.separated_mosaic = state.separated_mosaic;
      } else if (low == 0x0B) {
        emitControlPlaceholder();
        cell.character = 0x20;
        state.escape = have_second ? !state.escape : state.escape;
      } else if (low == 0x0C) {
        state.background = TeletextColour::Black;
        emitControlPlaceholder();
        cell.character = 0x20;
        cell.background = state.background;
      } else if (low == 0x0D) {
        state.background = state.foreground;
        emitControlPlaceholder();
        cell.character = 0x20;
        cell.background = state.background;
      } else if (low == 0x0E) {
        state.hold_mosaic = true;
        emitControlPlaceholder();
        cell.character = state.held_mosaic;
        cell.held_mosaic = true;
      } else {  // 0x0F: release mosaic
        emitControlPlaceholder();
        cell.character = 0x20;
        state.hold_mosaic = false;
      }
      cell.mosaic = state.mosaic;
      cell.foreground = state.foreground;
      have_previous = true;
      previous_code = code;
      continue;
    }

    // Graphic character.
    cell.character = code;
    // Mosaic characters (codes with bit 5 set, outside the 0x40-0x5F
    // blast-through range) update the held mosaic (vhs-teletext).
    if (state.mosaic && (code & 0x20) != 0 && !(code >= 0x40 && code < 0x60)) {
      state.held_mosaic = code;
      state.held_separated = state.separated_mosaic;
      cell.held_mosaic = true;
    } else if (state.mosaic && (code >= 0x40 && code < 0x60)) {
      // Blast-through capitals stay alphanumeric even in mosaic mode.
      cell.mosaic = false;
    }
    emitCell(code);
    have_previous = true;
    previous_code = code;
  }

  // Mark the lower half of double-height rows: a row whose origin declared
  // double height is followed by a lower row with no foreground data of its
  // own (§12.2 0/D). A full Level 1 renderer pairs them; the flag is set so a
  // consumer that lays rows out by height can find the lower rows.
  if (state.double_height && row >= 1 && row < TeletextPageSnapshot::kRows - 1) {
    snap.cells[row][0].double_height = true;
  }
}

}  // namespace tbc::vbi
