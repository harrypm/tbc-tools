/*
 * File:        naplps_raster.cpp
 * Module:      nabts_sink stage plugin
 * Purpose:     Deposits NAPLPS drawing primitives into a receiver's pixel grid
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "naplps_raster.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include "naplps_font.h"

// std::min/std::max break on MSVC: <algorithm> pulls in min/max macros
// that NOMINMAX on the target does not suppress. #undef of a non-macro
// is a safe no-op, so undefine just these two here, after all includes.
#undef min
#undef max
#ifdef centre_x
#error CENTRE_X_IS_MACRO
#endif
#ifdef centre_y
#error CENTRE_Y_IS_MACRO
#endif

namespace tbc::vbi {

namespace {

// Two coordinates closer than this are the same point. Unit space runs 0 to 1
// over at most 512 cells, so this is several orders of magnitude below anything
// the grid can distinguish and exists only to absorb the arithmetic of
// resolving a relative coordinate.
constexpr double kCoincident = 1e-9;

// Below this a determinant means three colinear control points, which §5.3.3.3
// draws as a line rather than as an arc of unbounded radius.
constexpr double kColinear = 1e-12;

constexpr double kPi = 3.14159265358979323846;

// How far the polyline standing in for an arc may stray from it, as a fraction
// of a cell. Well inside one pixel, so the approximation cannot show up in the
// cells that get painted.
constexpr double kArcToleranceCells = 0.2;

// An arc is never split into more segments than this. A circle of the whole
// screen at the finest grid needs a few thousand, and the cap only bites on a
// radius no display could show.
constexpr int kMaxArcSegments = 8192;

// How far past a filled figure its working grid reaches. Two cells, so that the
// border of that grid is clear of the outline the pel traces however the path
// rounds onto the grid — which is what lets a walk inward from that border
// start outside the figure.
constexpr double kFillMarginCells = 2.0;

double distance_between(const NabtsPoint& a, const NabtsPoint& b) {
  return std::hypot(a.x - b.x, a.y - b.y);
}

/**
 * @brief The cells a continuous range covers, as an inclusive index range
 *
 * Every cell any part of the range falls in, and never fewer than the one |low|
 * itself is in, so a range of no extent still names a cell. This is how an
 * *area* is measured onto the grid: by what it covers.
 */
void cells_overlapping(double low, double high, int& first, int& last) {
  first = static_cast<int>(std::floor(low));
  last = (std::max)(first, static_cast<int>(std::ceil(high)) - 1);
}

/**
 * @brief The cells one axis of the logical pel occupies
 *
 * How a *brush* is measured onto the grid, which is not the same thing as an
 * area. §5.3.2.2.6 sizes the pel in the drawing space and then maps it to "at
 * least one and possibly many display pixels"; nothing in that sizing refers to
 * where the pel happens to be or which way it is being swept, so nor does this.
 * The count of cells comes from the size alone, rounded to whole cells and
 * never below one, laid from the cell the pel's low corner falls in.
 *
 * Measuring the brush by what it covers instead would give it an extra cell
 * whenever it straddled a cell boundary, and a line would come out a pixel
 * heavier here than there along its own length.
 */
int pel_anchor(double position, double extent) {
  return static_cast<int>(std::floor((std::min)(position, position + extent)));
}

int pel_span(double extent) {
  return (std::max)(1, static_cast<int>(std::lround(std::fabs(extent))));
}

void pel_cells(double position, double extent, int& first, int& last) {
  first = pel_anchor(position, extent);
  last = first + pel_span(extent) - 1;
}

double normalised_radians(double angle) {
  double out = std::fmod(angle, 2.0 * kPi);
  if (out < 0.0) {
    out += 2.0 * kPi;
  }
  return out;
}

/**
 * @brief The arc of a circle running start to end the way round that passes
 *        through |through|
 *
 * Three points on a circle fix its centre as their circumcentre. Which of the
 * two arcs between the ends is meant is then decided by the intermediate point:
 * the sweep is taken counter-clockwise unless that would leave the intermediate
 * point on the other arc.
 */
std::vector<NabtsPoint> arc_segment(const NabtsPoint& start,
                                    const NabtsPoint& through,
                                    const NabtsPoint& end,
                                    double tolerance_unit) {
  // Circumcentre by the perpendicular-bisector determinant.
  const double ax = start.x;
  const double ay = start.y;
  const double bx = through.x;
  const double by = through.y;
  const double cx = end.x;
  const double cy = end.y;

  const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
  if (std::fabs(d) < kColinear) {
    // §5.3.3.3: "If the three drawing points are colinear, a line is drawn from
    // the start point to the end point".
    return {start, end};
  }

  const double a_sq = ax * ax + ay * ay;
  const double b_sq = bx * bx + by * by;
  const double c_sq = cx * cx + cy * cy;
  const NabtsPoint centre{
      (a_sq * (by - cy) + b_sq * (cy - ay) + c_sq * (ay - by)) / d,
      (a_sq * (cx - bx) + b_sq * (ax - cx) + c_sq * (bx - ax)) / d};

  const double radius = distance_between(centre, start);
  if (radius < kCoincident) {
    return {start, end};
  }

  const auto angle_of = [&centre](const NabtsPoint& point) {
    return std::atan2(point.y - centre.y, point.x - centre.x);
  };
  const double start_angle = angle_of(start);
  const double sweep_to_through =
      normalised_radians(angle_of(through) - start_angle);
  double sweep = normalised_radians(angle_of(end) - start_angle);
  if (sweep_to_through > sweep) {
    // Counter-clockwise would pass the end before reaching the intermediate
    // point, so the arc meant is the other way round.
    sweep -= 2.0 * kPi;
  }

  // Chord sagitta of a segment spanning t radians is r(1 - cos(t/2)), which for
  // a small t is about r t^2 / 8; inverting that gives the step that keeps the
  // polyline within tolerance.
  const double step = std::sqrt(8.0 * tolerance_unit / radius);
  int segments =
      step > 0.0 ? static_cast<int>(std::ceil(std::fabs(sweep) / step)) : 1;
  segments = std::clamp(segments, 1, kMaxArcSegments);

  std::vector<NabtsPoint> out;
  out.reserve(static_cast<size_t>(segments) + 1);
  for (int i = 0; i <= segments; ++i) {
    const double angle = start_angle + sweep * (static_cast<double>(i) /
                                                static_cast<double>(segments));
    out.push_back(NabtsPoint{centre.x + radius * std::cos(angle),
                             centre.y + radius * std::sin(angle)});
  }
  return out;
}

/**
 * @brief The distance along a path that one period of a line texture occupies
 *
 * §5.3.2.4.2 sizes line textures in the logical pel: a dot is one pel, and for
 * a horizontal line the spacings are measured in pel widths while for a
 * vertical line they are measured in pel heights. For a line at any other angle
 * the standard leaves the algorithm to the implementation, asking only for "a
 * visually consistent effect with that specified for horizontal and vertical
 * lines".
 *
 * The step taken here is the longest one that advances no further than a pel
 * width horizontally and no further than a pel height vertically. It is exactly
 * the pel width on a horizontal line and the pel height on a vertical one, and
 * it collapses to zero — a solid line — exactly where §5.3.2.4.2's closing note
 * says it should: "if logical pel size dx = 0, all nonvertical lines are solid.
 * If logical pel size dy = 0, all nonhorizontal lines are solid."
 */
double texture_step(double unit_dx, double unit_dy, const NaplpsCellSize& pel) {
  double step = std::numeric_limits<double>::infinity();
  if (std::fabs(unit_dx) > kCoincident) {
    step = (std::min)(step, pel.columns / std::fabs(unit_dx));
  }
  if (std::fabs(unit_dy) > kCoincident) {
    step = (std::min)(step, pel.rows / std::fabs(unit_dy));
  }
  return std::isfinite(step) ? step : 0.0;
}

/**
 * @brief One period of a line texture, in pels (§5.3.2.4.2, Table 12)
 *
 * A run is named by its first and last *pel*, because what a run of N pels
 * means on the grid is the pel swept from the run's first pel to its last —
 * N - 1 steps of travel, which the pel's own extent completes to N pels
 * exactly. Sweeping the whole run instead would draw it a pel too long, and a
 * single dot is then one stamp rather than a sweep.
 */
struct TextureRun {
  double first;
  double last;
};
struct TexturePeriod {
  std::vector<TextureRun> on;
  double length = 1.0;
};

TexturePeriod texture_period(NabtsLineTexture texture) {
  switch (texture) {
    case NabtsLineTexture::kSolid:
      return {{{0.0, 1.0}}, 1.0};
    case NabtsLineTexture::kDotted:
      // A dot one pel long, then a gap of the same.
      return {{{0.0, 0.0}}, 2.0};
    case NabtsLineTexture::kDashed:
      // A dash three pels long, then a gap of three.
      return {{{0.0, 2.0}}, 6.0};
    case NabtsLineTexture::kDottedDashed:
      // Dash of three, gap of one, dot of one, gap of one — "the
      // inter-dot-dash spacing is equivalent to the inter-dot spacing".
      return {{{0.0, 2.0}, {4.0, 4.0}}, 6.0};
  }
  return {{{0.0, 1.0}}, 1.0};
}

}  // namespace

// ---------------------------------------------------------------------------
// Ink
// ---------------------------------------------------------------------------

NaplpsInk naplps_ink_of(const NabtsPrimitive& primitive) {
  NaplpsInk ink;
  ink.colour = primitive.colour;
  ink.colour_map_address = primitive.colour_map_address;
  ink.blinking = primitive.blinking;
  ink.blink_to = primitive.blink_to;
  ink.blink_to_map_address = primitive.blink_to_map_address;
  return ink;
}

NaplpsInk naplps_background_ink_of(const NabtsPrimitive& primitive) {
  NaplpsInk ink;
  ink.colour = primitive.background;
  ink.colour_map_address = primitive.background_map_address;
  return ink;
}

// ---------------------------------------------------------------------------
// Surface
// ---------------------------------------------------------------------------

NaplpsCellSurface::NaplpsCellSurface(NaplpsRenderGrid grid) : grid_(grid) {
  const size_t count = static_cast<size_t>((std::max)(0, grid_.width)) *
                       static_cast<size_t>((std::max)(0, grid_.height));
  cells_.assign(count, NaplpsCell{});
}

const NaplpsCell& NaplpsCellSurface::at(int column, int row) const {
  if (!contains(column, row)) {
    return blank_;
  }
  return cells_[static_cast<size_t>(row) * static_cast<size_t>(grid_.width) +
                static_cast<size_t>(column)];
}

void NaplpsCellSurface::deposit(int column, int row, const NaplpsInk& ink) {
  if (!contains(column, row)) {
    return;
  }
  NaplpsCell& cell =
      cells_[static_cast<size_t>(row) * static_cast<size_t>(grid_.width) +
             static_cast<size_t>(column)];
  cell.painted = true;
  cell.colour = ink.colour;
  cell.colour_map_address = ink.colour_map_address;
  cell.blinking = ink.blinking;
  cell.blink_to = ink.blink_to;
  cell.blink_to_map_address = ink.blink_to_map_address;
}

size_t NaplpsCellSurface::painted_count() const {
  size_t count = 0;
  for (const NaplpsCell& cell : cells_) {
    if (cell.painted) {
      ++count;
    }
  }
  return count;
}

// ---------------------------------------------------------------------------
// Run merging
// ---------------------------------------------------------------------------

namespace {

/// Whether two cells would be drawn identically, which is what lets one run
/// stand for both. The colour map address is part of it because a blink
/// process is defined over an entry (§5.3.2.7.2), so two cells of the same
/// colour drawn from different entries can blink apart.
bool cells_match(const NaplpsCell& a, const NaplpsCell& b) {
  return a.painted == b.painted && a.colour == b.colour &&
         a.colour_map_address == b.colour_map_address &&
         a.blinking == b.blinking && a.blink_to == b.blink_to &&
         a.blink_to_map_address == b.blink_to_map_address;
}

}  // namespace

std::vector<NaplpsCellRun> naplps_merge_runs(const NaplpsCellSurface& surface) {
  std::vector<NaplpsCellRun> runs;
  for (int row = 0; row < surface.height(); ++row) {
    int column = 0;
    while (column < surface.width()) {
      const NaplpsCell& cell = surface.at(column, row);
      if (!cell.painted) {
        ++column;
        continue;
      }
      int end = column + 1;
      while (end < surface.width() && cells_match(surface.at(end, row), cell)) {
        ++end;
      }
      NaplpsCellRun run;
      run.column = column;
      run.row = row;
      run.columns = end - column;
      run.cell = cell;
      runs.push_back(run);
      column = end;
    }
  }
  return runs;
}

// ---------------------------------------------------------------------------
// Arc geometry
// ---------------------------------------------------------------------------

std::vector<NabtsPoint> naplps_arc_polyline(
    const std::vector<NabtsPoint>& control, double tolerance_unit) {
  if (control.size() < 2) {
    return control;
  }
  if (control.size() == 2) {
    // What a truncated record leaves behind; a line through the two is the most
    // that can be said of it.
    return control;
  }
  if (control.size() > 3) {
    // §5.3.3.3: the minimum implementation of a spline is the series of lines
    // joining its points, and the standard reserves the smooth form for future
    // standardization.
    return control;
  }

  const NabtsPoint& start = control[0];
  const NabtsPoint& through = control[1];
  const NabtsPoint& end = control[2];

  if (distance_between(start, end) < kCoincident) {
    // §5.3.3.2.2: "If the end point is omitted, it is taken to be coincident
    // with the start point and a circle is drawn", with the intermediate point
    // diametrically opposite.
    const NabtsPoint centre{(start.x + through.x) / 2.0,
                            (start.y + through.y) / 2.0};
    const double radius = distance_between(start, through) / 2.0;
    if (radius < kCoincident) {
      return {start};
    }
    const double start_angle =
        std::atan2(start.y - centre.y, start.x - centre.x);
    const double step = std::sqrt(8.0 * tolerance_unit / radius);
    int segments =
        step > 0.0 ? static_cast<int>(std::ceil(2.0 * kPi / step)) : 1;
    segments = std::clamp(segments, 3, kMaxArcSegments);

    std::vector<NabtsPoint> out;
    out.reserve(static_cast<size_t>(segments) + 1);
    for (int i = 0; i <= segments; ++i) {
      const double angle =
          start_angle +
          2.0 * kPi * (static_cast<double>(i) / static_cast<double>(segments));
      out.push_back(NabtsPoint{centre.x + radius * std::cos(angle),
                               centre.y + radius * std::sin(angle)});
    }
    return out;
  }

  return arc_segment(start, through, end, tolerance_unit);
}

// ---------------------------------------------------------------------------
// Pel
// ---------------------------------------------------------------------------

void NaplpsRasteriser::stamp_pel_block(int anchor_column, int anchor_row,
                                       const NaplpsCellSize& pel,
                                       const NaplpsInk& ink) {
  const int last_column = anchor_column + pel_span(pel.columns) - 1;
  const int last_row = anchor_row + pel_span(pel.rows) - 1;

  for (int row = anchor_row; row <= last_row; ++row) {
    for (int column = anchor_column; column <= last_column; ++column) {
      if (traced_ != nullptr) {
        if (traced_->contains(column, row)) {
          traced_->cells[traced_->index_of(column, row)] = 1u;
        }
        continue;
      }
      // Off the surface is not an error; it is simply not displayed.
      surface_.deposit(column, row, ink);
    }
  }
}

void NaplpsRasteriser::stamp_pel_cells(const NaplpsCellPoint& where,
                                       const NaplpsCellSize& pel,
                                       const NaplpsInk& ink) {
  stamp_pel_block(pel_anchor(where.column, pel.columns),
                  pel_anchor(where.row, pel.rows), pel, ink);
}

void NaplpsRasteriser::stamp_pel(const NabtsPoint& point, const NabtsSize& pel,
                                 const NaplpsInk& ink) {
  stamp_pel_cells(mapping_.cell_point(point), mapping_.cell_size(pel), ink);
}

// ---------------------------------------------------------------------------
// Stroking
// ---------------------------------------------------------------------------

void NaplpsRasteriser::sweep_pel(const NaplpsCellPoint& from,
                                 const NaplpsCellPoint& to,
                                 const NaplpsCellSize& pel,
                                 const NaplpsInk& ink) {
  // The locus is walked in whole cells, from the cell the pel starts in to the
  // cell it ends in, by the straight line algorithm §5.3.2.2.6 names: one cell
  // per step along whichever axis is moving faster. That draws a line of the
  // pel's own weight whichever way it runs — stepping along both axes instead
  // would put the pel down at every cell the locus so much as grazes, and a
  // rectangular brush grazes more cells running diagonally than running square,
  // so a diagonal would come out half as thick again as a horizontal from a pel
  // the standard sizes without reference to direction at all.
  //
  // Walking it in cells rather than by sampling a continuous position also
  // makes the swept region gap-free by construction, every step touching the
  // last. Sampling could drop a cell wherever the arithmetic of the endpoints
  // rounded the wrong way, and a filled figure works out its own inside from
  // what its outline shuts off from the outside: one dropped cell let the whole
  // fill out.
  const int from_column = pel_anchor(from.column, pel.columns);
  const int from_row = pel_anchor(from.row, pel.rows);
  const int to_column = pel_anchor(to.column, pel.columns);
  const int to_row = pel_anchor(to.row, pel.rows);

  const int columns = std::abs(to_column - from_column);
  const int rows = std::abs(to_row - from_row);
  const int column_step = to_column >= from_column ? 1 : -1;
  const int row_step = to_row >= from_row ? 1 : -1;

  int column = from_column;
  int row = from_row;
  int error = columns - rows;
  for (;;) {
    stamp_pel_block(column, row, pel, ink);
    if (column == to_column && row == to_row) {
      return;
    }
    const int doubled = 2 * error;
    if (doubled > -rows) {
      error -= rows;
      column += column_step;
    }
    if (doubled < columns) {
      error += columns;
      row += row_step;
    }
  }
}

void NaplpsRasteriser::stroke_path(const std::vector<NabtsPoint>& points,
                                   const NabtsSize& pel,
                                   NabtsLineTexture texture,
                                   const NaplpsInk& ink, bool closed) {
  if (points.empty()) {
    return;
  }
  if (points.size() == 1) {
    stamp_pel(points.front(), pel, ink);
    return;
  }

  const NaplpsCellSize pel_size = mapping_.cell_size(pel);
  const TexturePeriod period = texture_period(texture);

  // The texture runs on along the whole path rather than restarting at each
  // vertex, so a polyline dashes as one line does.
  double phase = 0.0;

  const size_t segment_count = closed ? points.size() : points.size() - 1;
  for (size_t i = 0; i < segment_count; ++i) {
    const NaplpsCellPoint a = mapping_.cell_point(points[i]);
    const NaplpsCellPoint b =
        mapping_.cell_point(points[(i + 1) % points.size()]);
    const double run = b.column - a.column;
    const double rise = b.row - a.row;
    const double length = std::hypot(run, rise);

    const double step_length =
        length > 0.0 ? texture_step(run / length, rise / length, pel_size)
                     : 0.0;

    if (texture == NabtsLineTexture::kSolid || !(step_length > 0.0)) {
      // Solid, either because the texture is or because the pel has no extent
      // along this line — §5.3.2.4.2's "if logical pel size dx = 0, all
      // nonvertical lines are solid" and its converse.
      sweep_pel(a, b, pel_size, ink);
      continue;
    }

    // A position along the segment at |steps_along| pels from its start.
    const auto at_phase = [&](double steps_along) {
      const double t =
          std::clamp((steps_along * step_length) / length, 0.0, 1.0);
      return NaplpsCellPoint{a.column + run * t, a.row + rise * t};
    };

    const double entry_phase = phase;
    const double exit_phase = entry_phase + length / step_length;

    const double first_period = std::floor(entry_phase / period.length);
    const double last_period = std::floor(exit_phase / period.length);
    for (double p = first_period; p <= last_period; p += 1.0) {
      for (const TextureRun& on : period.on) {
        const double run_first =
            (std::max)(p * period.length + on.first, entry_phase);
        const double run_last =
            (std::min)(p * period.length + on.last, exit_phase);
        if (run_last < run_first) {
          continue;
        }
        sweep_pel(at_phase(run_first - entry_phase),
                  at_phase(run_last - entry_phase), pel_size, ink);
      }
    }

    // §5.3.2.4.2: "All end points of lines and arcs and all vertices ... must
    // be plotted regardless of the line texture used."
    stamp_pel_cells(a, pel_size, ink);
    stamp_pel_cells(b, pel_size, ink);

    phase = exit_phase;
  }
}

void NaplpsRasteriser::highlight_path(const std::vector<NabtsPoint>& points,
                                      const NabtsSize& pel,
                                      const NaplpsInk& ink, bool closed) {
  // §5.3.2.4.3: solid, "independent of the current line texture", using the
  // current logical pel size.
  stroke_path(points, pel, NabtsLineTexture::kSolid, ink, closed);
}

// ---------------------------------------------------------------------------
// Filling
// ---------------------------------------------------------------------------

namespace {

/**
 * @brief Whether a texture pattern covers a cell (§5.3.2.4.4)
 *
 * The predefined hatches are lines whose width and spacing are the logical
 * pel's, and the programmable masks are bitmaps stepped and repeated at the
 * mask size. Both are registered against the unit screen's origin rather than
 * the figure's, which is what §5.3.2.4.4 and §5.3.2.4.5 require so that
 * "registration of the patterns shall be maintained across figures".
 */
bool pattern_covers(NabtsTexturePattern pattern, const NabtsSize& pel,
                    const NabtsSize& mask_size, const NabtsTextureMask* mask,
                    double unit_x, double unit_y) {
  // A hatch line and the gap beside it are each one pel across, so a position
  // is on a line when it falls in an even-numbered pel-pair band.
  const auto in_hatch_band = [](double coordinate, double pitch) {
    if (!(std::fabs(pitch) > kCoincident)) {
      // §5.3.2.4.4: with no pel there is no pattern to draw, and the fill is
      // solid.
      return true;
    }
    const double band = std::floor(coordinate / std::fabs(pitch));
    return std::fmod(std::fabs(band), 2.0) < 1.0;
  };

  switch (pattern) {
    case NabtsTexturePattern::kSolid:
      return true;
    case NabtsTexturePattern::kVerticalHatch:
      return in_hatch_band(unit_x, pel.dx);
    case NabtsTexturePattern::kHorizontalHatch:
      return in_hatch_band(unit_y, pel.dy);
    case NabtsTexturePattern::kCrossHatch:
      // Both sets of lines are drawn, so a cell on either is covered.
      return in_hatch_band(unit_x, pel.dx) || in_hatch_band(unit_y, pel.dy);
    default:
      break;
  }

  if (mask == nullptr || !mask->defined() || mask->width <= 0 ||
      mask->height <= 0) {
    // An undefined mask leaves solid, which is what an undefined pattern draws.
    return true;
  }

  const double step_x = std::fabs(mask_size.dx);
  const double step_y = std::fabs(mask_size.dy);
  if (!(step_x > kCoincident) || !(step_y > kCoincident)) {
    return true;
  }

  // Step and repeat from the unit screen's origin (§5.3.2.4.5).
  double fraction_x = std::fmod(unit_x, step_x) / step_x;
  double fraction_y = std::fmod(unit_y, step_y) / step_y;
  if (fraction_x < 0.0) {
    fraction_x += 1.0;
  }
  if (fraction_y < 0.0) {
    fraction_y += 1.0;
  }
  // "The sign bits of dx and dy are used to reflect the mask pattern within the
  // mask field" (§5.3.2.4.5).
  if (mask_size.dx < 0.0) {
    fraction_x = 1.0 - fraction_x;
  }
  if (mask_size.dy < 0.0) {
    fraction_y = 1.0 - fraction_y;
  }

  int element_x = static_cast<int>(fraction_x * mask->width);
  int element_y = static_cast<int>(fraction_y * mask->height);
  element_x = std::clamp(element_x, 0, mask->width - 1);
  element_y = std::clamp(element_y, 0, mask->height - 1);

  // Row 0 of a mask buffer is its bottom, matching unit space.
  return mask->elements[static_cast<size_t>(element_y) *
                            static_cast<size_t>(mask->width) +
                        static_cast<size_t>(element_x)] != 0;
}

}  // namespace

void NaplpsRasteriser::fill_path(const std::vector<NabtsPoint>& points,
                                 const NabtsSize& pel,
                                 NabtsTexturePattern pattern,
                                 const NabtsSize& mask_size,
                                 const NabtsTextureMask* mask,
                                 const NaplpsInk& ink) {
  if (points.size() < 2) {
    if (points.size() == 1) {
      stamp_pel(points.front(), pel, ink);
    }
    return;
  }

  // The figure is worked out as a coverage mask first and deposited afterwards.
  // The enclosed area and the outline the pel traces are one filled region
  // (§5.3.3.4.1, §5.3.3.5.1), and the texture pattern applies to the whole of
  // it, so the two cannot be deposited independently without the pattern
  // falling out of registration between them.
  if (surface_.width() <= 0 || surface_.height() <= 0) {
    return;
  }

  // The figure's neighbourhood: its own extent, the pel it is outlined with,
  // and a margin round the lot. The margin is what makes the border of this
  // rectangle lie outside the figure whatever shape the path is, which is what
  // the walk below starts from — and it has to be measured from the path
  // rather than clipped to the display area, or a figure drawn right up to the
  // edge of the screen would have no outside left to start from.
  const NaplpsCellSize pel_size = mapping_.cell_size(pel);
  double left = std::numeric_limits<double>::infinity();
  double right = -std::numeric_limits<double>::infinity();
  double bottom = std::numeric_limits<double>::infinity();
  double top = -std::numeric_limits<double>::infinity();
  for (const NabtsPoint& point : points) {
    const NaplpsCellPoint at = mapping_.cell_point(point);
    left = (std::min)(left, at.column);
    right = (std::max)(right, at.column);
    bottom = (std::min)(bottom, at.row);
    top = (std::max)(top, at.row);
  }
  left += (std::min)(0.0, pel_size.columns) - kFillMarginCells;
  right += (std::max)(0.0, pel_size.columns) + kFillMarginCells;
  bottom += (std::min)(0.0, pel_size.rows) - kFillMarginCells;
  top += (std::max)(0.0, pel_size.rows) + kFillMarginCells;

  // A coordinate far outside the display area draws nothing a receiver shows,
  // so there is no point carrying the grid out to meet it. Beyond a screen's
  // width either way the figure covers everything on show in any case.
  const auto clamp_column = [this](double column) {
    return std::clamp(static_cast<int>(std::floor(column)), -surface_.width(),
                      2 * surface_.width());
  };
  const auto clamp_row = [this](double row) {
    return std::clamp(static_cast<int>(std::floor(row)), -surface_.height(),
                      2 * surface_.height());
  };

  NaplpsTracedRegion region;
  region.first_column = clamp_column(left);
  region.first_row = clamp_row(bottom);
  region.columns = clamp_column(std::ceil(right)) - region.first_column + 1;
  region.rows = clamp_row(std::ceil(top)) - region.first_row + 1;
  if (region.columns <= 0 || region.rows <= 0) {
    return;
  }
  region.cells.assign(
      static_cast<size_t>(region.columns) * static_cast<size_t>(region.rows),
      0u);

  // The outline the pel traces, which §5.3.3.4.1 and §5.3.3.5.1 put inside the
  // filled area rather than over it — so it is swept solid, whatever the line
  // texture. It is worked out by running the routine that *strokes* an outline
  // and noting the cells it touched: that region and a stroked outline are the
  // same thing, and a service that fills a figure and then outlines it depends
  // on their agreeing cell for cell.
  traced_ = &region;
  stroke_path(points, pel, NabtsLineTexture::kSolid, ink, /*closed=*/true);
  traced_ = nullptr;

  // Everything the outline does not shut off from the outside, found by walking
  // inward from the region's border. The outline blocks the walk, and a swept
  // pel is a closed curve joined corner to corner, which is exactly what it
  // takes to stop a walk that only moves along edges.
  //
  // Deciding the figure this way, rather than from where the path's own
  // crossings fall, is what settles the fill against the outline. The two come
  // from the one swept pel: a fill cannot show past the outline, because it
  // stops where the outline starts, and it cannot fall short of it either,
  // because everything up to it is shut off from the outside. Worked out apart,
  // they disagreed by a cell all along every diagonal — the fill showing past
  // the line drawn over it in one direction, and leaving a seam down the corner
  // in the other.
  const int last_column = region.first_column + region.columns - 1;
  const int last_row = region.first_row + region.rows - 1;

  // Which side of the path each cell is on, by the path's own crossings at the
  // row's centre. This is what says a figure has a hole in it: §5.3.3.5.1's
  // enclosed area is the odd-even one, so a frame drawn as one path — round the
  // outside, in through a slit, and round the inside — is a frame and not a
  // slab. The outline alone cannot tell the two apart, the slit being closed to
  // a walk.
  std::vector<uint8_t> enclosed(region.cells.size(), 0u);
  std::vector<double> crossings;
  for (int row = region.first_row; row <= last_row; ++row) {
    const double sample = row + 0.5;
    crossings.clear();
    for (size_t i = 0; i < points.size(); ++i) {
      const NaplpsCellPoint from = mapping_.cell_point(points[i]);
      const NaplpsCellPoint to =
          mapping_.cell_point(points[(i + 1) % points.size()]);
      // A half-open test in y counts a vertex once rather than twice, which is
      // what keeps a shared vertex from toggling the span twice and punching a
      // hole in the fill.
      if ((from.row <= sample && to.row > sample) ||
          (to.row <= sample && from.row > sample)) {
        crossings.push_back(from.column + (sample - from.row) /
                                              (to.row - from.row) *
                                              (to.column - from.column));
      }
    }
    std::sort(crossings.begin(), crossings.end());
    for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
      const int from_column = (std::max)(
          region.first_column, static_cast<int>(std::ceil(crossings[i] - 0.5)));
      const int to_column = (std::min)(
          last_column, static_cast<int>(std::floor(crossings[i + 1] - 0.5)));
      for (int column = from_column; column <= to_column; ++column) {
        enclosed[region.index_of(column, row)] = 1u;
      }
    }
  }

  std::vector<uint8_t> outside(region.cells.size(), 0u);
  std::vector<size_t> walk;
  const auto step_outward = [&](int column, int row) {
    if (!region.contains(column, row)) {
      return;
    }
    const size_t index = region.index_of(column, row);
    if (outside[index] != 0u || region.cells[index] != 0u) {
      return;
    }
    outside[index] = 1u;
    walk.push_back(index);
  };

  // The walk starts from every cell the path leaves outside that is also clear
  // of the outline — a cell of the outline's own neighbourhood is not a safe
  // place to start, since that is exactly where the two ways of putting a
  // boundary on the grid part company by a cell. Anywhere further out they
  // agree, so a hole in the figure is found from the middle of the hole and the
  // outside from the outside, while the seam along a diagonal — outside by the
  // crossings, inside the outline that was drawn — is left for the walk to fail
  // to reach.
  const auto beside_outline = [&](int column, int row) {
    for (int down = -1; down <= 1; ++down) {
      for (int across = -1; across <= 1; ++across) {
        const int at_column = column + across;
        const int at_row = row + down;
        if (region.contains(at_column, at_row) &&
            region.cells[region.index_of(at_column, at_row)] != 0u) {
          return true;
        }
      }
    }
    return false;
  };
  for (int row = region.first_row; row <= last_row; ++row) {
    for (int column = region.first_column; column <= last_column; ++column) {
      const size_t index = region.index_of(column, row);
      if (enclosed[index] == 0u && region.cells[index] == 0u &&
          !beside_outline(column, row)) {
        step_outward(column, row);
      }
    }
  }
  while (!walk.empty()) {
    const size_t index = walk.back();
    walk.pop_back();
    const int column =
        region.first_column +
        static_cast<int>(index % static_cast<size_t>(region.columns));
    const int row =
        region.first_row +
        static_cast<int>(index / static_cast<size_t>(region.columns));
    step_outward(column - 1, row);
    step_outward(column + 1, row);
    step_outward(column, row - 1);
    step_outward(column, row + 1);
  }

  // Only what the receiver shows is deposited.
  const int first_shown_column = (std::max)(region.first_column, 0);
  const int last_shown_column = (std::min)(last_column, surface_.width() - 1);
  const int first_shown_row = (std::max)(region.first_row, 0);
  const int last_shown_row = (std::min)(last_row, surface_.height() - 1);
  for (int row = first_shown_row; row <= last_shown_row; ++row) {
    for (int column = first_shown_column; column <= last_shown_column;
         ++column) {
      if (outside[region.index_of(column, row)] != 0u) {
        continue;
      }
      if (!pattern_covers(pattern, pel, mask_size, mask,
                          mapping_.centre_x(column), mapping_.centre_y(row))) {
        continue;
      }
      surface_.deposit(column, row, ink);
    }
  }
}

std::vector<NabtsPoint> NaplpsRasteriser::arc_polyline(
    const std::vector<NabtsPoint>& control) const {
  // A fraction of a cell, so the polyline standing in for the curve cannot
  // change which cells get painted.
  return naplps_arc_polyline(control,
                             kArcToleranceCells / mapping_.columns_per_unit);
}

// ---------------------------------------------------------------------------
// Characters, mosaics and downloadable glyphs
// ---------------------------------------------------------------------------

void NaplpsRasteriser::fill_cell_rect(double left, double bottom, double width,
                                      double height, const NaplpsInk& ink) {
  if (width <= 0.0 || height <= 0.0) {
    return;
  }
  int first_column = 0;
  int last_column = 0;
  int first_row = 0;
  int last_row = 0;
  cells_overlapping(left, left + width, first_column, last_column);
  cells_overlapping(bottom, bottom + height, first_row, last_row);
  for (int row = first_row; row <= last_row; ++row) {
    for (int column = first_column; column <= last_column; ++column) {
      surface_.deposit(column, row, ink);
    }
  }
}

bool NaplpsRasteriser::field_cells(const NabtsPrimitive& primitive,
                                   int& columns, int& rows) const {
  // The field runs from its origin by the character field size, either of which
  // may be signed — §5.3.2.3.9 uses the sign to reflect the pattern.
  const NaplpsCellSize field = mapping_.cell_size(primitive.size);
  if (std::fabs(field.columns) < kCoincident ||
      std::fabs(field.rows) < kCoincident) {
    return false;
  }
  columns =
      (std::max)(1, static_cast<int>(std::lround(std::fabs(field.columns))));
  rows = (std::max)(1, static_cast<int>(std::lround(std::fabs(field.rows))));
  return true;
}

void NaplpsRasteriser::deposit_pattern(const NabtsPrimitive& primitive,
                                       const uint16_t* pattern_rows,
                                       int pattern_width, int pattern_height,
                                       const NaplpsInk& ink,
                                       const NaplpsInk* background) {
  if (pattern_rows == nullptr || pattern_width <= 0 || pattern_height <= 0) {
    return;
  }

  int columns = 0;
  int rows = 0;
  if (!field_cells(primitive, columns, rows)) {
    return;
  }

  const NaplpsCellPoint origin = mapping_.cell_point(primitive.origin);

  for (int row = 0; row < rows; ++row) {
    for (int column = 0; column < columns; ++column) {
      // Nearest-neighbour from the field back into the pattern: a receiver
      // deposits the pattern it stored, so a larger field shows the same shape
      // over more pixels rather than a smoother one.
      int source_column = pattern_width * column / columns;
      int source_row = pattern_height * row / rows;
      source_column = std::clamp(source_column, 0, pattern_width - 1);
      source_row = std::clamp(source_row, 0, pattern_height - 1);

      // The pattern's rows run top down while the field's run up.
      const uint16_t bits = pattern_rows[source_row];
      const bool lit =
          (bits & (1u << (pattern_width - 1 - source_column))) != 0;

      // §6.2.7.4: reverse video fills the field and leaves the shape undrawn.
      const NaplpsInk* pen = nullptr;
      if (primitive.reverse_video) {
        pen = lit ? background : &ink;
      } else {
        pen = lit ? &ink : background;
      }
      if (pen == nullptr) {
        continue;  // nothing to deposit: the ground shows through
      }

      // §5.3.2.3.1: rotation turns the field counter-clockwise about its
      // origin, taking the character with it.
      int placed_column = column;
      int placed_row = rows - 1 - row;
      switch (primitive.rotation) {
        case NabtsCharRotation::kNone:
          break;
        case NabtsCharRotation::k90: {
          const int rotated_column = -(placed_row);
          const int rotated_row = placed_column;
          placed_column = rotated_column + rows - 1;
          placed_row = rotated_row;
          break;
        }
        case NabtsCharRotation::k180:
          placed_column = columns - 1 - placed_column;
          placed_row = rows - 1 - placed_row;
          break;
        case NabtsCharRotation::k270: {
          const int rotated_column = placed_row;
          const int rotated_row = columns - 1 - placed_column;
          placed_column = rotated_column;
          placed_row = rotated_row;
          break;
        }
      }

      // A negative field dimension reflects the pattern within the field, the
      // way §5.3.2.4.5 reflects a texture mask.
      const double column_step = primitive.size.dx < 0.0 ? -1.0 : 1.0;
      const double row_step = primitive.size.dy < 0.0 ? -1.0 : 1.0;

      const int surface_column = static_cast<int>(
          std::floor(origin.column + column_step * placed_column +
                     (column_step < 0.0 ? -1.0 : 0.0)));
      const int surface_row = static_cast<int>(std::floor(
          origin.row + row_step * placed_row + (row_step < 0.0 ? -1.0 : 0.0)));
      surface_.deposit(surface_column, surface_row, *pen);
    }
  }
}

void NaplpsRasteriser::deposit_character(
    const NabtsPrimitive& primitive,
    const std::vector<NabtsDrcsCharacter>& drcs, const NaplpsInk& ink,
    const NaplpsInk* background) {
  switch (primitive.repertoire) {
    case NabtsPrimitive::Repertoire::kMosaic: {
      // §5.4: a code position the standard does not assign "shall be displayed
      // as SPACE", which is a mosaic with nothing lit.
      const uint8_t sixels = nabts_is_mosaic_code(primitive.character)
                                 ? nabts_mosaic_sixels(primitive.character)
                                 : 0;

      // The cell in cell space, either dimension of which may be signed.
      const NaplpsCellSize field = mapping_.cell_size(primitive.size);
      const NaplpsCellPoint origin = mapping_.cell_point(primitive.origin);
      const double left =
          (std::min)(origin.column, origin.column + field.columns);
      const double bottom = (std::min)(origin.row, origin.row + field.rows);
      const double width = std::fabs(field.columns);
      const double height = std::fabs(field.rows);
      if (width < kCoincident || height < kCoincident) {
        return;
      }

      // §5.3.2.2.6 lists separated mosaics among the things the logical pel
      // sizes: a separated element is shrunk by the pel, which is what opens
      // the gap between blocks that would otherwise touch.
      const bool separated = primitive.underlined;
      const NaplpsCellSize pel = mapping_.cell_size(primitive.logical_pel);
      const double inset_columns = separated ? std::fabs(pel.columns) : 0.0;
      const double inset_rows = separated ? std::fabs(pel.rows) : 0.0;

      const double element_width = width / 2.0;
      const double element_height = height / 3.0;

      if (background != nullptr) {
        // The field behind the blocks, where the colour mode gives it one.
        fill_cell_rect(left, bottom, width, height, *background);
      }
      for (int element = 0; element < 6; ++element) {
        if ((sixels & (1u << element)) == 0) {
          continue;
        }
        // Bit 0 is top-left and bit 5 bottom-right, so the row counts down from
        // the top while cell rows count up from the bottom.
        const int column = element % 2;
        const int row_from_top = element / 2;
        const double element_left = left + column * element_width;
        const double element_bottom =
            bottom + (2 - row_from_top) * element_height;
        // Left- and bottom-justified within its area, matching what a renderer
        // drawing the same page as vectors does.
        const double drawn_width = (std::max)(0.0, element_width - inset_columns);
        const double drawn_height = (std::max)(0.0, element_height - inset_rows);
        fill_cell_rect(element_left, element_bottom, drawn_width, drawn_height,
                       primitive.reverse_video && background != nullptr
                           ? *background
                           : ink);
      }
      return;
    }

    case NabtsPrimitive::Repertoire::kDrcs: {
      for (const NabtsDrcsCharacter& glyph : drcs) {
        if (glyph.code != primitive.character || !glyph.defined()) {
          continue;
        }
        // A DRCS buffer holds one element per bool with row 0 at its bottom;
        // the row form wants packed bits from the top.
        std::vector<uint16_t> rows(static_cast<size_t>(glyph.height), 0u);
        for (int row = 0; row < glyph.height; ++row) {
          uint16_t bits = 0;
          for (int column = 0; column < glyph.width && column < 8; ++column) {
            if (glyph.elements[static_cast<size_t>(row) * glyph.width +
                               static_cast<size_t>(column)]) {
              bits |= static_cast<uint16_t>(1u << (glyph.width - 1 - column));
            }
          }
          rows[static_cast<size_t>(glyph.height - 1 - row)] = bits;
        }
        deposit_pattern(primitive, rows.data(), std::min<int>(glyph.width, 8),
                        glyph.height, ink, background);
        return;
      }
      // §5.6: a character never defined is displayed as SPACE.
      return;
    }

    case NabtsPrimitive::Repertoire::kPrimary:
    case NabtsPrimitive::Repertoire::kSupplementary: {
      const char32_t code =
          primitive.repertoire == NabtsPrimitive::Repertoire::kPrimary
              ? nabts_primary_to_unicode(primitive.character)
              : nabts_supplementary_to_unicode(primitive.character);
      // §5.1 leaves the pattern to the receiver, so which face it is drawn
      // from is settled by the receiver's grid and the field it has to fill.
      int columns = 0;
      int rows = 0;
      if (!field_cells(primitive, columns, rows)) {
        return;
      }
      const NaplpsFontFace& face =
          naplps_font_face_for_field(mapping_.grid, columns, rows);
      const uint16_t* pattern = naplps_face_pattern(face, code);
      if (pattern == nullptr) {
        return;
      }
      deposit_pattern(primitive, pattern, face.width, face.height, ink,
                      background);
      return;
    }
  }
}

// ---------------------------------------------------------------------------
// Incremental colour runs
// ---------------------------------------------------------------------------

void NaplpsRasteriser::deposit_colour_run(
    const NabtsPoint& field_origin, const NabtsSize& field_size,
    const NabtsSize& pel, const std::vector<NaplpsInk>& colours) {
  if (colours.empty()) {
    return;
  }

  // The field's origin may be any of its corners, since either dimension may be
  // signed (§5.3.3.6.2 FIELD).
  const double left = (std::min)(field_origin.x, field_origin.x + field_size.dx);
  const double bottom =
      (std::min)(field_origin.y, field_origin.y + field_size.dy);
  const double width_unit = std::fabs(field_size.dx);
  const double height_unit = std::fabs(field_size.dy);
  const double top = bottom + height_unit;

  // The pel is the raster cell. Where it is dimensionless there is still a
  // pixel to fill — §5.3.2.2.6's at-least-one — so the step falls back to a
  // single cell of the grid.
  const double step_x = std::fabs(pel.dx) > kCoincident
                            ? std::fabs(pel.dx)
                            : 1.0 / mapping_.columns_per_unit;
  const double step_y = std::fabs(pel.dy) > kCoincident
                            ? std::fabs(pel.dy)
                            : 1.0 / mapping_.rows_per_unit;

  const int columns =
      (std::max)(1, static_cast<int>(std::floor(width_unit / step_x)));

  const NabtsSize cell_pel{step_x, step_y};
  for (size_t i = 0; i < colours.size(); ++i) {
    const int column = static_cast<int>(i % static_cast<size_t>(columns));
    const int row = static_cast<int>(i / static_cast<size_t>(columns));
    // The raster runs left to right and top down, and the pel is anchored at
    // its lower left, so each row sits one pel below the last.
    const NabtsPoint where{left + column * step_x,
                           top - static_cast<double>(row + 1) * step_y};
    if (where.y + step_y <= bottom) {
      break;  // past the bottom of the field: nothing left to show
    }
    stamp_pel(where, cell_pel, colours[i]);
  }
}

}  // namespace tbc::vbi
