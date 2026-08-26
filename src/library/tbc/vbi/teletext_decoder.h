/*
 * File:        teletext_decoder.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Decode World System Teletext (System B, Level 1) T42 packet
 *              streams into TeletextPageSnapshot values
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * The stateful page-assembly, row-squashing and Level 1 attribute resolution
 * that decode-orc's teletext_page_decoder/teletext_row_squasher performed are
 * implemented here against the tbc::vbi::TeletextPageSnapshot model from
 * teletext_page.h. The Hamming 8/4 coding helpers and the G0 -> Unicode
 * mapping are reused from vbi_coding.h and teletext_page.h rather than
 * duplicated.
 */

#ifndef TBC_VBI_TELETEXT_DECODER_H
#define TBC_VBI_TELETEXT_DECODER_H

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

#include "teletext_page.h"
#include "vbi_coding.h"

namespace tbc::vbi {

// A T42 packet is the 42-byte MRAG + data payload of an ETSI EN 300 706 §7.1
// teletext packet, after the clock run-in and framing code have been removed.
// This is what a .t34 stream carries end to end.
constexpr size_t kTeletextT42Bytes = 42;

// Configuration that is a property of the transmission rather than of a page's
// own header bits (see TeletextG0Set): the default G0 set the decoder reads
// alphanumeric codes in when no packet X/28 or M/29 designated one, and the
// optional second set that the ESC spacing attribute toggles to.
struct TeletextDecoderSettings {
  TeletextG0Set g0_set = TeletextG0Set::Latin;
  std::optional<TeletextG0Designation> second_g0_set;
};

// What a pass over a packet stream produced.
struct TeletextDecoderStats {
  uint64_t packets_seen = 0;
  uint64_t packets_short = 0;       // fewer than kTeletextT42Bytes
  uint64_t padding_packets = 0;     // all-zero packets, as vhs-teletext is_padding
  uint64_t header_packets = 0;      // row 0
  uint64_t display_packets = 0;     // rows 1-24
  uint64_t rows_above_24 = 0;       // rows 25-31: control/fastext, counted
  uint64_t pages_emitted = 0;
};

/**
 * @brief Turns a temporally ordered T42 packet stream into page snapshots
 *
 * Fed packets in broadcast order and calls back once per page as each
 * completes. A receiver holds one open page per magazine: a row 0 header for a
 * magazine closes whatever page was open for it (delivered, partial if it had
 * rows still to come) and opens a new one, and rows 1-24 fold into the open
 * page. Repeated transmissions of a row squash into one, and the copy count is
 * kept as the page's confidence in that row.
 *
 * Not thread safe: like the NABTS assembler, its value is that it sees the
 * stream in one order on one thread.
 */
class TeletextDecoder {
 public:
  using PageCallback = std::function<void(const TeletextPageSnapshot&)>;

  void set_page_callback(PageCallback callback) {
    callback_ = std::move(callback);
  }

  void set_settings(const TeletextDecoderSettings& settings) {
    settings_ = settings;
  }

  const TeletextDecoderSettings& settings() const { return settings_; }

  /// Take in one T42 packet. A short packet is counted and dropped.
  void add_packet(const uint8_t* packet, size_t length);

  /// End the pass: every page still open is delivered partial. Idempotent.
  void flush();

  const TeletextDecoderStats& stats() const { return stats_; }

 private:
  // One open page per magazine (magazines 1-8, stored at index magazine-1).
  struct OpenPage {
    bool open = false;
    TeletextPageSnapshot snapshot;
    // The display-row payloads that have arrived, kept so a second copy can
    // squash against the first rather than overwriting it silently.
    std::array<std::array<uint8_t, TeletextPageSnapshot::kColumns>, 25>
        row_bytes{};
  };

  OpenPage* magazine_page(int magazine) {
    if (magazine < 1 || magazine > 8) {
      return nullptr;
    }
    return &open_[magazine - 1];
  }

  void begin_page(int magazine, const uint8_t* packet);
  void add_display_row(int magazine, int row, const uint8_t* packet);
  void emit_page(int magazine, bool complete);

  /// Resolve the 40 Level 1 cells of |row| from its display bytes, given the
  /// page's G0 sets and national option sub-set. Row 0 is the header row and
  /// carries 32 display bytes; rows 1-24 carry 40.
  static void parse_display_row(OpenPage& page, int row,
                                const uint8_t* bytes, size_t length,
                                const TeletextDecoderSettings& settings);

  PageCallback callback_;
  TeletextDecoderSettings settings_;
  std::array<OpenPage, 8> open_;
  TeletextDecoderStats stats_;
};

}  // namespace tbc::vbi

#endif  // TBC_VBI_TELETEXT_DECODER_H
