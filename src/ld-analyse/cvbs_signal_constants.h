/*
 * File:        cvbs_signal_constants.h
 * Module:      ld-analyse
 * Purpose:     Normative CVBS_U10_4FSC signal constants for PAL, NTSC, and PAL_M
 *
 * Ported from decode-orc (orc/sdk/include/orc/stage/cvbs_signal_constants.h)
 * so ld-analyse's scopes use the same authoritative 10-bit CVBS domain and
 * spec-referenced conversions as decode-orc.  All code lives in tbc-tools;
 * decode-orc is only read from.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TBC_CVBS_SIGNAL_CONSTANTS_H
#define TBC_CVBS_SIGNAL_CONSTANTS_H

#include "tbcmetadata.h"  // global VideoSystem enum (PAL / NTSC / PAL_M)

#include <cstdint>
#include <utility>

// This header is the single authoritative source for CVBS_U10_4FSC signal
// constants within ld-analyse scope code.  No other file in ld-analyse may
// define these numeric values.  Scope code must include this header and use
// the named constants.

namespace tbc::cvbs {

// ---------------------------------------------------------------------------
// PAL — EBU Tech. 3280-E
// ---------------------------------------------------------------------------

// EBU Tech. 3280-E §1.1.1 Table 1: PAL colour subcarrier frequency.
constexpr double kPalFsc = 4'433'618.75;

// EBU Tech. 3280-E §1.1.1 Table 1: 4FSC sample rate = 4 × fsc.
constexpr double kPalSampleRate = 17'734'475.0;

// EBU Tech. 3280-E §1.2: Nominal samples per line (non-integer due to
// non-orthogonal sampling).
constexpr double kPalSamplesPerLine = 1135.0064;

// EBU Tech. 3280-E §1.2: Total samples in a complete PAL frame.
// = 625 lines × 1135 samples + 4 extra samples on non-orthogonal lines.
constexpr int32_t kPalFrameSamples = 709'379;

// EBU Tech. 3280-E §1.1.1 Table 1: Total lines in a PAL frame.
constexpr int32_t kPalFrameLines = 625;

// EBU Tech. 3280-E §1.3.2: Lines in PAL field 1 (the 313-line field; CVBS
// convention places the longer field first in the flat frame buffer).
constexpr int32_t kPalField1Lines = 313;

// EBU Tech. 3280-E §2.5.2 Table 2: First active picture line per field
// (0-based index into the field line buffer).
// PAL: lines 1–22 (1-based) carry sync equalising/broad pulses and VBI; active
// picture begins at line 23 (1-based) = index 22 (0-based).  Matches
// ld-decode convention: first_active_frame_line = 44 → firstActiveFieldLine
// = 44 / 2 = 22.
constexpr int32_t kPalFirstActiveLine = 22;

// EBU Tech. 3280-E §1.2: Nominal integer samples per line (floor of 1135.0064).
// Use this everywhere a fixed per-line width of 1135 is required (TBC storage,
// frame_line_sample_offset, display width, etc.).
constexpr int32_t kPalSamplesPerLineNominal = 1135;

// EBU Tech. 3280-E §1.2: Maximum samples on any PAL line.
// Lines 312 and 624 (0-based) each carry 2 extra samples → 1137.
constexpr int32_t kPalMaxSamplesPerLine = 1137;

// EBU Tech. 3280-E §1.1.1 Table 1: Normative CVBS_U10_4FSC signal levels
// (10-bit domain). PAL carries no setup pedestal: black = blanking (0 IRE).
constexpr int32_t kPalSyncTip = 4;
constexpr int32_t kPalBlanking = 256;
constexpr int32_t kPalBlack = 256;  // 0x100: no pedestal; black = blanking
constexpr int32_t kPalWhite = 844;
constexpr int32_t kPalPeak = 1019;

// EBU Tech. 3280-E §1.2: normative placement of the 4 extra samples per PAL
// frame.  The standard concentrates them on the last line of each field:
//   Frame-flat line 312 (0-based = line 313 1-based, last of field 1): +2
//   Frame-flat line 624 (0-based = line 625 1-based, last of field 2): +2
constexpr int32_t kPalExtraSampleLines[4] = {312, 312, 624, 624};

// ---------------------------------------------------------------------------
// NTSC — SMPTE 244M-2003
// ---------------------------------------------------------------------------

// SMPTE 244M-2003 §4.1: NTSC colour subcarrier frequency.
constexpr double kNtscFsc = 315.0e6 / 88.0;

// SMPTE 244M-2003 §4.1: 4FSC sample rate = 4 × fsc.
constexpr double kNtscSampleRate = 4.0 * kNtscFsc;

// SMPTE 244M-2003 §4.1: Samples per line (orthogonal, exact integer).
constexpr int32_t kNtscSamplesPerLine = 910;

// SMPTE 244M-2003 §4.1: Total samples in a complete NTSC frame.
// = 525 lines × 910 samples/line.
constexpr int32_t kNtscFrameSamples = 477'750;

// SMPTE 244M-2003 §3.1: Total lines in an NTSC frame.
constexpr int32_t kNtscFrameLines = 525;

// SMPTE 244M-2003 §3.2: Lines in NTSC VFR field 1 (top spatial field).
constexpr int32_t kNtscField1Lines = 263;

// SMPTE 170M-2004 §7.1 Table 1: First active picture line per field (0-based).
// NTSC: lines 1–20 (1-based) carry sync, equalising/broad pulses, and VBI
// (closed captions, VITS, etc.); active picture begins at line 21 (1-based) =
// index 20 (0-based).
constexpr int32_t kNtscFirstActiveLine = 20;

// SMPTE 244M-2003 §4.2.1 Table 1: Normative CVBS_U10_4FSC signal levels.
constexpr int32_t kNtscSyncTip = 16;    // 0x010: -40 IRE sync tip
constexpr int32_t kNtscBlanking = 240;  // 0x0F0: 0 IRE blanking reference
// SMPTE 170M-2004 Table 1: black (setup) = 7.5 IRE above blanking.
// 240 + 7.5 x (800 - 240) / 100 = 240 + 42 = 282.
constexpr int32_t kNtscBlack = 282;  // 0x11A: +7.5 IRE picture black
constexpr int32_t kNtscWhite = 800;  // 0x320: +100 IRE white
// SMPTE 244M-2003 §4.2.4: maximum permitted 10-bit value = 0x3FB = 1019.
constexpr int32_t kNtscPeak = 1019;  // 0x3FB: maximum legal sample value

// ---------------------------------------------------------------------------
// PAL_M — ITU-R BT.1700-1 Annex 1 Part B
// ---------------------------------------------------------------------------

// ITU-R BT.1700-1 Annex 1 Part B: PAL_M colour subcarrier frequency.
// Derived from: fsc = (909/4) × (525 × 30000/1001) Hz.
constexpr double kPalMFsc = (909.0 / 4.0) * (525.0 * 30000.0 / 1001.0);

// ITU-R BT.1700-1 Annex 1 Part B: 4FSC sample rate = 4 × fsc.
constexpr double kPalMSampleRate = 4.0 * kPalMFsc;

// ITU-R BT.1700-1 Annex 1 Part B: Samples per line (orthogonal, exact integer).
constexpr int32_t kPalMSamplesPerLine = 909;

// ITU-R BT.1700-1 Annex 1 Part B: Total samples in a complete PAL_M frame.
// = 525 lines × 909 samples/line.
constexpr int32_t kPalMFrameSamples = 477'225;

// ITU-R BT.1700-1 Annex 1 Part B: Total lines in a PAL_M frame.
constexpr int32_t kPalMFrameLines = 525;

// ITU-R BT.1700-1 Annex 1 Part B: Lines in PAL_M VFR field 1 (top spatial
// field). Identical to NTSC.
constexpr int32_t kPalMField1Lines = 263;

// ITU-R BT.1700-1 Annex 1 Part B: First active picture line per field
// (0-based).  PAL_M has the same 525-line structure as NTSC.
constexpr int32_t kPalMFirstActiveLine = kNtscFirstActiveLine;

// PAL M signal levels are identical to NTSC (same line count and blanking).
// Use kNtscSyncTip, kNtscBlanking, kNtscBlack, kNtscWhite, kNtscPeak.

// ---------------------------------------------------------------------------
// ld-decode 16-bit domain normative levels
// ---------------------------------------------------------------------------
// The ld-decode .tbc format stores samples as uint16_t using a ×64 scale
// factor applied to the CVBS_U10_4FSC signal levels.  These constants are
// ONLY for use when converting CVBS_U10_4FSC ↔ ld-decode 16-bit.
//
// NTSC setup note: ld-decode JSON stores black16bIre at the 7.5 IRE picture
// black (setup) level = kNtscBlack × 64 = 282 × 64 = 18048, NOT the 0 IRE
// blanking level = kNtscBlanking × 64 = 240 × 64 = 15360.  When
// blanking16bIre is absent or equals black16bIre for an NTSC/PAL_M source,
// derive blanking from black using the formula:
//   blanking_16b = black16bIre − 7.5 × (white16bIre − black16bIre) / 92.5
// PAL and PAL_M have no setup pedestal so black == blanking.

// PAL ld-decode 16-bit domain levels (CVBS_U10_4FSC × 64):
constexpr int32_t kTbcPalBlanking = 16384;  // kPalBlanking × 64 = 256 × 64
constexpr int32_t kTbcPalWhite = 54016;     // kPalWhite × 64 = 844 × 64

// NTSC ld-decode 16-bit domain levels (CVBS_U10_4FSC × 64):
constexpr int32_t kTbcNtscBlanking = 15360;  // kNtscBlanking × 64 = 240 × 64
constexpr int32_t kTbcNtscBlack = 18048;     // kNtscBlack × 64 = 282 × 64 (setup)
constexpr int32_t kTbcNtscWhite = 51200;     // kNtscWhite × 64 = 800 × 64

// ---------------------------------------------------------------------------
// Active video geometry constants
// ---------------------------------------------------------------------------
// Normative sample and line positions for the active picture area.
// BT.601-5 §2 / EBU Tech. 3280-E §1.2 / SMPTE 170M §6.4 / ITU-R BT.1700-1.

// PAL (625-line, EBU Tech. 3280-E §1.2):
constexpr int32_t kPalActiveVideoStart = 157;
constexpr int32_t kPalActiveVideoEnd = 157 + 948;  // = 1105
constexpr int32_t kPalFirstActiveFrameLine = 44;
// ITU-R BT.1700 Table 1 item 1a: 576 active lines for 625-line PAL.
constexpr int32_t kPalLastActiveFrameLine = 620;

// NTSC (525-line, SMPTE 170M §6.4):
constexpr int32_t kNtscActiveVideoStart = 126;
constexpr int32_t kNtscActiveVideoEnd = 126 + 768;  // = 894
constexpr int32_t kNtscFirstActiveFrameLine = 40;
// ITU-R BT.1700 Table 1 item 1a: 483 active lines for 525-line systems.
constexpr int32_t kNtscLastActiveFrameLine = 523;

// PAL_M shares NTSC active video geometry (ITU-R BT.1700-1 Annex 1 Part B).

// ---------------------------------------------------------------------------
// Per-system total-sample and frame-line helpers
// ---------------------------------------------------------------------------

// Return the total number of samples in a complete frame for the given system.
inline int32_t frame_samples_from_system(::VideoSystem sys) {
  switch (sys) {
    case PAL:
      return kPalFrameSamples;
    case NTSC:
      return kNtscFrameSamples;
    case PAL_M:
      return kPalMFrameSamples;
    default:
      return 0;
  }
}

// Return the total number of lines in a complete frame for the given system.
inline int32_t frame_lines_from_system(::VideoSystem sys) {
  switch (sys) {
    case PAL:
      return kPalFrameLines;
    case NTSC:
      return kNtscFrameLines;
    case PAL_M:
      return kPalMFrameLines;
    default:
      return 0;
  }
}

// Return the nominal samples-per-line for the given system.
inline int32_t samples_per_line_from_system(::VideoSystem sys) {
  switch (sys) {
    case PAL:
      return kPalSamplesPerLineNominal;
    case NTSC:
      return kNtscSamplesPerLine;
    case PAL_M:
      return kPalMSamplesPerLine;
    default:
      return 0;
  }
}

// ---------------------------------------------------------------------------
// Colour burst sample range constants
// ---------------------------------------------------------------------------

// EBU Tech. 3280-E Table 1: PAL colour burst sample range.
constexpr int32_t kPalColourBurstStart = 98;
constexpr int32_t kPalColourBurstEnd = 138;

// NTSC colour burst sample range in the ld-decode CVBS_U10_4FSC line format.
// SMPTE 170M-2004 Table 2: burst duration = 9 ± 1 subcarrier cycles →
// 36 ± 4 samples at 4fsc. Window here is exactly 36 samples (9 cycles).
// PAL_M uses the same values as NTSC.
constexpr int32_t kNtscColourBurstStart = 72;
constexpr int32_t kNtscColourBurstEnd = 108;

// Return the (start, end) colour burst sample range for the given video system.
inline std::pair<int32_t, int32_t> colour_burst_range(::VideoSystem sys) {
  if (sys == PAL) {
    return {kPalColourBurstStart, kPalColourBurstEnd};
  }
  // NTSC and PAL_M share the same burst window.
  return {kNtscColourBurstStart, kNtscColourBurstEnd};
}

// ---------------------------------------------------------------------------
// System-derived sample rate and subcarrier frequency helpers
// ---------------------------------------------------------------------------

// Return the 4FSC sample rate (Hz) for the given video system.
inline double sample_rate_from_system(::VideoSystem sys) {
  switch (sys) {
    case PAL:
      return kPalSampleRate;
    case PAL_M:
      return kPalMSampleRate;
    default:
      return kNtscSampleRate;
  }
}

// Return the colour subcarrier frequency (Hz) for the given video system.
inline double fsc_from_system(::VideoSystem sys) {
  switch (sys) {
    case PAL:
      return kPalFsc;
    case PAL_M:
      return kPalMFsc;
    default:
      return kNtscFsc;
  }
}

// ---------------------------------------------------------------------------
// Per-system 10-bit CVBS level accessors (used by the scope renderers)
// ---------------------------------------------------------------------------

inline int32_t cvbs_blanking(::VideoSystem sys) {
  return (sys == PAL) ? kPalBlanking : kNtscBlanking;
}

inline int32_t cvbs_black(::VideoSystem sys) {
  return (sys == PAL) ? kPalBlack : kNtscBlack;
}

inline int32_t cvbs_white(::VideoSystem sys) {
  return (sys == PAL) ? kPalWhite : kNtscWhite;
}

inline int32_t cvbs_peak(::VideoSystem sys) {
  return (sys == PAL) ? kPalPeak : kNtscPeak;
}

inline int32_t cvbs_sync_tip(::VideoSystem sys) {
  return (sys == PAL) ? kPalSyncTip : kNtscSyncTip;
}

}  // namespace tbc::cvbs

#endif  // TBC_CVBS_SIGNAL_CONSTANTS_H
