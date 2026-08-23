/*
 * File:        tbc_cvbs_conversion.h
 * Module:      ld-analyse
 * Purpose:     Convert legacy 16-bit ld-decode TBC samples to the 10-bit
 *              CVBS_U10_4FSC domain used by the scopes
 *
 * Ported from decode-orc's tbc_source {pal,ntsc,pal_m}_tbc_converter
 * (tbc_to_cvbs per-sample level mapping).  Only the per-sample mapping is
 * needed for scope display; full-frame assembly (PAL bridge samples / NTSC
 * padding strip) is not required here.  All code lives in tbc-tools;
 * decode-orc is only read from.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef TBC_CVBS_CONVERSION_H
#define TBC_CVBS_CONVERSION_H

#include "cvbs_signal_constants.h"
#include "tbcmetadata.h"

#include <cmath>
#include <cstdint>

namespace tbc::cvbs {

// Map one 16-bit ld-decode TBC sample to the 10-bit CVBS_U10_4FSC domain.
//
// tbc_blanking / tbc_white are the 16-bit TBC-domain level values read from
// the source metadata (blanking_16b_ire, white_16b_ire).  The mapping is the
// linear level transform used by decode-orc's PalTBCConverter / NtscTBCConverter:
//   n = (sample - blanking) / (white - blanking)
//   cvbs = n * (kSysWhite - kSysBlanking) + kSysBlanking
// rounded to the nearest integer.  No output clamping is applied — headroom
// below sync tip and above peak white is preserved in the int16_t result, so
// out-of-range samples remain visible on the scope rather than being clipped.
//
// Returns the system 10-bit blanking level when white <= blanking (degenerate
// metadata) so the scope still renders a sane baseline.
int16_t tbc_to_cvbs(uint16_t tbc_sample, int32_t tbc_blanking,
                    int32_t tbc_white, ::VideoSystem system);

// Resolve the 16-bit blanking level for a source from its VideoParameters.
//
// ld-decode metadata stores white16bIre / black16bIre / blanking16bIre, where
// a value of -1 means "not set".  NTSC (and PAL_M, which decode-orc assigns
// the NTSC 10-bit levels incl. the 7.5 IRE setup) store black16bIre at the
// 7.5 IRE picture-black (setup) level, NOT the 0 IRE blanking reference.
// When blanking is absent (or equal to black, indicating it was never
// separately recorded) for a 525-line system, blanking is derived from black:
//   blanking_16b = black16bIre − 7.5 × (white16bIre − black16bIre) / 92.5
// PAL (625-line) has no setup pedestal, so black == blanking.
//
// An explicitly-provided, valid blanking that differs from black is always
// trusted as-is.
inline int32_t resolve_blanking_16b(::VideoSystem system, int32_t black16bIre,
                                    int32_t white16bIre,
                                    int32_t blanking16bIre) {
  // Trust an explicit, distinct blanking value from the metadata.
  if (blanking16bIre >= 0 && blanking16bIre != black16bIre) {
    return blanking16bIre;
  }

  // PAL (625-line): no setup pedestal — black is blanking.
  if (system == PAL) {
    return (black16bIre >= 0) ? black16bIre
                              : static_cast<int32_t>(kTbcPalBlanking);
  }

  // NTSC / PAL_M (525-line): 7.5 IRE setup pedestal.  decode-orc assigns PAL_M
  // the NTSC 10-bit levels (kNtscBlack = 282 = setup), so the setup-derived
  // path is used for both.  Fall back to normative 16-bit levels when the
  // metadata is missing black/white.
  const int32_t black =
      (black16bIre >= 0) ? black16bIre : static_cast<int32_t>(kTbcNtscBlack);
  const int32_t white =
      (white16bIre > black) ? white16bIre : static_cast<int32_t>(kTbcNtscWhite);

  const double diff = static_cast<double>(white - black);
  const int32_t derived =
      black - static_cast<int32_t>(std::lround(7.5 * diff / 92.5));
  return derived;
}

}  // namespace tbc::cvbs

#endif  // TBC_CVBS_CONVERSION_H
