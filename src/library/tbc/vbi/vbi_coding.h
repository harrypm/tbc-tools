/*
 * File:        vbi_coding.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Shared teletext/NABTS coding constants and Hamming 8/4 + 24/18
 *              error-protection helpers used by the packet, data-group and
 *              record layers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/common/vbi-services/teletext_slicer.h) at tag v2.7.2
 * (commit fef0115a). Only the shared coding constants, the TeletextSystem
 * enumerators and accessors, the TeletextPacketConfidence type and the
 * Hamming 8/4 + 24/18 error-protection functions were extracted here; the
 * analog data-line slicer (clock run-in search, framing-code correlation,
 * MLSE bit detection) stays with the VBI-scanning phase. The orc:: namespace
 * was re-namespaced to tbc::vbi::.
 */

#ifndef TBC_VBI_VBI_CODING_H
#define TBC_VBI_VBI_CODING_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace tbc::vbi {

// ETSI EN 300 706 §5.3: bit rate = 444 × nominal fH = 6,9375 Mbit/s ± 25 ppm.
constexpr double kTeletextBitRate = 6'937'500.0;

// ETSI EN 300 706 §7.1: a teletext packet comprises 360 bits organized as 45
// bytes; removing the clock run-in (2 bytes, §6.1) and framing code (1 byte,
// §6.2) leaves the 42-byte MRAG + data payload — the T42 packet.
//
// This is also the size of the packet buffer everywhere downstream: the
// 525-line variant below is shorter and occupies the leading bytes of the same
// array.
constexpr size_t kTeletextPacketBytes = 42;

// ITU-R BT.653 Table 1b, Teletext System B on 525-line television systems:
// bit rate = 364 × fH = 5,727272 Mbit/s ± 25 ppm. At 4FSC NTSC that is exactly
// 2,5 samples per bit.
constexpr double kTeletext525BitRate = 5'727'272.0;

// ITU-R BT.653 Table 1b: the 525-line data line is 296 bits = 37 bytes;
// removing the clock run-in (2 bytes) and the framing code (1 byte) leaves 34
// bytes — the 2-byte prefix (MRAG) plus a 32-byte data block.
constexpr size_t kTeletext525PacketBytes = 34;

// CEA-516-S-2013 §2.1 and §3.1, Teletext System C (NABTS) on 525-line
// television systems: the data line is 288 bits, of which the first 24 are the
// synchronization sequence (§2.2), leaving a 264-bit data packet organized as
// 33 bytes. The bit rate is the 525-line System B one (§1.3) and so is the
// clock run-in; the framing code (§2.2.3) and this length are what differ.
constexpr size_t kNabtsPacketBytes = 33;

// Teletext service, and the television system it is carried on. All three
// share the 16-bit clock run-in and the LSB-first byte order, and differ in
// bit rate, framing code, packet length and the position of the data in the
// line.
enum class TeletextSystem {
  // ETSI EN 300 706 (System B on 625 lines): 6,9375 Mbit/s, 42-byte packet,
  // framing code 0xE4.
  kWst625,

  // ITU-R BT.653 Table 1b (System B on 525 lines): 5,727272 Mbit/s, 34-byte
  // packet, framing code 0xE4. The service US broadcasters carried as "WST".
  kWst525,

  // CEA-516-S-2013, ITU-R BT.653 System C (NABTS) on 525 lines: the same
  // 5,727272 Mbit/s (§1.3) and the same clock run-in (§2.2.2) as the line
  // above, with framing code 0xE7 (§2.2.3) and a 33-byte packet (§3.1). The
  // framing code is the only thing that separates the two on a capture.
  kNabts525,
};

// Transmitted bit rate of |system|, in Hz.
constexpr double teletext_bit_rate(TeletextSystem system) {
  return system == TeletextSystem::kWst625 ? kTeletextBitRate
                                           : kTeletext525BitRate;
}

// Packet length of |system|, in bytes (framing code excluded).
constexpr size_t teletext_packet_bytes(TeletextSystem system) {
  switch (system) {
    case TeletextSystem::kWst525:
      return kTeletext525PacketBytes;
    case TeletextSystem::kNabts525:
      return kNabtsPacketBytes;
    case TeletextSystem::kWst625:
      break;
  }
  return kTeletextPacketBytes;
}

// Whether |system| gives its data bytes byte-wise odd parity in a way a slicer
// may gate on.
//
// True for both System B services: ETSI EN 300 706 §9.3.1 and ITU-R BT.653
// Table 1b give the display bytes of rows 0-25 odd parity, and the row number
// is recoverable from the packet's own addressing. False for NABTS, where
// CEA-516 §3.3 makes byte parity conditional on the data group type — a
// property of the group the packet belongs to, not of the packet, and so not
// knowable from one line.
constexpr bool teletext_has_parity_coded_rows(TeletextSystem system) {
  return system != TeletextSystem::kNabts525;
}

// Bytes at the head of a packet that carry Hamming 8/4 protected addressing,
// and which a slicer may therefore test for plausibility.
//
//   System B: the two MRAG bytes (ETSI EN 300 706 §7.1.2).
//   System C: the five packet prefix bytes P1-P3, CI and PS (CEA-516 §3.2.1),
//             which use the same Hamming 8/4 code (§3.2.2).
constexpr size_t teletext_hamming_prefix_bytes(TeletextSystem system) {
  return system == TeletextSystem::kNabts525 ? 5 : 2;
}

// Encode a 4-bit value as a Hamming 8/4 protected byte.
// ETSI EN 300 706 §8.2: bits 1, 3, 5, 7 (LSB numbering, transmission order)
// carry the protection bits P1-P4 and bits 2, 4, 6, 8 the data bits D1-D4.
// Only the low nibble of |value| is used.
uint8_t teletext_hamming84_encode(uint8_t value);

// Decode a Hamming 8/4 protected byte.
// ETSI EN 300 706 §8.2: single-bit errors are identified and corrected;
// double-bit errors are detected. Returns the decoded 4-bit value (0-15), or
// -1 when the byte is uncorrectable (double-bit error).
int teletext_hamming84_decode(uint8_t byte);

// Encode 18 data bits as a Hamming 24/18 protected triplet.
// ETSI EN 300 706 §8.3: over three consecutive bytes, transmission-order bits
// 1, 2, 4, 8, 16 and 24 are the protection bits P1-P6 and the remaining
// eighteen carry D1-D18. |value| supplies D1 (its bit 0) to D18 (its bit 17);
// bits above that are ignored. |out_bytes| receives the three bytes in
// transmission order.
void teletext_hamming2418_encode(uint32_t value, uint8_t out_bytes[3]);

// Decode a Hamming 24/18 protected triplet (ETSI EN 300 706 §8.3), given the
// three bytes in transmission order. Single-bit errors are identified and
// corrected; double-bit errors are detected. Returns the 18-bit value with D1
// in bit 0, or -1 when the triplet is uncorrectable.
//
// The returned value is what the standard's packet tables address as "triplet
// bits 1 to 18", so a field at bits m-n is read as (value >> (m - 1)) masked
// to n - m + 1 bits.
int32_t teletext_hamming2418_decode(uint8_t byte_n, uint8_t byte_n1,
                                    uint8_t byte_n2);

// How sure the recovery chain was of each byte of a packet, 0 … 1. Indexed by
// packet byte; only the leading packet_bytes() entries are meaningful for a
// given system.
using TeletextPacketConfidence = std::array<float, kTeletextPacketBytes>;

}  // namespace tbc::vbi

#endif  // TBC_VBI_VBI_CODING_H
