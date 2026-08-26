/*
 * File:        nabts_page.cpp
 * Module:      tbc-library (shared VBI services)
 * Purpose:     The NAPLPS default colour map and character repertoires
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/common/vbi-services/nabts_page.cpp) at tag v2.7.2
 * (commit fef0115a). Algorithmic bodies are intact; the orc:: namespace was
 * re-namespaced to tbc::vbi::.
 */

#include "nabts_page.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "vbi_nominnmax_undef.h"
namespace tbc::vbi {

namespace {

// Bits per gun, which Table D1 item 5(4) fixes at three — "sixteen simultaneous
// colours out of a set of 512 obtained by allocating three bits each to G R &
// B".
constexpr int kGunBits = 3;
constexpr int kGunMax = (1 << kGunBits) - 1;  // 7

// X3.110 §5.3.2.5.2 places the three primaries equidistant around a hue circle
// with blue at 0 degrees, red at 120 and green at 240.
constexpr double kBlueAngle = 0.0;
constexpr double kRedAngle = 120.0;
constexpr double kGreenAngle = 240.0;

/// Angular distance between |a| and |b| the short way round the circle.
double angular_distance(double a, double b) {
  double delta = std::fabs(a - b);
  if (delta > 180.0) {
    delta = 360.0 - delta;
  }
  return delta;
}

/**
 * @brief The hue at |angle| degrees, per the algorithm of X3.110 §5.3.2.5.2
 *
 * The closest primary to the angle is set full on, the furthest full off, and
 * the second closest gets the angular distance from the closest primary divided
 * by 60 degrees — which is the half-separation of two primaries, so the
 * fraction runs from 0 at a pure primary to 1 midway between two.
 *
 * The value is then "normalized by multiplying it by the maximum color value
 * which can be stored for that primary", i.e. by 7 with three bits, "and then
 * rounded to three places". Verified against T.101 Table II-3: this reproduces
 * all eight of its hue entries exactly.
 */
NabtsColour hue_at(double angle) {
  struct Primary {
    double angle;
    uint8_t* component;
  };

  NabtsColour colour;
  Primary primaries[3] = {
      {kGreenAngle, &colour.green},
      {kRedAngle, &colour.red},
      {kBlueAngle, &colour.blue},
  };

  // Sort by distance from the hue: closest, second, furthest. Three elements,
  // so a couple of swaps rather than a sort.
  double distances[3];
  for (int i = 0; i < 3; ++i) {
    distances[i] = angular_distance(angle, primaries[i].angle);
  }
  for (int i = 0; i < 2; ++i) {
    for (int j = i + 1; j < 3; ++j) {
      if (distances[j] < distances[i]) {
        std::swap(distances[i], distances[j]);
        std::swap(primaries[i], primaries[j]);
      }
    }
  }

  // P1 full on, P3 off, P2 by the fraction above.
  *primaries[0].component = static_cast<uint8_t>(kGunMax);
  *primaries[2].component = 0;
  const double fraction = distances[0] / 60.0;
  *primaries[1].component = static_cast<uint8_t>(
      std::lround(fraction * static_cast<double>(kGunMax)));
  return colour;
}

}  // namespace

void nabts_default_colour_map(NabtsColour (&map)[kNabtsColourMapEntries]) {
  // §5.3.2.5.2: "The first half of the default color map is used to store a
  // complete, uniformly spaced grey scale", G = R = B, black through white.
  constexpr size_t kGreys = kNabtsColourMapEntries / 2;
  for (size_t i = 0; i < kGreys; ++i) {
    const uint8_t level = static_cast<uint8_t>(i);
    map[i] = NabtsColour{level, level, level, false};
  }

  // "The second half ... a full range of hues equally spaced around the
  // perimeter of the hue circle", starting at 0 degrees — blue — and proceeding
  // counterclockwise, which is the direction of increasing angle given where
  // §5.3.2.5.2 places the primaries.
  constexpr size_t kHues = kNabtsColourMapEntries - kGreys;
  for (size_t i = 0; i < kHues; ++i) {
    const double angle =
        360.0 * static_cast<double>(i) / static_cast<double>(kHues);
    map[kGreys + i] = hue_at(angle);
  }
}

namespace {

// The lowest and highest code positions a G-set occupies (X3.110 §4.3.1: a
// G-set is the 96 positions 2/0 to 7/15 of the in-use table).
constexpr uint8_t kFirstCode = 0x20;
constexpr uint8_t kLastCode = 0x7F;
constexpr size_t kSetSize = kLastCode - kFirstCode + 1;

// The supplementary set, code position 2/0 first (X3.110 §5.2, Tables 25 to
// 27). Verified position by position against the Coded Representation column
// of Tables 18 to 27, which name the code position of every character the set
// carries; the layout that falls out is ISO 6937-1982's, which §7.2 names as
// the source, plus X3.110's own additions at 5/6 to 5/11 and 6/5.
//
// Column 4 is Tables 26 and 27's non-spacing marks, given here as the Unicode
// combining marks. They are transmitted *before* the letter they modify, which
// is the reverse of Unicode's order — see nabts_supplementary_is_nonspacing().
constexpr char32_t kSupplementary[kSetSize] = {
    // 2/0 - 2/15
    U' ',
    U'¡',
    U'¢',
    U'£',
    U'$',
    U'¥',
    U'#',
    U'§',
    U'¤',
    U'‘',
    U'“',
    U'«',
    U'←',
    U'↑',
    U'→',
    U'↓',
    // 3/0 - 3/15
    U'°',
    U'±',
    U'²',
    U'³',
    U'×',
    U'µ',
    U'¶',
    U'·',
    U'÷',
    U'’',
    U'”',
    U'»',
    U'¼',
    U'½',
    U'¾',
    U'¿',
    // 4/0 - 4/15: the non-spacing marks. 4/0 is Table 27's "vector overbar",
    // whose graphic the note describes as an arrow above the letter; 4/9 its
    // "slant" and 4/12 its underline.
    U'⃗',
    U'̀',
    U'́',
    U'̂',
    U'̃',
    U'̄',
    U'̆',
    U'̇',
    U'̈',
    U'̸',
    U'̊',
    U'̧',
    U'̲',
    U'̋',
    U'̨',
    U'̌',
    // 5/0 - 5/15. 5/6 to 5/11 are X3.110's line and diagonal graphics (Table 25
    // notes 1 to 8), drawn corner to corner of the character field, so the box
    // and triangle characters are the closest Unicode has.
    U'―',
    U'¹',
    U'®',
    U'©',
    U'™',
    U'♪',
    U'─',
    U'│',
    U'╱',
    U'╲',
    U'◢',
    U'◣',
    U'⅛',
    U'⅜',
    U'⅝',
    U'⅞',
    // 6/0 - 6/15. 6/5 is Table 25's cross, "equivalent to overlaying the full
    // horizontal line character with the full vertical line character".
    U'Ω',
    U'Æ',
    U'Đ',
    U'ª',
    U'Ħ',
    U'┼',
    U'Ĳ',
    U'Ŀ',
    U'Ł',
    U'Ø',
    U'Œ',
    U'º',
    U'Þ',
    U'Ŧ',
    U'Ŋ',
    U'ŉ',
    // 7/0 - 7/15. 7/15 is unassigned.
    U'ĸ',
    U'æ',
    U'đ',
    U'ð',
    U'ħ',
    U'ı',
    U'ĳ',
    U'ŀ',
    U'ł',
    U'ø',
    U'œ',
    U'ß',
    U'þ',
    U'ŧ',
    U'ŋ',
    U' ',
};

/// |code| as UTF-8. Hand-rolled rather than pulled from <codecvt>, which is
/// deprecated, and the repertoire reaches only three bytes.
std::string encode_utf8(char32_t code) {
  std::string out;
  if (code < 0x80) {
    out.push_back(static_cast<char>(code));
  } else if (code < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
  }
  return out;
}

}  // namespace

char32_t nabts_primary_to_unicode(uint8_t code) {
  // 7/15 is DELETE in ASCII rather than a graphic, and anything below 2/0 is a
  // control that never reaches a character primitive.
  if (code < kFirstCode || code >= kLastCode) {
    return U' ';
  }
  return static_cast<char32_t>(code);
}

char32_t nabts_supplementary_to_unicode(uint8_t code) {
  if (code < kFirstCode || code > kLastCode) {
    return U' ';
  }
  return kSupplementary[code - kFirstCode];
}

bool nabts_supplementary_is_nonspacing(uint8_t code) {
  // Column 4 exactly: Tables 26 and 27 assign every one of its sixteen
  // positions to a mark, and no other column carries one.
  return code >= 0x40 && code <= 0x4F;
}

bool nabts_is_mosaic_code(uint8_t code) {
  // Figure 62: b6 = 1 selects a mosaic, which is columns 2, 3, 6 and 7. §5.4
  // adds the second copy of the solid mosaic at 5/15.
  return (code & 0x20) != 0 || code == 0x5F;
}

uint8_t nabts_mosaic_sixels(uint8_t code) {
  // Figure 62 assigns b1 to b5 to the top-left through bottom-left elements and
  // b7 to the bottom-right one, leaving b6 as the marker bit and b8 free. That
  // is the same allocation World System Teletext uses for its G1 sixels, so the
  // packing is the same: bits 0 to 4 straight through, b7 folded down to bit 5.
  return static_cast<uint8_t>((code & 0x1F) | ((code >> 1) & 0x20));
}

std::string nabts_character_to_utf8(uint8_t code,
                                    NabtsPrimitive::Repertoire repertoire) {
  switch (repertoire) {
    case NabtsPrimitive::Repertoire::kPrimary:
      return encode_utf8(nabts_primary_to_unicode(code));
    case NabtsPrimitive::Repertoire::kSupplementary:
      return encode_utf8(nabts_supplementary_to_unicode(code));
    case NabtsPrimitive::Repertoire::kMosaic:
    case NabtsPrimitive::Repertoire::kDrcs:
      // Block graphics and a downloaded bitmap are shapes rather than
      // characters; a text reading of one would be an invention.
      break;
  }
  return " ";
}

namespace {

// Unit-space slack for deciding whether two characters share a baseline.
// Table D1 item 8 puts the nominal resolution at 256 x 200, so one pixel is
// about 1/256; this is far below that and absorbs only the arithmetic of
// resolving a relative coordinate.  Kept at namespace scope because MSVC
// rejects reading a function-local constexpr from a capture-less lambda.
constexpr double kEpsilon = 1e-6;

}  // namespace

std::string nabts_page_text(const NabtsPageSnapshot& page) {
  struct Placed {
    double y = 0.0;
    double x = 0.0;
    double height = 0.0;
    std::string text;
  };

  std::vector<Placed> placed;
  // §7.2: a non-spacing mark is transmitted before the letter it modifies and
  // shares its character field, so it is held until that letter arrives.
  std::string pending_marks;

  for (const NabtsPrimitive& primitive : page.primitives) {
    if (primitive.kind != NabtsPrimitiveKind::kCharacter) {
      continue;
    }
    if (primitive.repertoire == NabtsPrimitive::Repertoire::kMosaic ||
        primitive.repertoire == NabtsPrimitive::Repertoire::kDrcs) {
      // A block graphic or a downloaded bitmap has no text form; reading one as
      // a space would put gaps in the text that the record never had.
      continue;
    }
    if (primitive.repertoire == NabtsPrimitive::Repertoire::kSupplementary &&
        nabts_supplementary_is_nonspacing(primitive.character)) {
      pending_marks +=
          nabts_character_to_utf8(primitive.character, primitive.repertoire);
      continue;
    }

    Placed entry;
    entry.y = primitive.origin.y;
    entry.x = primitive.origin.x;
    entry.height = std::fabs(primitive.size.dy);
    entry.text =
        nabts_character_to_utf8(primitive.character, primitive.repertoire);
    entry.text += pending_marks;
    pending_marks.clear();
    placed.push_back(std::move(entry));
  }

  if (placed.empty()) {
    return {};
  }

  std::stable_sort(placed.begin(), placed.end(),
                   [](const Placed& lhs, const Placed& rhs) {
                     if (std::fabs(lhs.y - rhs.y) > kEpsilon) {
                       return lhs.y > rhs.y;  // top of the screen first
                     }
                     return lhs.x < rhs.x;
                   });

  std::string out;
  double line_y = placed.front().y;
  double line_height = placed.front().height;
  for (size_t i = 0; i < placed.size(); ++i) {
    if (i > 0) {
      const double tolerance =
          (std::max)((std::max)(line_height, placed[i].height) / 2.0, kEpsilon);
      if (std::fabs(placed[i].y - line_y) > tolerance) {
        out += '\n';
        line_y = placed[i].y;
        line_height = placed[i].height;
      }
    }
    out += placed[i].text;
  }
  return out;
}

}  // namespace tbc::vbi
