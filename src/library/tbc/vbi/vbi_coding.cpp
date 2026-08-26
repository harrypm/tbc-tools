/*
 * File:        vbi_coding.cpp
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Hamming 8/4 and 24/18 error-protection codec implementations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/common/vbi-services/teletext_slicer.cpp) at tag v2.7.2
 * (commit fef0115a). Only the Hamming 8/4 + 24/18 encode/decode functions were
 * extracted here; the analog slicer stays with the VBI-scanning phase. The
 * orc:: namespace was re-namespaced to tbc::vbi::.
 */

#include "vbi_coding.h"

#include <array>
#include <cstdint>

namespace tbc::vbi {

uint8_t teletext_hamming84_encode(uint8_t value) {
  // ETSI EN 300 706 §8.2 encoding equations. Bit numbering: spec bit 1 (first
  // transmitted) is the byte LSB, so P1..P4 occupy bits 0/2/4/6 and D1..D4
  // bits 1/3/5/7.
  const int d1 = (value >> 0) & 1;
  const int d2 = (value >> 1) & 1;
  const int d3 = (value >> 2) & 1;
  const int d4 = (value >> 3) & 1;
  const int p1 = 1 ^ d1 ^ d3 ^ d4;
  const int p2 = 1 ^ d1 ^ d2 ^ d4;
  const int p3 = 1 ^ d1 ^ d2 ^ d3;
  const int p4 = 1 ^ p1 ^ d1 ^ p2 ^ d2 ^ p3 ^ d3 ^ d4;
  return static_cast<uint8_t>((p1 << 0) | (d1 << 1) | (p2 << 2) | (d2 << 3) |
                              (p3 << 4) | (d3 << 5) | (p4 << 6) | (d4 << 7));
}

int teletext_hamming84_decode(uint8_t byte) {
  // ETSI EN 300 706 §8.2: Hamming 8/4 has minimum distance 4, so every byte
  // within Hamming distance 1 of a codeword decodes to that codeword (single
  // errors corrected, including protection-bit errors) and every byte at
  // distance 2 is uncorrectable (double error detected). A 256-entry table
  // realises exactly that decision rule.
  static const auto kTable = [] {
    std::array<int8_t, 256> table{};
    table.fill(-1);
    for (int value = 0; value < 16; ++value) {
      const uint8_t code =
          teletext_hamming84_encode(static_cast<uint8_t>(value));
      table[code] = static_cast<int8_t>(value);
      for (int bit = 0; bit < 8; ++bit) {
        table[code ^ (1u << bit)] = static_cast<int8_t>(value);
      }
    }
    return table;
  }();
  return kTable[byte];
}

namespace {

// Transmission-order bit positions (1-based, as ETSI EN 300 706 §8.3 numbers
// them) of D1 to D18 within a Hamming 24/18 triplet. The six that are missing
// — 1, 2, 4, 8, 16 and 24 — are the protection bits P1 to P6.
constexpr std::array<int, 18> kHamming2418DataBits = {
    3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 17, 18, 19, 20, 21, 22, 23};

// The five odd-parity tests A to E of §8.3, as masks over the triplet's 24
// transmission-order bits held with bit 1 in the least significant position.
//
// Test i covers every position whose own number has bit i set, which is what
// makes the five failures spell out the position of a single error directly
// (§8.3 note 2). Position 24 is the exception the standard's table draws: it
// is P6, the parity of the whole triplet, and takes part in no test but F.
constexpr std::array<uint32_t, 5> hamming2418_test_masks() {
  std::array<uint32_t, 5> masks{};
  for (size_t test = 0; test < masks.size(); ++test) {
    for (uint32_t position = 1; position <= 23; ++position) {
      if ((position & (1u << test)) != 0u) {
        masks[test] |= 1u << (position - 1u);
      }
    }
  }
  return masks;
}

constexpr std::array<uint32_t, 5> kHamming2418TestMasks =
    hamming2418_test_masks();

bool odd_parity_of(uint32_t bits) {
  // True when the number of set bits is odd, which is what each of the
  // standard's tests requires of the bits it covers.
  int ones = 0;
  for (int bit = 0; bit < 24; ++bit) {
    ones += static_cast<int>((bits >> bit) & 1u);
  }
  return (ones % 2) == 1;
}

}  // namespace

void teletext_hamming2418_encode(uint32_t value, uint8_t out_bytes[3]) {
  uint32_t bits = 0;
  for (size_t index = 0; index < kHamming2418DataBits.size(); ++index) {
    if (((value >> index) & 1u) != 0u) {
      bits |= 1u << (kHamming2418DataBits[index] - 1);
    }
  }

  // P1 to P5 make their own test odd; P6 makes the whole triplet odd. Each
  // protection bit sits inside its own test and outside the others', so they
  // can be solved in this order without iterating.
  for (size_t test = 0; test < kHamming2418TestMasks.size(); ++test) {
    if (odd_parity_of(bits & kHamming2418TestMasks[test])) {
      continue;  // Already odd over this test's bits.
    }
    // The protection bit of test i is the one at position 2^i.
    bits |= 1u << ((1u << test) - 1u);
  }
  if (!odd_parity_of(bits)) {
    bits |= 1u << 23;  // P6, transmission-order bit 24
  }

  out_bytes[0] = static_cast<uint8_t>(bits & 0xFFu);
  out_bytes[1] = static_cast<uint8_t>((bits >> 8) & 0xFFu);
  out_bytes[2] = static_cast<uint8_t>((bits >> 16) & 0xFFu);
}

int32_t teletext_hamming2418_decode(uint8_t byte_n, uint8_t byte_n1,
                                    uint8_t byte_n2) {
  uint32_t bits = static_cast<uint32_t>(byte_n) |
                  (static_cast<uint32_t>(byte_n1) << 8) |
                  (static_cast<uint32_t>(byte_n2) << 16);

  // ETSI EN 300 706 §8.3: tests A to E locate a single error, test F over the
  // whole triplet separates one error from two. Every test is an *odd* parity
  // test, so a failure is an even count.
  uint32_t error_position = 0;
  for (size_t test = 0; test < kHamming2418TestMasks.size(); ++test) {
    if (!odd_parity_of(bits & kHamming2418TestMasks[test])) {
      error_position |= 1u << test;
    }
  }
  const bool test_f_ok = odd_parity_of(bits);

  if (error_position != 0) {
    if (test_f_ok) {
      return -1;  // Two errors: detected, not correctable.
    }
    // One error, at the transmission-order position the tests spell out. A
    // position beyond the triplet cannot arise from a single error, so it is
    // rejected rather than used to flip a bit that is not there.
    if (error_position > 24) {
      return -1;
    }
    bits ^= 1u << (error_position - 1u);
  }
  // error_position == 0 with test F failing is an error in P6 alone, which
  // leaves the data bits good — so there is nothing to do in either case.

  int32_t value = 0;
  for (size_t index = 0; index < kHamming2418DataBits.size(); ++index) {
    if (((bits >> (kHamming2418DataBits[index] - 1)) & 1u) != 0u) {
      value |= 1 << index;
    }
  }
  return value;
}

}  // namespace tbc::vbi
