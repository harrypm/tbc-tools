/*
 * File:        vectorscope_data.h
 * Module:      tbc-analyse
 * Purpose:     Vectorscope data contract for the composite (measurement)
 *              acquisition and the shared scope display
 *
 * Ported from decode-orc (orc/sdk/include/orc/stage/preview/orc_vectorscope.h)
 * so tbc-analyse's vectorscope carries the same acquisition semantics and
 * instrument readouts as decode-orc's measurement scope.  All code lives in
 * tbc-tools; decode-orc is only read from.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TBC_ANALYSE_VECTORSCOPE_DATA_H
#define TBC_ANALYSE_VECTORSCOPE_DATA_H

#include "cvbs_signal_constants.h"  // global ::VideoSystem enum

#include <cstdint>
#include <vector>

namespace tbc::cvbs {

// How the U/V samples in a VectorscopeData were acquired.
//
// The two modes answer different questions and are not interchangeable:
//
// - DecodedComponent is a post-production colour-grading scope.  It plots the
//   U/V planes a chroma decoder produced, i.e. after demodulation, after
//   comb/delay-line filtering and after the PAL V-switch has been undone.  It
//   shows what the decoder output looks like.
// - CompositeCarrier is a technical measurement scope.  It plots chroma
//   demodulated straight from the composite carrier with no delay-line
//   averaging and no V-switch correction, so the colour burst and both PAL
//   line phases can be present in the data set.  It shows what the signal
//   looks like.
enum class VectorscopeAcquisitionMode : uint8_t {
  DecodedComponent = 0,  // Decoded U/V planes (grading scope)
  CompositeCarrier = 1,  // Demodulated composite carrier (measurement scope)
};

// Which part of the line a sample was taken from.
//
// Only meaningful for CompositeCarrier acquisition; DecodedComponent samples
// are always Picture because the decoded planes hold active picture only.
enum class VectorscopeSampleClass : uint8_t {
  Picture = 0,   // Within the active picture window
  Burst = 1,     // Within the colour-burst window on the back porch
  Blanking = 2,  // Elsewhere on the line (sync, porches, VBI)
};

// PAL V-switch state of the line a sample was taken from.
//
// ITU-R BT.470-6 Table 2 item 2.16: the PAL V component is inverted on
// alternate lines.  A measurement scope does not undo that inversion, so every
// sample carries the state of the line it came from; the two states plot as
// two sets of targets mirrored about the U axis.
enum class VectorscopeLinePhase : uint8_t {
  NotApplicable = 0,  // NTSC, or V-switch state not determined
  VPositive = 1,      // +V line
  VNegative = 2,      // -V line
};

// Portion of each line a composite acquisition samples.
enum class VectorscopeSampleWindow : uint8_t {
  BurstOnly = 0,   // Colour-burst window only
  ActiveLine = 1,  // Active picture window only
  WholeLine = 2,   // Entire line including sync, porches and burst
};

// Single U/V sample point for vectorscope display, in the signed display
// domain (+/-32767, the blanking-to-white range mapped to full scale).
struct UVSample {
  double u;                    // U (Cb) component
  double v;                    // V (Cr) component
  uint8_t field_id;            // Field index (0 = first/odd, 1 = second/even)
  VectorscopeSampleClass sample_class;  // Part of the line this came from
  VectorscopeLinePhase line_phase;      // PAL V-switch state of that line
  uint8_t reserved;            // Padding; keeps line_number aligned
  // Interlaced frame-line index (0-based) the sample is on: line 0 is the
  // top line of the frame and consecutive numbers alternate fields.
  uint16_t line_number;

  UVSample()
      : u(0),
        v(0),
        field_id(0),
        sample_class(VectorscopeSampleClass::Picture),
        line_phase(VectorscopeLinePhase::NotApplicable),
        reserved(0),
        line_number(0) {}

  UVSample(double u_val, double v_val, uint8_t field = 0)
      : u(u_val),
        v(v_val),
        field_id(field),
        sample_class(VectorscopeSampleClass::Picture),
        line_phase(VectorscopeLinePhase::NotApplicable),
        reserved(0),
        line_number(0) {}

  UVSample(double u_val, double v_val, uint8_t field,
           VectorscopeSampleClass sample_class_val,
           VectorscopeLinePhase line_phase_val, uint16_t line_number_val)
      : u(u_val),
        v(v_val),
        field_id(field),
        sample_class(sample_class_val),
        line_phase(line_phase_val),
        reserved(0),
        line_number(line_number_val) {}
};

// Instrument readouts derived from a composite acquisition.
//
// All values are computed from the burst on every line that carries one, so
// they are only populated for VectorscopeAcquisitionMode::CompositeCarrier.
struct VectorscopeMeasurements {
  bool valid = false;  // True when a burst reference was recovered

  // Mean burst peak amplitude in IRE (100 IRE = blanking->white).
  double burst_amplitude_ire = 0.0;

  // Mean burst amplitude as a percentage of the nominal for the system
  // (EBU Tech. 3280-E section 1.2: PAL 300 mV p-p; SMPTE 170M-2004 section
  // 8.4: NTSC 40 IRE p-p).  100 % means the burst is at its specified
  // amplitude.
  double burst_amplitude_percent = 0.0;

  // RMS deviation of the per-line burst phase from the mean phase of its own
  // V-switch group, in degrees - subcarrier phase jitter.
  double burst_phase_jitter_degrees = 0.0;

  // PAL only: half the angle between the two burst vectors minus the nominal
  // 45 degrees, in degrees.  Zero when the +/-V burst split is exactly 90
  // degrees.
  double burst_phase_split_error_degrees = 0.0;

  // Mean active-picture chroma amplitude divided by the mean burst amplitude.
  // 0 when no active-picture samples were acquired.
  double chroma_to_burst_ratio = 0.0;

  uint32_t burst_line_count = 0;  // Lines that contributed to the reference
};

// Vectorscope data extracted from a frame.
//
// Contains all U/V chroma samples for vectorscope visualization along with
// the parameters needed for graticule rendering and colour accuracy targets.
struct VectorscopeData {
  std::vector<UVSample> samples;  // All U/V samples from the frame
  uint32_t width;                 // Sampled window width in samples
  uint32_t height;                // Sampled line count
  uint64_t field_number;          // Frame/field number for identification

  // CVBS_U10_4FSC anchor points for graticule/targets (10-bit domain).
  ::VideoSystem system = PAL;  // Video system (PAL / NTSC / PAL_M)
  int32_t cvbs_white = 0;      // White level (100 IRE) in CVBS_U10_4FSC
  int32_t cvbs_blanking = 0;   // Blanking level (0 IRE) in CVBS_U10_4FSC

  // How these samples were acquired; selects the graticule the renderer draws.
  VectorscopeAcquisitionMode acquisition_mode =
      VectorscopeAcquisitionMode::DecodedComponent;

  // Portion of each line sampled (CompositeCarrier only).
  VectorscopeSampleWindow sample_window = VectorscopeSampleWindow::WholeLine;

  // Inclusive interlaced frame-line range actually sampled.
  uint32_t first_line = 0;
  uint32_t last_line = 0;

  // Every |sample_stride|-th sample of each line was taken (1 = every sample).
  uint32_t sample_stride = 1;

  // Instrument readouts (CompositeCarrier only).
  VectorscopeMeasurements measurements{};

  VectorscopeData() : width(0), height(0), field_number(0) {}
};

}  // namespace tbc::cvbs

#endif  // TBC_ANALYSE_VECTORSCOPE_DATA_H
