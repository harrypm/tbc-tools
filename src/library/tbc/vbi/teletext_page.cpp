/*
 * File:        teletext_page.cpp
 * Module:      tbc-library (shared VBI services)
 * Purpose:     WST Level 1 G0 character set tables and code-to-Unicode mapping
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/common/vbi-services/teletext_page_decoder.cpp) at tag
 * v2.7.2 (commit fef0115a). Only the tables and free functions that belong to
 * the snapshot model were extracted; the decoder-class methods stay with the
 * WST decoder track. The orc:: namespace was re-namespaced to tbc::vbi::.
 */

#include "teletext_page.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tbc::vbi {

namespace {

// The thirteen G0 positions a national option sub-set replaces
// (ETSI EN 300 706 §15.6.1 Table 35 NOTE 2 — the shaded positions).
constexpr std::array<uint8_t, 13> kNationalOptionPositions = {
    0x23, 0x24, 0x40, 0x5B, 0x5C, 0x5D, 0x5E,
    0x5F, 0x60, 0x7B, 0x7C, 0x7D, 0x7E};

// ETSI EN 300 706 §15.6.2 Table 36, the seven sub-sets a Level 1 page can
// reach through C12-C14, in TeletextNationalOption order. Columns follow
// kNationalOptionPositions.
//
// Two glyphs the standard draws rather than names take the nearest Unicode:
// English 6/0 is a horizontal line across the character rectangle (em dash
// here) and 7/C a double vertical line (U+2016). Note that the sub-sets which
// take 2/3 for a currency or accented character put the '#' the primary set
// holds there at 5/F instead, rather than dropping it.
constexpr std::array<std::array<char32_t, 13>, 7> kNationalOptionSubsets = {{
    // English
    {U'£', U'$', U'@', U'←', U'½', U'→', U'↑', U'#', U'—', U'¼', U'‖', U'¾',
     U'÷'},
    // German
    {U'#', U'$', U'§', U'Ä', U'Ö', U'Ü', U'^', U'_', U'°', U'ä', U'ö', U'ü',
     U'ß'},
    // Swedish/Finnish/Hungarian
    {U'#', U'¤', U'É', U'Ä', U'Ö', U'Å', U'Ü', U'_', U'é', U'ä', U'ö', U'å',
     U'ü'},
    // Italian
    {U'£', U'$', U'é', U'°', U'ç', U'→', U'↑', U'#', U'ù', U'à', U'ò', U'è',
     U'ì'},
    // French
    {U'é', U'ï', U'à', U'ë', U'ê', U'ù', U'î', U'#', U'è', U'â', U'ô', U'û',
     U'ç'},
    // Portuguese/Spanish
    {U'ç', U'$', U'¡', U'á', U'é', U'í', U'ó', U'ú', U'¿', U'ü', U'ñ', U'è',
     U'à'},
    // Czech/Slovak
    {U'#', U'ů', U'č', U'ť', U'ž', U'ý', U'í', U'ř', U'é', U'á', U'ě', U'ú',
     U'š'},
}};

// The three Cyrillic G0 primary sets, ETSI EN 300 706 §15.6.4 to §15.6.6
// Tables 38, 39 and 40, as the 96 code points of positions 2/0 to 7/F in
// ascending code order.
//
// Unlike the Latin set these reserve no positions for national option
// sub-sets: each is a complete alphabet in its own right, chosen by the
// character set *designation* rather than by the header's C12-C14 bits
// (§15.2 Table 32, designation 0100). Every one of them therefore has to be
// written out in full — the letters are not an ASCII-compatible permutation of
// anything, and only positions 2/0-2/5, 2/7-2/9, 2/B-3/F agree with Latin.
//
// Common to all three (each table's NOTE 1 to NOTE 3):
//   - 2/0 is SPACE;
//   - 2/A is the asterisk, replaced by '@' only when the set is reached
//     through a packet X/26 column address triplet, which a Level 1 page has
//     no way to send;
//   - 7/F is a filled rectangle, as it is in the Latin set.

// Table 38 — Option 1, Serbian/Croatian. The Macedonian Ѓ/Ќ at 5/7 and 5/1
// come with it. The block at 7/F takes the position lowercase џ would have
// held, so this set has Џ and no џ; that is the table as printed.
constexpr std::array<char32_t, 96> kCyrillic1G0 = {
    /* 2/0 */ U' ', U'!', U'"', U'#', U'$', U'%', U'&', U'\'',
    /* 2/8 */ U'(', U')', U'*', U'+', U',', U'-', U'.', U'/',
    /* 3/0 */ U'0', U'1', U'2', U'3', U'4', U'5', U'6', U'7',
    /* 3/8 */ U'8', U'9', U':', U';', U'<', U'=', U'>', U'?',
    /* 4/0 */ U'Ч', U'А', U'Б', U'Ц', U'Д', U'Е', U'Ф', U'Г',
    /* 4/8 */ U'Х', U'И', U'Ј', U'К', U'Л', U'М', U'Н', U'О',
    /* 5/0 */ U'П', U'Ќ', U'Р', U'С', U'Т', U'У', U'В', U'Ѓ',
    /* 5/8 */ U'Љ', U'Њ', U'З', U'Ћ', U'Ж', U'Ђ', U'Ш', U'Џ',
    /* 6/0 */ U'ч', U'а', U'б', U'ц', U'д', U'е', U'ф', U'г',
    /* 6/8 */ U'х', U'и', U'ј', U'к', U'л', U'м', U'н', U'о',
    /* 7/0 */ U'п', U'ќ', U'р', U'с', U'т', U'у', U'в', U'ѓ',
    /* 7/8 */ U'љ', U'њ', U'з', U'ћ', U'ж', U'ђ', U'ш', U'■',
};

// Table 39 — Option 2, Russian/Bulgarian. The alphabet is laid out as KOI-7
// lays it out, except that Ъ and Ы are the other way round (5/9 and 5/F) and
// the block at 7/F displaces lowercase ы to 2/6, where the Latin set has '&'.
constexpr std::array<char32_t, 96> kCyrillic2G0 = {
    /* 2/0 */ U' ', U'!', U'"', U'#', U'$', U'%', U'ы', U'\'',
    /* 2/8 */ U'(', U')', U'*', U'+', U',', U'-', U'.', U'/',
    /* 3/0 */ U'0', U'1', U'2', U'3', U'4', U'5', U'6', U'7',
    /* 3/8 */ U'8', U'9', U':', U';', U'<', U'=', U'>', U'?',
    /* 4/0 */ U'Ю', U'А', U'Б', U'Ц', U'Д', U'Е', U'Ф', U'Г',
    /* 4/8 */ U'Х', U'И', U'Й', U'К', U'Л', U'М', U'Н', U'О',
    /* 5/0 */ U'П', U'Я', U'Р', U'С', U'Т', U'У', U'Ж', U'В',
    /* 5/8 */ U'Ь', U'Ъ', U'З', U'Ш', U'Э', U'Щ', U'Ч', U'Ы',
    /* 6/0 */ U'ю', U'а', U'б', U'ц', U'д', U'е', U'ф', U'г',
    /* 6/8 */ U'х', U'и', U'й', U'к', U'л', U'м', U'н', U'о',
    /* 7/0 */ U'п', U'я', U'р', U'с', U'т', U'у', U'ж', U'в',
    /* 7/8 */ U'ь', U'ъ', U'з', U'ш', U'э', U'щ', U'ч', U'■',
};

// Table 40 — Option 3, Ukrainian. Option 2's layout with the letters Ukrainian
// does not use replaced by the ones it does: І at 5/9 for Ъ, Є at 5/C for Э,
// Ї at 5/F for Ы, and ї at 2/6 for ы.
constexpr std::array<char32_t, 96> kCyrillic3G0 = {
    /* 2/0 */ U' ', U'!', U'"', U'#', U'$', U'%', U'ї', U'\'',
    /* 2/8 */ U'(', U')', U'*', U'+', U',', U'-', U'.', U'/',
    /* 3/0 */ U'0', U'1', U'2', U'3', U'4', U'5', U'6', U'7',
    /* 3/8 */ U'8', U'9', U':', U';', U'<', U'=', U'>', U'?',
    /* 4/0 */ U'Ю', U'А', U'Б', U'Ц', U'Д', U'Е', U'Ф', U'Г',
    /* 4/8 */ U'Х', U'И', U'Й', U'К', U'Л', U'М', U'Н', U'О',
    /* 5/0 */ U'П', U'Я', U'Р', U'С', U'Т', U'У', U'Ж', U'В',
    /* 5/8 */ U'Ь', U'І', U'З', U'Ш', U'Є', U'Щ', U'Ч', U'Ї',
    /* 6/0 */ U'ю', U'а', U'б', U'ц', U'д', U'е', U'ф', U'г',
    /* 6/8 */ U'х', U'и', U'й', U'к', U'л', U'м', U'н', U'о',
    /* 7/0 */ U'п', U'я', U'р', U'с', U'т', U'у', U'ж', U'в',
    /* 7/8 */ U'ь', U'і', U'з', U'ш', U'є', U'щ', U'ч', U'■',
};

// Lowest G0 code the tables above start at: everything below 2/0 is a spacing
// attribute rather than a character (§15.5).
constexpr uint8_t kFirstG0Code = 0x20;

const std::array<char32_t, 96>* cyrillic_table(TeletextG0Set g0_set) {
  switch (g0_set) {
    case TeletextG0Set::Cyrillic1:
      return &kCyrillic1G0;
    case TeletextG0Set::Cyrillic2:
      return &kCyrillic2G0;
    case TeletextG0Set::Cyrillic3:
      return &kCyrillic3G0;
    case TeletextG0Set::Latin:
      break;
  }
  return nullptr;
}

// The G0 set names the parameter surface and the project file use. They name
// the languages rather than the standard's option numbers, because "Cyrillic
// option 2" tells a user nothing about whether it is the one their recording
// needs.
struct G0SetName {
  TeletextG0Set set;
  const char* name;
};

constexpr std::array<G0SetName, 4> kG0SetNames = {{
    {TeletextG0Set::Latin, "Latin"},
    {TeletextG0Set::Cyrillic1, "Cyrillic (Serbian/Croatian)"},
    {TeletextG0Set::Cyrillic2, "Cyrillic (Russian/Bulgarian)"},
    {TeletextG0Set::Cyrillic3, "Cyrillic (Ukrainian)"},
}};

}  // namespace

bool teletext_odd_parity_valid(uint8_t byte) {
  // ETSI EN 300 706 §8.1: accept when D1..D7 ⊕ P = 1 (odd number of set
  // bits over the whole byte).
  int ones = 0;
  for (int bit = 0; bit < 8; ++bit) {
    ones += (byte >> bit) & 1;
  }
  return (ones % 2) == 1;
}

uint8_t teletext_odd_parity_encode(uint8_t value) {
  uint8_t byte = value & 0x7F;
  int ones = 0;
  for (int bit = 0; bit < 7; ++bit) {
    ones += (byte >> bit) & 1;
  }
  // ETSI EN 300 706 §8.1: P = 1 ⊕ D1 ⊕ ... ⊕ D7.
  if ((ones % 2) == 0) {
    byte |= 0x80;
  }
  return byte;
}

std::string to_string(TeletextG0Set g0_set) {
  for (const G0SetName& entry : kG0SetNames) {
    if (entry.set == g0_set) {
      return entry.name;
    }
  }
  return kG0SetNames.front().name;
}

std::optional<TeletextG0Set> teletext_g0_set_from_string(
    std::string_view name) {
  for (const G0SetName& entry : kG0SetNames) {
    if (name == entry.name) {
      return entry.set;
    }
  }
  return std::nullopt;
}

char32_t teletext_g0_to_unicode(uint8_t code, TeletextG0Set g0_set,
                                int national_option_subset) {
  const uint8_t c = static_cast<uint8_t>(code & 0x7F);
  // Codes 0/0-1/F are spacing attributes, not characters (§15.5).
  if (c < kFirstG0Code) {
    return U' ';
  }

  // A Cyrillic set defines all 96 of its positions, so it is a lookup and the
  // national option sub-set has no part in it.
  if (const std::array<char32_t, 96>* table = cyrillic_table(g0_set);
      table != nullptr) {
    return (*table)[static_cast<size_t>(c - kFirstG0Code)];
  }

  // §15.6.1 Table 35 NOTE 4: 7/F is a rectangle filling the character area.
  if (c == 0x7F) {
    return U'■';
  }
  const size_t subset =
      national_option_subset >= 0 &&
              static_cast<size_t>(national_option_subset) <
                  kNationalOptionSubsets.size()
          ? static_cast<size_t>(national_option_subset)
          : 0;  // Table 32 designates no sub-set for 1 1 1; render as English
  for (size_t i = 0; i < kNationalOptionPositions.size(); ++i) {
    if (kNationalOptionPositions[i] == c) {
      return kNationalOptionSubsets[subset][i];
    }
  }
  // Every remaining Table 35 position coincides with ASCII.
  return static_cast<char32_t>(c);
}

std::string teletext_g0_to_utf8(uint8_t code, TeletextG0Set g0_set,
                                int national_option_subset) {
  const char32_t cp =
      teletext_g0_to_unicode(code, g0_set, national_option_subset);
  std::string out;
  // Table 36 reaches U+2016 at most, so two continuation bytes suffice; the
  // three-byte branch is written out anyway rather than assuming that.
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
  return out;
}

}  // namespace tbc::vbi
