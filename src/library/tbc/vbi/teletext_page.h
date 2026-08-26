/*
 * File:        teletext_page.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     WST (System B) teletext Level 1 page snapshot model — the
 *              decode/render boundary value types shared by the decoder and
 *              the viewer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/common/vbi-services/teletext_page_decoder.h) at tag
 * v2.7.2 (commit fef0115a). Only the snapshot value types and the free
 * functions that map G0 codes to Unicode/UTF-8 were extracted here; the
 * stateful TeletextPageDecoder class (which depends on teletext_row_squasher
 * and teletext_slicer) is ported separately by the WST decoder track. The
 * orc:: namespace was re-namespaced to tbc::vbi::.
 */

#ifndef TBC_VBI_TELETEXT_PAGE_H
#define TBC_VBI_TELETEXT_PAGE_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tbc::vbi {

// Check a display byte for odd parity.
// ETSI EN 300 706 §8.1: bit 8 is the parity bit, bits 1-7 carry the data;
// the byte is accepted when it contains an odd number of '1' bits.
bool teletext_odd_parity_valid(uint8_t byte);

// Encode a 7-bit value as an odd-parity protected byte (ETSI EN 300 706
// §8.1). Only the low 7 bits of |value| are used.
uint8_t teletext_odd_parity_encode(uint8_t value);

// National option sub-sets a Level 1 page can select, in the order the C12,
// C13 and C14 header bits designate them.
//
// ETSI EN 300 706 §15.2 Table 32 indexes the sub-set by the default G0/G2
// designation *and* those three bits; a Level 1 page has no packet X/28 or
// M/29 to carry a designation, so §15.2 says the sub-set "is defined by the
// C12, C13 and C14 control bits in the page header alone" and the default
// designation row (triplet 1 bits 14-11 = 0000) is the one that applies.
// C12 is the most significant of the three bits, as the table prints them.
//
// Table 32 leaves 1 1 1 blank for this designation — no sub-set is defined —
// and there is nothing to fall back on: Table 35's own glyphs at these
// positions apply only when the set is reached through a packet X/26
// (Table 35 NOTE 2). It is rendered as English.
enum class TeletextNationalOption : uint8_t {
  English = 0,
  German = 1,
  SwedishFinnishHungarian = 2,
  Italian = 3,
  French = 4,
  PortugueseSpanish = 5,
  CzechSlovak = 6,
  Undefined = 7,
};

// G0 primary character sets a Level 1 page can be displayed in.
//
// The G0 set is a property of the transmission rather than of the page's
// header bits: ETSI EN 300 706 §15.2 designates it by the four *Default G0 and
// G2 Character Set Designation* bits of a packet X/28/0 Format 1 or M/29/0,
// and the three national option bits then choose a sub-set within it. A Level
// 1 service transmits neither packet, and §15.2 is explicit about what happens
// then: "in the absence of a packet X/28/0 Format 1, X/28/4, M/29/0 or M/29/4,
// the default sets are established by a local Code of Practice" — that is, by
// where the receiver was sold. That is why this is a decoder setting and not
// something recoverable from the stream.
//
// Only the sets Table 32 reaches with a Cyrillic designation are enumerated
// here alongside Latin. Greek, Arabic and Hebrew are defined by the standard
// and not implemented; a page designating one of them is displayed as Latin.
enum class TeletextG0Set : uint8_t {
  // §15.6.1 Table 35, with the national option sub-sets of Table 36.
  Latin = 0,
  // §15.6.4 Table 38 — Serbian/Croatian (Table 32 designation 0100, national
  // option bits 000).
  Cyrillic1 = 1,
  // §15.6.5 Table 39 — Russian/Bulgarian (designation 0100, bits 100).
  Cyrillic2 = 2,
  // §15.6.6 Table 40 — Ukrainian (designation 0100, bits 101).
  Cyrillic3 = 3,
};

// A designated G0 set together with the national option sub-set the
// designation itself names for it.
//
// The two travel together because ETSI EN 300 706 §15.2 Table 32 and §15.3
// Table 33 are single 7-bit values selecting both at once. It matters for the
// *second* G0 set in particular: §15.3 says in as many words that "the national
// option sub-set selected by the C12, C13 and C14 bits is not relevant to the
// secondary set", so a second Latin set carries its own sub-set and cannot
// borrow the page header's.
struct TeletextG0Designation {
  TeletextG0Set g0_set = TeletextG0Set::Latin;
  // A TeletextNationalOption; only consulted when |g0_set| is Latin, the
  // Cyrillic sets reserving no positions for a sub-set.
  int national_option_subset = 0;
};

// Human-readable name of a G0 set, as the parameter surface spells it.
std::string to_string(TeletextG0Set g0_set);

// The G0 set a name from to_string() denotes, or std::nullopt when it names
// none.
std::optional<TeletextG0Set> teletext_g0_set_from_string(std::string_view name);

// Map a 7-bit G0 display code to its Unicode code point.
//
// For TeletextG0Set::Latin this is the primary set of ETSI EN 300 706 §15.6.1
// Table 35 with the |national_option_subset| substitutions of §15.6.2 Table 36
// at the thirteen positions Table 35 NOTE 2 reserves for them (2/3, 2/4, 4/0,
// 5/B-5/F, 6/0 and 7/B-7/E). The Cyrillic sets define all 96 positions
// themselves and reserve none, so the sub-set is not consulted for them.
//
// |national_option_subset| is a TeletextNationalOption, i.e. the value
// TeletextPageSnapshot::national_option_subset carries; anything outside its
// range is treated as English. Codes below 2/0 are spacing attributes rather
// than characters and return SPACE.
char32_t teletext_g0_to_unicode(uint8_t code, TeletextG0Set g0_set,
                                int national_option_subset);

// teletext_g0_to_unicode() encoded as UTF-8, for text output (subtitle cues
// and the like) rather than a glyph grid.
std::string teletext_g0_to_utf8(uint8_t code, TeletextG0Set g0_set,
                                int national_option_subset);

// Level 1 display colours in spacing-attribute code order.
// ETSI EN 300 706 §12.2 Table 26: alpha colour codes 0/0-0/7 and mosaic
// colour codes 1/0-1/7 select black through white in this order.
enum class TeletextColour : uint8_t {
  Black = 0,
  Red = 1,
  Green = 2,
  Yellow = 3,
  Blue = 4,
  Magenta = 5,
  Cyan = 6,
  White = 7,
};

// One rendered character cell of a Level 1 page.
struct TeletextPageCell {
  // 7-bit transmitted code (odd parity removed). For alphanumeric cells this
  // indexes the current G0 set; for mosaic cells the G1 set. Cells occupied
  // by a spacing attribute hold 0x20 (SPACE), or the held-mosaic character
  // when |held_mosaic| is set (EN 300 706 §12.2 code 1/E).
  uint8_t character = 0x20;
  TeletextColour foreground = TeletextColour::White;
  TeletextColour background = TeletextColour::Black;
  // G1 mosaic set selected (EN 300 706 §12.2 codes 1/1-1/7). Character codes
  // 0x40-0x5F remain alphanumeric capitals even in mosaic mode.
  bool mosaic = false;
  // Separated (bordered) rather than contiguous mosaic blocks (§12.2 1/A).
  bool separated_mosaic = false;
  // Cell is a spacing attribute displayed as the held mosaic character
  // (§12.2 1/E); |separated_mosaic| then reflects the held character's
  // original mode.
  bool held_mosaic = false;
  // Origin (upper) cell of a double-height pair (§12.2 0/D).
  bool double_height = false;
  // Lower cell of a double-height pair: no foreground data, background
  // copied from the origin row (§12.2 0/D).
  bool double_height_lower = false;
  // §12.2 0/8: foreground pixels alternate with the background colour, at a
  // rate the renderer chooses.
  bool flash = false;
  bool conceal = false;  // §12.2 1/8: display as SPACE until revealed
  bool boxed = false;    // inside a Start Box/End Box region (§12.2 0/A-0/B)
  // The transmitted byte failed odd parity (EN 300 706 §8.1); |character| is
  // replaced with SPACE and the cell flagged so renderers can mark it.
  bool parity_error = false;

  // G0 set |character| is to be read in, and the national option sub-set that
  // goes with it. Resolved per cell rather than per page because the ESC
  // spacing attribute (§12.2 Table 26 code 1/B) toggles the row between the
  // page's default G0 set and its second one, so a single row can hold two
  // alphabets — the very thing two-alphabet services use it for. A consumer
  // converting a cell to a glyph reads these and needs to know nothing about
  // ESC; on a page with no second set designated they are the page's own
  // g0_set and national_option_subset throughout.
  TeletextG0Set g0_set = TeletextG0Set::Latin;
  int national_option_subset = 0;

  // A byte earlier in this row failed odd parity while a second G0 set was in
  // force for the page, so |g0_set| above may be the wrong one of the two: a
  // damaged byte cannot be told from the ESC it might have been, and one lost
  // ESC inverts every cell after it to the end of the row.
  //
  // Cells are only ever marked from the damage onwards, and never at all on a
  // page with no second set — where ESC does nothing and there is nothing to
  // get wrong. §12.2 Table 26 re-selects the default set at the start of every
  // row, which is what stops this reaching past the row it began in.
  bool g0_set_uncertain = false;
};

// A completed Level 1 page: the 25-row grid (row 0 is the header row) plus
// the page address and header control bits of ETSI EN 300 706 §9.3.1.
struct TeletextPageSnapshot {
  static constexpr int kRows = 25;     // header row 0 + display rows 1-24
  static constexpr int kColumns = 40;  // EN 300 706 §9.3.2: 40 display bytes

  // Display columns to draw. kColumns on both services: the Level 1 display is
  // a 40-column grid whatever the packet length, and a row the service left
  // short simply shows spaces to the right of what it sent — which is what a
  // receiver puts on screen. Carried in the snapshot rather than read from
  // kColumns by consumers so a service that displays fewer can say so.
  int columns = kColumns;

  // Whether character codes 4/0-5/F keep their alphanumeric meaning while
  // mosaic graphics are selected — "blast-through", ETSI EN 300 706 §15.7.1
  // Table 47 NOTE 1 — so that capitals can be written into a graphic without
  // leaving mosaic mode. True on 625 lines.
  //
  // The 525-line recordings say otherwise: their page graphics run codes from
  // that range in among the mosaic ones, and read as mosaics those are the
  // block patterns the drawing needs — 5/F being a solid block. Rendered as
  // capitals they put stray letters through the artwork. Nothing in ITU-R
  // BT.653 settles it, so this is what the material shows rather than what a
  // standard states.
  //
  // A renderer reads this to decide whether a cell in mosaic mode holding such
  // a code is a character or a block; nothing else about the page depends on
  // it.
  bool mosaic_blast_through = true;

  // Displayed magazine number 1-8. Transmission magazine 0 is displayed as
  // magazine 8 (EN 300 706 §3.1 "page number" convention: page 100 = 1/00).
  int magazine = 8;
  // Two-digit hexadecimal page number 0x00-0xFF (EN 300 706 §9.3.1.1).
  int page_number = 0;
  // 13-bit page sub-code S1-S4 (EN 300 706 §9.3.1.2).
  int subcode = 0;

  // Page header control bits (EN 300 706 §9.3.1.3 Table 2).
  bool erase_page = false;            // C4
  bool newsflash = false;             // C5
  bool subtitle = false;              // C6
  bool suppress_header = false;       // C7
  bool update_indicator = false;      // C8
  bool interrupted_sequence = false;  // C9
  bool inhibit_display = false;       // C10
  bool magazine_serial = false;       // C11
  // C12-C14 as a TeletextNationalOption: which national option sub-set the
  // page's G0 set uses (EN 300 706 §15.2 Table 32). C12 is the most
  // significant bit, so the value indexes Table 32 as printed.
  int national_option_subset = 0;

  // G0 primary set the page's alphanumeric codes are read in: what a packet
  // X/28/0 Format 1 designated for this page, failing that what an M/29/0
  // designated for its magazine, failing both the decoder's configured default
  // (EN 300 706 §15.2). The Cyrillic sets reserve no national option
  // positions, so |national_option_subset| above is only consulted when this
  // is Latin.
  //
  // This is the set every row *starts* in (§12.2 Table 26 code 1/B). Where the
  // page also has a second set, the cells say which of the two each of them is
  // actually in; see TeletextPageCell::g0_set.
  TeletextG0Set g0_set = TeletextG0Set::Latin;

  // The page's second G0 set, if it has one: what a packet X/28/0 Format 1 or
  // X/28/4 designated for this page, failing that what an M/29/0 or M/29/4
  // designated for its magazine, failing all four the decoder's configured
  // default (EN 300 706 §15.3).
  //
  // Unset means the ESC spacing attribute does nothing on this page — either
  // because no second set was designated, or because the designation was the
  // 1111111 that §15.3 defines as "no second G0 set required" and says a
  // decoder may read as disabling ESC. Every cell is then in |g0_set| above.
  std::optional<TeletextG0Designation> second_g0_set;

  // Field indices of the header packet that *opened* this transmission and of
  // the last packet that contributed to the page. A header re-sent while the
  // page's own rows are still being transmitted does not restamp the first of
  // these, so every snapshot of one appearance of a page shares a
  // header_field_index and a consumer can use it to tell appearances apart.
  int64_t header_field_index = 0;
  int64_t last_field_index = 0;

  // Whether a packet was received for each row of this page (row 0 = the
  // X/0 header). A row with no packet displays as spaces on a black
  // background, which is indistinguishable from a transmitted blank row —
  // so recovery gaps can only be reported from this flag, never inferred
  // from the cells.
  std::array<bool, kRows> row_received{};

  // Copies of each display row that were combined to produce it: 0 where no
  // packet was received, 1 where the row rests on a single copy, more where
  // repeated transmissions corrected each other.
  //
  // This is the page's confidence in its own rows. One copy is not a fault,
  // but it is unchecked: the row is shown exactly as it was received, and a
  // burst long enough to carry a row's address onto another codeword puts
  // that row in the wrong place with nothing to contradict it. A second copy
  // is what turns that into a vote.
  std::array<int, kRows> row_copies{};

  // Whether the page's transmission had finished when this snapshot was
  // taken. False means more rows of *this* transmission were still to come.
  // A partial snapshot is not damaged data; it is a page that has not all
  // arrived yet, and the two look identical on screen, so only this flag
  // distinguishes them.
  bool transmission_complete = true;

  std::array<std::array<TeletextPageCell, kColumns>, kRows> cells{};
};

// One subtitle cue recovered from a C6-flagged page. Times are expressed as
// the field indices the decoder was fed; consumers convert to seconds via the
// field rate (50 fields/s for 625-line PAL).
struct TeletextSubtitleCue {
  int64_t start_field_index = 0;
  int64_t end_field_index = 0;
  // Plain text, rows separated by '\n', Level 1 attributes dropped.
  std::string text;
};

}  // namespace tbc::vbi

#endif  // TBC_VBI_TELETEXT_PAGE_H
