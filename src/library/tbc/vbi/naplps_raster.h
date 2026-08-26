/*
 * File:        naplps_raster.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Deposits NAPLPS drawing primitives into a receiver's pixel grid
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/nabts_sink/naplps_raster.h) at tag v2.7.2 (commit
 * fef0115a). Algorithmic bodies are intact; orc:: -> tbc::vbi:: and the
 * vbi-services/nabts_page.h include replaced by nabts_page.h.
 */

#ifndef TBC_VBI_NAPLPS_RASTER_H
#define TBC_VBI_NAPLPS_RASTER_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nabts_page.h"
#include "naplps_render_grid.h"

namespace tbc::vbi {

/**
 * @brief One physical pixel of the receiver's display
 *
 * X3.110 §4.2.3 builds a picture in layers — "if a subsequent drawing command
 * or alphabetic character affects a given pixel of the display, it supersedes
 * the effect of any previous command on that pixel" — so a cell holds only what
 * was deposited last, not a history.
 *
 * The colour map address is carried alongside the resolved colour because a
 * blink process is defined over a map *entry* (§5.3.2.7.2), so what alternates
 * is every pixel that entry coloured, wherever it was drawn from.
 */
struct NaplpsCell {
  /// Whether anything has been deposited here. An untouched cell is not black;
  /// it is nothing, and a renderer leaves the ground showing through it.
  bool painted = false;

  NabtsColour colour;
  int16_t colour_map_address = -1;

  bool blinking = false;
  NabtsColour blink_to;
  int16_t blink_to_map_address = -1;
};

/// What one drawing operation deposits, which is everything a cell records
/// apart from the fact of having been painted at all.
struct NaplpsInk {
  NabtsColour colour;
  int16_t colour_map_address = -1;
  bool blinking = false;
  NabtsColour blink_to;
  int16_t blink_to_map_address = -1;
};

/// The ink an operation draws in, taken from the attributes the interpreter
/// resolved onto it.
NaplpsInk naplps_ink_of(const NabtsPrimitive& primitive);

/// The ink an operation's *background* fills in, for the colour mode that has
/// one (§5.3.2.6 mode 2).
NaplpsInk naplps_background_ink_of(const NabtsPrimitive& primitive);

/**
 * @brief The receiver's frame buffer, one cell per physical pixel
 *
 * Row 0 is the bottom, matching the unit screen's y and the element order of
 * the DRCS and texture-mask buffers, so nothing in the decoder has to think
 * about which way up a buffer is.
 *
 * The surface covers the display area alone — the lower @ref
 * kNabtsDisplayAreaHeight of the unit screen, which Table D1 item 10 is the
 * part a receiver guarantees is visible. A coordinate outside it is not an
 * error; it is simply not displayed, and is dropped.
 */
class NaplpsCellSurface {
 public:
  explicit NaplpsCellSurface(NaplpsRenderGrid grid);

  const NaplpsRenderGrid& grid() const { return grid_; }
  int width() const { return grid_.width; }
  int height() const { return grid_.height; }

  bool contains(int column, int row) const {
    return column >= 0 && column < grid_.width && row >= 0 &&
           row < grid_.height;
  }

  /// The cell at |column|, |row|, or an unpainted one for a position off the
  /// surface, so a reader never has to bounds-check before looking.
  const NaplpsCell& at(int column, int row) const;

  /// Lay |ink| into one cell, superseding whatever was there (§4.2.3). A
  /// position off the surface is dropped.
  void deposit(int column, int row, const NaplpsInk& ink);

  /// Cells that have been painted, which is what tells an empty page from a
  /// black one.
  size_t painted_count() const;

 private:
  NaplpsRenderGrid grid_;
  std::vector<NaplpsCell> cells_;
  NaplpsCell blank_;
};

/// A position in continuous cell space.
struct NaplpsCellPoint {
  double column = 0.0;
  double row = 0.0;
};

/// An extent in continuous cell space, which may be signed: the sign of a
/// NAPLPS size says which way it runs from its origin.
struct NaplpsCellSize {
  double columns = 0.0;
  double rows = 0.0;
};

/**
 * @brief Maps the unit screen onto a surface's cells
 *
 * Unit x runs 0 to 1 across the grid's width and unit y runs 0 to
 * @ref kNabtsDisplayAreaHeight up its height, so a cell is square in unit space
 * (see naplps_render_grid.h). Positions are continuous: a coordinate lands
 * partway through a cell, and which cells that covers is the drawing rule's
 * business rather than the mapping's.
 */
struct NaplpsGridMapping {
  NaplpsRenderGrid grid;
  /// Cells per unit of x, and per unit of y.
  double columns_per_unit = 0.0;
  double rows_per_unit = 0.0;

  /// A display: unit x over the grid's width, unit y over the display area.
  explicit NaplpsGridMapping(NaplpsRenderGrid target)
      : grid(target),
        columns_per_unit(target.width),
        rows_per_unit(1.0 / target.pitch_y()) {}

  /**
   * @brief A buffer covering the whole unit screen on both axes
   *
   * What a DRCS character or a texture mask is defined into: §6.2.3 and §6.2.4
   * execute the defining code into a storage buffer rather than onto the
   * display, and the buffer stands for the whole drawing space rather than for
   * the part of it a receiver shows.
   */
  static NaplpsGridMapping over_unit_screen(NaplpsRenderGrid target) {
    NaplpsGridMapping out(target);
    out.columns_per_unit = target.width;
    out.rows_per_unit = target.height;
    return out;
  }

  /// Continuous cell-space x of a unit-screen x.
  double column_at(double x) const { return x * columns_per_unit; }
  /// Continuous cell-space y of a unit-screen y, counting up from the bottom.
  double row_at(double y) const { return y * rows_per_unit; }

  /// A unit-space width as a count of cells.
  double columns_across(double dx) const { return dx * columns_per_unit; }
  /// A unit-space height as a count of cells.
  double rows_up(double dy) const { return dy * rows_per_unit; }

  /// A unit-screen point and a unit-screen size in cell space, which is the
  /// form every drawing rule works in.
  NaplpsCellPoint cell_point(const NabtsPoint& point) const {
    return NaplpsCellPoint{column_at(point.x), row_at(point.y)};
  }
  NaplpsCellSize cell_size(const NabtsSize& size) const {
    return NaplpsCellSize{columns_across(size.dx), rows_up(size.dy)};
  }

  /// The unit-screen x and y of the centre of a cell, which is what a pattern
  /// anchored to the unit screen is sampled at.
  double centre_x(int column) const {
    return (column + 0.5) / columns_per_unit;
  }
  double centre_y(int row) const { return (row + 0.5) / rows_per_unit; }
};

/**
 * @brief An ARC's control points as a polyline that approximates it
 *
 * §5.3.3.3 reads a control-point run three ways. Three points are a circle when
 * the start and end coincide — the intermediate point being diametrically
 * opposite — a straight line when all three are colinear, and otherwise the
 * segment of the circle through all three. More than three points are a
 * curvilinear spline whose "minimum implementation shall be a series of lines
 * connecting the start, intermediate, and end points", which is what this
 * returns for them.
 *
 * @p tolerance_unit is the greatest distance the polyline may stray from the
 * true curve, in unit-screen terms; a caller passes a fraction of a cell so the
 * approximation cannot be seen on the grid it is drawn into.
 */
std::vector<NabtsPoint> naplps_arc_polyline(
    const std::vector<NabtsPoint>& control, double tolerance_unit);

/**
 * @brief One run of identical cells along a row of the surface
 *
 * A rasterised page is emitted as runs rather than as a bitmap so that a
 * renderer can put a receiver's rectangular pixels on screen at any size and
 * keep their edges sharp. @ref columns is how many cells the run covers,
 * starting at @ref column on @ref row.
 */
struct NaplpsCellRun {
  int column = 0;
  int row = 0;
  int columns = 0;
  NaplpsCell cell;
};

/**
 * @brief The painted cells of @p surface as run-length merged runs
 *
 * Merged along a row only. Merging rectangles as well would cut the count
 * further on a page of flat colour, but a run per row keeps the emitted
 * geometry a plain function of the surface, which is what makes it testable
 * against the cells rather than against itself.
 *
 * Unpainted cells produce no run at all: a page does not cover the screen, and
 * emitting its ground would paint over whatever a renderer puts behind it.
 */
std::vector<NaplpsCellRun> naplps_merge_runs(const NaplpsCellSurface& surface);

/**
 * @brief The cells a filled figure is worked out in, and its traced outline
 *
 * A rectangle of cell space with a bit per cell. It covers the figure's own
 * path and a margin round it, wherever that falls — including cells the
 * receiver does not show.
 *
 * Off-surface cells have to count. A figure may run past the edge of the
 * display area, and an outline clipped off at that edge would be no barrier at
 * all: the walk that decides which cells lie outside a figure would step
 * through the gap and claim the whole figure. Keeping the margin outside the
 * path is also what makes the walk's starting ring outside the figure by
 * construction, whatever shape the path is. Only the depositing at the end is
 * clipped to what the receiver shows.
 */
struct NaplpsTracedRegion {
  int first_column = 0;
  int first_row = 0;
  int columns = 0;
  int rows = 0;
  std::vector<uint8_t> cells;

  bool contains(int column, int row) const {
    return column >= first_column && column < first_column + columns &&
           row >= first_row && row < first_row + rows;
  }
  size_t index_of(int column, int row) const {
    return static_cast<size_t>(row - first_row) * static_cast<size_t>(columns) +
           static_cast<size_t>(column - first_column);
  }
};

/**
 * @brief Draws NAPLPS primitives into a cell surface
 *
 * Every geometric primitive of §5.3.3 comes down to two operations on the grid:
 * sweeping the logical pel along a path, and filling an enclosed area. This
 * carries out both against the rules §5.3.2.2.6, §5.3.2.4.2 and §5.3.2.4.4
 * state in terms of the receiver's physical pixels — which is the whole reason
 * the grid has to be known before anything can be drawn.
 */
class NaplpsRasteriser {
 public:
  NaplpsRasteriser(NaplpsCellSurface& surface, NaplpsGridMapping mapping)
      : surface_(surface), mapping_(mapping) {}

  const NaplpsGridMapping& mapping() const { return mapping_; }

  /**
   * @brief Lay the logical pel down once, with the drawing point at |point|
   *
   * §5.3.2.2.6: the deposit covers "all of those pixels that lie under any
   * portion of the logical pel as it is mapped to the display screen", and
   * "the logical pel, therefore, will always map to at least one and possibly
   * many display pixels" — so a dimensionless pel still paints the cell the
   * drawing point falls in.
   *
   * The pel's sign decides which of its corners the drawing point is: lower
   * left when both dimensions are positive, and the other three corners for the
   * other three sign combinations, which is what taking the rectangle between
   * the point and the point plus the pel gives.
   *
   * How many pixels that is comes from the pel's size alone, so a pel of a
   * given size is the same brush wherever it is put down — see @ref
   * NaplpsRasteriser::stamp_pel_cells.
   */
  void stamp_pel(const NabtsPoint& point, const NabtsSize& pel,
                 const NaplpsInk& ink);

  /**
   * @brief Sweep the pel along a path, in the given line texture
   *
   * "A LINE is a locus of points following a straight line algorithm between
   * two specified coordinates. The physical picture elements (pixels) through
   * which the infinitely small locus point passes would be drawn. The logical
   * pel specification allows the locus point to take on specific dimensions,
   * thereby acting as a larger brush" (§5.3.2.2.6). The locus is walked at one
   * cell per step along whichever axis is moving faster, which is the straight
   * line algorithm that leaves no gap and draws a line of the pel's weight
   * whichever way it runs.
   *
   * |closed| joins the last point back to the first, which is what an outlined
   * rectangle or polygon needs.
   */
  void stroke_path(const std::vector<NabtsPoint>& points, const NabtsSize& pel,
                   NabtsLineTexture texture, const NaplpsInk& ink,
                   bool closed = false);

  /**
   * @brief Fill the area a path encloses, plus the outline the pel traces
   *
   * §5.3.3.4.1, §5.3.3.5.1 and §5.3.3.3 all put "the region of the outline
   * traced by the logical pel" inside the filled area, so a filled figure is
   * its enclosed area *plus* a solid sweep of the pel around its outline. A
   * service that draws a letterform as a path enclosing almost nothing and lets
   * the pel give it weight depends on this.
   *
   * The outline is therefore drawn first, by @ref stroke_path itself, and the
   * figure is everything that outline shuts off from the outside. Taking the
   * inside from the outline rather than from the path is what settles a fill
   * against the line a service draws over it: the fill stops exactly where the
   * outline starts, and reaches all the way to it. A service that draws a map
   * by filling each region and then outlining it depends on both — a fill that
   * overshot showed a coloured fringe outside the coastline, and one that fell
   * short left a seam down every diagonal.
   *
   * |pattern| is applied through the texture rules of §5.3.2.4.4: a cell is
   * filled only where the pattern covers it, with the pattern registered
   * against the unit screen's origin so it lines up across figures.
   */
  void fill_path(const std::vector<NabtsPoint>& points, const NabtsSize& pel,
                 NabtsTexturePattern pattern, const NabtsSize& mask_size,
                 const NabtsTextureMask* mask, const NaplpsInk& ink);

  /**
   * @brief Outline a filled figure in the highlight colour
   *
   * §5.3.2.4.3: the outline of a highlighted figure is drawn "with solid line
   * texture (independent of the current line texture) using the current logical
   * pel size", in nominal black in colour modes 0 and 1 and in the background
   * colour in mode 2.
   */
  void highlight_path(const std::vector<NabtsPoint>& points,
                      const NabtsSize& pel, const NaplpsInk& ink,
                      bool closed = true);

  /**
   * @brief Deposit a raster of colours across a field, one pel apiece
   *
   * §5.3.3.6.3 INCREMENTAL POINT: the colours are laid raster-sequentially
   * across the active field, each covering one logical pel, wrapping at the
   * field's edge. The pel is the raster cell here, so this is the primitive the
   * receiver's resolution shows in most directly.
   */
  void deposit_colour_run(const NabtsPoint& field_origin,
                          const NabtsSize& field_size, const NabtsSize& pel,
                          const std::vector<NaplpsInk>& colours);

  /// @ref naplps_arc_polyline at a tolerance fine enough for this grid, which
  /// is what a caller drawing into it wants and saves it working out.
  std::vector<NabtsPoint> arc_polyline(
      const std::vector<NabtsPoint>& control) const;

  /**
   * @brief Deposit one character's pattern into its character field
   *
   * A receiver draws a character by depositing its stored pattern, so this
   * scales the pattern to the field rather than to the grid: a double-size
   * field gets the same pattern over twice as many pixels, which is what
   * §5.3.2.3.9's continuously variable character size means on a fixed font.
   *
   * §5.3.2.3.1's rotation turns the field about its origin, and §6.2.7.4's
   * reverse video fills the field and leaves the character shape undrawn —
   * @p background is what the shape is left in, where the colour mode has one.
   *
   * @p pattern is indexed by row from the *top*, each row's bit
   * (@p pattern_width - 1 - column) being its leftmost pixel; @p pattern_width
   * and @p pattern_height give its extent. This takes a pattern rather than a
   * character so that the coded sets, DRCS (§5.6) and mosaics (§5.4) can all
   * come through one path.
   */
  void deposit_pattern(const NabtsPrimitive& primitive,
                       const uint16_t* pattern_rows, int pattern_width,
                       int pattern_height, const NaplpsInk& ink,
                       const NaplpsInk* background);

  /**
   * @brief The character field of @p primitive, in whole cells
   *
   * Empty where either dimension rounds away, which is a field with nothing to
   * draw in it.
   */
  bool field_cells(const NabtsPrimitive& primitive, int& columns,
                   int& rows) const;

  /// Deposit the character @p primitive names, looking its pattern up in the
  /// font and in @p drcs for a downloadable one. Draws nothing where neither
  /// holds it.
  void deposit_character(const NabtsPrimitive& primitive,
                         const std::vector<NabtsDrcsCharacter>& drcs,
                         const NaplpsInk& ink, const NaplpsInk* background);

 private:
  /**
   * @brief Lay the pel down with its drawing point at |where|
   *
   * Never fewer than one cell — §5.3.2.2.6's at-least-one-pixel — and never
   * more than the pel's own size, rounded to whole cells. Only *where* it is
   * laid comes from the position. The pel is a brush the standard sizes without
   * reference to where it is or which way it is being swept, so one that grew a
   * pixel whenever it happened to straddle a pixel boundary would draw a line
   * whose weight varied along its own length and with its own direction.
   */
  void stamp_pel_cells(const NaplpsCellPoint& where, const NaplpsCellSize& pel,
                       const NaplpsInk& ink);

  /// The same, laid from the cell the pel's low corner is in, which is the form
  /// a sweep works in: it walks whole cells, so it has no continuous position
  /// to hand back.
  void stamp_pel_block(int anchor_column, int anchor_row,
                       const NaplpsCellSize& pel, const NaplpsInk& ink);

  /// Fill a cell-space rectangle, covering every cell any part of it touches.
  /// An area, rather than a brush: it is sized by what it covers.
  void fill_cell_rect(double left, double bottom, double width, double height,
                      const NaplpsInk& ink);

  /// Sweep the pel from one cell-space position to another, stamping it along
  /// the way so the locus has no gap.
  void sweep_pel(const NaplpsCellPoint& from, const NaplpsCellPoint& to,
                 const NaplpsCellSize& pel, const NaplpsInk& ink);

  NaplpsCellSurface& surface_;
  NaplpsGridMapping mapping_;

  /// While set, a stamped pel records the cells it covers here instead of
  /// depositing into the surface.
  ///
  /// This is how a filled figure works out the outline the pel traces round it:
  /// by running the routine that *strokes* an outline and noting what it
  /// touched. §5.3.3.5.1 puts that traced region inside the fill and
  /// §5.3.2.2.6 defines it once for every drawing operation alike, so the two
  /// are the same region and computing them separately only invites them to
  /// disagree — as they did, leaving a fill showing past the outline a service
  /// drew over it.
  NaplpsTracedRegion* traced_ = nullptr;
};

}  // namespace tbc::vbi

#endif  // TBC_VBI_NAPLPS_RASTER_H
