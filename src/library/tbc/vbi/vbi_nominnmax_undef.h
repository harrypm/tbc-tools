/*
 * File:        vbi_nominnmax_undef.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Forcefully defeat the min/max macros that some Windows/Qt headers
 *              define as object-like macros, so std::min/std::max resolve cleanly.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * NOMINMAX stops <windows.h> from defining min/max, but some Qt headers
 * pull in other definitions that are not gated by NOMINMAX, and those break
 * std::min/std::max even when the call's function name is wrapped in
 * parentheses. Undefining the macros here, after all includes, is the one fix
 * that works regardless of where the macros came from. Include this header
 * first, after the project headers and before the <algorithm>/<utility> uses.
 */

#ifndef TBC_VBI_NOMINMAX_UNDEF_H
#define TBC_VBI_NOMINMAX_UNDEF_H

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#endif  // TBC_VBI_NOMINMAX_UNDEF_H
