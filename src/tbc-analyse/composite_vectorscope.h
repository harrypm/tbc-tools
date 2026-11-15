/*
 * File:        composite_vectorscope.h
 * Module:      tbc-analyse
 * Purpose:     Chroma demodulation from the composite carrier for the
 *              technical (measurement) vectorscope
 *
 * Ported from decode-orc
 * (orc/core/analysis/vectorscope/composite_vectorscope.h) with TBC-domain
 * adaptations: the input is a flat field-sequential CVBS_U10_4FSC buffer
 * assembled from a ld-decode .tbc frame (uniform line width, per-field line
 * counts) rather than decode-orc's CVBS-container frame.  All code lives in
 * tbc-tools; decode-orc is only read from.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TBC_ANALYSE_COMPOSITE_VECTORSCOPE_H
#define TBC_ANALYSE_COMPOSITE_VECTORSCOPE_H

#include "cvbs_signal_constants.h"
#include "tbcmetadata.h"
#include "vectorscope_data.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace tbc::cvbs {

// ============================================================================
// Composite (measurement) vectorscope acquisition
// ============================================================================
// A technical vectorscope monitors the composite signal, not a decoded
// picture.  It product-detects chroma against a subcarrier reference recovered
// from the colour burst and plots the result with none of the processing a
// decoder applies to make a good picture:
//
//   * no delay-line / comb averaging between lines;
//   * no PAL V-switch correction, so +V and -V lines plot as two sets of
//     targets mirrored about the U axis (ITU-R BT.470-6 Table 2 item 2.16);
//   * no active-area restriction, so the burst on the back porch is in the
//     data set alongside the picture.
//
// The subcarrier reference is measured from the burst rather than assumed from
// the nominal subcarrier-cycles-per-line, which is what an instrument's
// burst-locked oscillator does.  A local straight-line fit over a few tens of
// lines acts as its flywheel: any drift in the line-start phase is tracked out
// - a TBC whose lines sit on a plain integer-sample grid drifts about half a
// degree per line, enough to smear a frame of colour bars into arcs - while
// per-line phase error is left visible as spread around the burst vectors.

// Geometry and levels describing one flat TBC-domain frame for the
// acquisition, in the 10-bit CVBS_U10_4FSC domain after conversion.
struct CompositeScopeParameters {
  ::VideoSystem system = PAL;

  // Uniform samples per line of the flat buffer (TBC fieldWidth, e.g. 1135
  // PAL / 910 NTSC).
  int32_t field_width = 0;

  // Line counts of the two field buffers, field 1 first in the flat buffer
  // (e.g. PAL 313 + 313, NTSC 263 + 263 as stored by ld-decode).
  int32_t field1_height = 0;
  int32_t field2_height = 0;

  // 10-bit CVBS domain levels (normative for the system).
  int32_t white_level = 0;      // 100 IRE
  int32_t blanking_level = 0;   // 0 IRE

  // Colour-burst sample window within a line (half-open), resolved from the
  // source metadata or the system constants.
  int32_t burst_start = 0;
  int32_t burst_end = 0;

  // Active video sample window within a line (half-open).
  int32_t active_video_start = 0;
  int32_t active_video_end = 0;

  // Active picture lines in interlaced frame numbering: first is 0-based
  // inclusive, last is exclusive (e.g. PAL 44 .. 620).
  int32_t first_active_frame_line = 0;
  int32_t last_active_frame_line = 0;

  // Total lines in the flat frame buffer.
  int32_t frame_lines() const { return field1_height + field2_height; }
};

// Sampling options for a composite acquisition.
struct CompositeVectorscopeOptions {
  // Portion of each line to sample.
  VectorscopeSampleWindow window = VectorscopeSampleWindow::WholeLine;

  // Restrict the emitted lines to the active picture
  // (first_active_frame_line .. last_active_frame_line - 1), which is what the
  // decoded acquisition plots.  Applied on top of |first_line| / |last_line| -
  // the two intersect.  The burst survey and the instrument readouts are
  // unaffected: they always run over every line of the frame that carries a
  // burst.
  bool active_lines_only = false;

  // Inclusive interlaced frame-line range (0-based), the numbering both
  // acquisitions use - line 0 is the top line of the frame and consecutive
  // numbers alternate fields.  |last_line| == 0 means "to the last line of
  // the frame".
  uint32_t first_line = 0;
  uint32_t last_line = 0;

  // Soft ceiling on emitted samples.  When the selected window would produce
  // more, every n-th sample is taken instead; the divisor is reported back as
  // VectorscopeData::sample_stride.  The default clears a whole PAL frame of
  // whole lines (625 x 1135 = 709 375) so the commonest acquisition is not
  // subsampled: dropping samples lengthens the chords the renderer draws
  // between them, which coarsens the trace exactly where it is busiest.
  uint32_t max_samples = 800000;

  // Average each emitted sample's demodulated U/V with the same sample on
  // the nearest lines of the same field that share its V-switch state
  // (two field lines away for PAL / PAL_M, one for NTSC), weighted
  // [1, 2, 1] / 4 - the vertical chroma filter of a 2D decoder.  Per-line
  // noise and line-to-line phase error then average down so the plot reads
  // as clean vectors and transitions, as the decoded scope does.  Display
  // only: the burst survey and every instrument readout use the raw lines.
  bool vertical_average = false;
};

// Nominal colour-burst peak amplitude in IRE for |system|.
// EBU Tech. 3280-E section 1.2: PAL burst is 300 mV peak-to-peak against a
// 700 mV luminance range -> 21.43 IRE peak.
// SMPTE 170M-2004 section 8.4: NTSC burst is 40 IRE peak-to-peak -> 20 IRE
// peak.  ITU-R BT.1700-1 Annex 1 Part B: PAL_M follows the 525-line levels.
double nominal_burst_amplitude_ire(::VideoSystem system);

// Subcarrier cycles per line for |system|, derived from the 4FSC frame
// geometry: a frame holds frame_samples/4 subcarrier cycles spread over
// frame_lines lines.  PAL gives 283.7516, NTSC 227.5, PAL_M 227.25.
// Returns 0 for an unsupported system (SECAM / MESECAM carry FM chroma and
// have no subcarrier to demodulate).
double subcarrier_cycles_per_line(::VideoSystem system);

// Demodulate |frame| and produce measurement-vectorscope samples.
//
// |frame| is a flat field-sequential CVBS_U10_4FSC frame buffer: field 1's
// lines (field1_height of them, each field_width samples) followed by field
// 2's.  |frame_sample_count| bounds reads into it.
//
// Returns nullopt when |parameters| does not describe a usable 4FSC frame.
//
// Complexity: O(selected lines x line width).
std::optional<VectorscopeData> extract_composite_vectorscope(
    const int16_t* frame, size_t frame_sample_count,
    const CompositeScopeParameters& parameters, uint64_t field_number,
    const CompositeVectorscopeOptions& options);

// ============================================================================
// TBC-domain adaptation layer
// ============================================================================

// Resolve the acquisition parameters for a source from its ld-decode
// VideoParameters, falling back to the system constants where the metadata
// does not carry a value (-1).  |field1_height| / |field2_height| are the
// actual stored field heights (0 falls back to the nominal field-1 height;
// the second field falls back to the first).
CompositeScopeParameters make_composite_scope_parameters(
    const TbcMetaData::VideoParameters& video_parameters,
    int32_t field1_height, int32_t field2_height);

// Convert one ld-decode .tbc frame (the two fields' raw 16-bit composite
// buffers) into the flat field-sequential 10-bit CVBS_U10_4FSC buffer the
// acquisition takes, using the per-sample level mapping of tbc_to_cvbs().
// Returns an empty vector when a field is empty or the levels are degenerate.
std::vector<int16_t> assemble_composite_frame(
    const std::vector<uint16_t>& first_field_composite,
    const std::vector<uint16_t>& second_field_composite,
    const TbcMetaData::VideoParameters& video_parameters);

}  // namespace tbc::cvbs

#endif  // TBC_ANALYSE_COMPOSITE_VECTORSCOPE_H
