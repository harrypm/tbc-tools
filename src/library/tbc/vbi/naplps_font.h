/*
 * File:        naplps_font.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Fixed bitmap faces for depositing NAPLPS characters into a
 *              receiver's pixel grid
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/nabts_sink/naplps_font.h) at tag v2.7.2
 * (commit fef0115a). Algorithmic bodies are intact; the orc:: namespace was
 * re-namespaced to tbc::vbi::. The companion naplps_font.cpp is a vendored
 * generated table (see its header comment for provenance).
 */

#ifndef TBC_VBI_NAPLPS_FONT_H
#define TBC_VBI_NAPLPS_FONT_H

#include <cstddef>
#include <cstdint>

#include "naplps_render_grid.h"

namespace tbc::vbi {

/**
 * @file
 * @brief The character patterns a receiver deposits for the coded sets
 *
 * X3.110 §5.1 leaves the patterns to the receiver — "the particular patterns
 * (font) chosen for the characters are implementation-dependent and are
 * constrained only by the specified character field at each size for a given
 * display resolution" — so a decoder emulating one has to choose a font. The
 * clause makes that choice a property of the receiver rather than of the page:
 * a set with more pixels held a character generator with finer patterns, and
 * magnifying a coarse one is not the same thing. The plugin therefore carries
 * several faces and picks between them by the field a character is drawn into
 * (@ref naplps_font_face_for_field).
 *
 * Every face is a member of the X11 "misc fixed" family, which is public
 * domain (X.Org font-misc-misc; "Public domain font. Share and enjoy.") and so
 * redistributable under this project's GPL-3.0-or-later:
 *
 * - 6 by 10, from `6x10.bdf`. The cell Appendix B arrives at from first
 *   principles — "psychological studies have shown that the most readable
 *   characters in this size range are 6 pixels by 10 pixels" — and the one a
 *   256 by 200 receiver gives the default character field. It is legible at
 *   the smallest field the standard requires (Table D1 item 5(g): dx = 6/256,
 *   dy = 8/256), which is what the reference receiver needs of it.
 * - 9 by 15, from `9x15.bdf`.
 * - 10 by 20, from `10x20.bdf`.
 *
 * The tables are generated from those BDF sources by decode-orc's
 * `tools/generate_naplps_font.py`, so what is in the tree can be checked
 * against what it came from rather than trusted. The generator makes one
 * documented departure from upstream: the 6 by 10 draws a full stop as a
 * five-pixel diamond straddling the baseline, and builds its colon and
 * semicolon from the same mark, which on a page of leader dots reads as a row
 * of crosses. Those three glyphs are replaced by the square block the finer
 * faces use, scaled to the cell. The substitutions are stated in the
 * generator, and are the only cells in any face that are not upstream's.
 *
 * Patterns are keyed by Unicode code point rather than by code position,
 * because the mapping from a NAPLPS code position to a character is already
 * made once for text extraction (nabts_primary_to_unicode() and
 * nabts_supplementary_to_unicode()); keying on the result is what stops the
 * page that is drawn and the text that is searched disagreeing about what a
 * byte meant.
 */

/**
 * @brief One fixed bitmap face
 *
 * @ref rows holds @ref count patterns of @ref height rows each, in the order
 * @ref codes lists them. A pattern's rows run top down, and within a row bit
 * (@ref width - 1) is the leftmost column — so a row reads left to right as it
 * is written.
 */
struct NaplpsFontFace {
  /// The upstream face, for diagnostics and for naming it in a test failure.
  const char* name;
  int width;
  int height;
  const char32_t* codes;
  size_t count;
  const uint16_t* rows;
};

/// How many faces the plugin carries.
size_t naplps_font_face_count();

/// Face @p index, ordered by ascending cell size.
const NaplpsFontFace& naplps_font_face(size_t index);

/**
 * @brief The pattern for @p code in @p face, or nullptr where it has none
 *
 * A code point no face carries is not an error: §5.1 does not guarantee
 * "character legibility ... at all sizes, in all colors, and at all display
 * resolutions", let alone that a receiver holds every character. A caller
 * draws nothing for it, which is what a receiver with no pattern does.
 */
const uint16_t* naplps_face_pattern(const NaplpsFontFace& face, char32_t code);

/**
 * @brief The face a receiver at @p grid draws a @p columns by @p rows field
 * from
 *
 * Two rules, in that order.
 *
 * A receiver holds the generator its own resolution called for. The reference
 * model's is the 6 by 10 and nothing else: its default character field is
 * exactly that cell, so a finer pattern is one it could never show, and the
 * larger fields it does offer are the ones a set-top decoder drew by doubling
 * the cell it had. A receiver above the reference grid holds the whole set.
 *
 * Within that, the face is the finest one the field can hold whose cell height
 * divides the field's a whole number of times. A pattern is deposited by
 * sampling the field back into it, so a field that is not a whole multiple of
 * the face duplicates some of its rows and not others, which is what breaks a
 * letterform — the same reason naplps_render_grid.h offers only whole
 * multiples of the reference grid. The rule is stated on rows rather than on
 * both axes because only rows can satisfy it: every character field height the
 * standard names is a whole number of pixels on all three grids (the default
 * dy = 5/128 is 10 rows, 20 and 30), while the widths are not (dx = 1/40 is
 * 6.4 pixels, 12.8 and 19.2), so the column count is a rounding no face can
 * divide. Where no face divides the field, the finest one that fits it is
 * drawn instead, and where none fits — a field narrower or shorter than the
 * smallest cell — the 6 by 10 is squeezed into it, which is what a receiver
 * with one generator and a field below it does.
 */
inline const NaplpsFontFace& naplps_font_face_for_field(
    const NaplpsRenderGrid& grid, int columns, int rows) {
  const size_t held =
      grid.width <= kNaplpsGridReference.width ? 1u : naplps_font_face_count();

  const NaplpsFontFace* chosen = nullptr;
  bool chosen_divides = false;
  int chosen_elements = 0;
  for (size_t index = 0; index < held; ++index) {
    const NaplpsFontFace& face = naplps_font_face(index);
    if (face.width > columns || face.height > rows) {
      continue;  // the field cannot hold the cell
    }
    const bool divides = (rows % face.height) == 0;
    const int elements = face.width * face.height;
    if (chosen != nullptr) {
      if (chosen_divides && !divides) {
        continue;
      }
      if (divides == chosen_divides && elements <= chosen_elements) {
        continue;
      }
    }
    chosen = &face;
    chosen_divides = divides;
    chosen_elements = elements;
  }
  return chosen != nullptr ? *chosen : naplps_font_face(0);
}

}  // namespace tbc::vbi

#endif  // TBC_VBI_NAPLPS_FONT_H
