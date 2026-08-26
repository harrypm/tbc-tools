/*
 * File:        nabts_packet.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Decode one NABTS data packet: prefix, data block extent and
 *              suffix error protection (CEA-516 §3)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/nabts_sink/nabts_packet.h) at tag v2.7.2 (commit
 * fef0115a). Algorithmic bodies are intact; orc:: -> tbc::vbi:: and the
 * vbi-services/teletext_slicer.h include replaced by tbc/vbi/vbi_coding.h.
 */

#ifndef TBC_VBI_NABTS_PACKET_H
#define TBC_VBI_NABTS_PACKET_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "vbi_coding.h"

namespace tbc::vbi {

// CEA-516 §3.2.1: the packet prefix is five Hamming 8/4 bytes, P1 P2 P3 CI PS.
constexpr size_t kNabtsPrefixBytes = 5;

// CEA-516 §3.3: the data block runs from the prefix to the suffix, so it is at
// its longest when there is no suffix at all — 33 − 5 = 28 bytes.
constexpr size_t kNabtsMaxDataBlockBytes =
    kNabtsPacketBytes - kNabtsPrefixBytes;

// CEA-516 §3.2.3: P1 P2 P3 are one hexadecimal digit each, so 4096 channels.
constexpr uint16_t kNabtsChannelCount = 4096;

// CEA-516 §3.2.4: the continuity index runs 0 to 15 and wraps.
constexpr uint8_t kNabtsContinuityModulus = 16;

/**
 * @brief Suffix length and error-protection scheme, from PS b8/b6
 *
 * CEA-516 §3.4. The four codes are a length and a promise about what the bytes
 * mean; the data block is what is left of the packet once the prefix and the
 * suffix are accounted for.
 */
enum class NabtsSuffixKind {
  // 0,0 — no suffix. The whole 28 bytes after the prefix are data, and the
  // packet carries no check of its own.
  kNone,
  // 0,1 — one longitudinal parity-check byte over data block plus suffix.
  kLongitudinal,
  // 1,0 — two bytes; the last is the longitudinal parity check and the one
  // before it is "subject to further study" in §3.4. It is counted as suffix,
  // and so is covered by the longitudinal check, but nothing is read from it.
  kLongitudinalPlusReserved,
  // 1,1 — 28 bytes, the whole packet after the prefix. §3.4 terminates a bundle
  // of earlier data blocks with one or more of these, and leaves the method
  // "reserved for future standardization". Such a packet carries no data block.
  kBundle,
};

/// Bytes of suffix |kind| occupies (CEA-516 §3.4).
constexpr size_t nabts_suffix_bytes(NabtsSuffixKind kind) {
  switch (kind) {
    case NabtsSuffixKind::kNone:
      return 0;
    case NabtsSuffixKind::kLongitudinal:
      return 1;
    case NabtsSuffixKind::kLongitudinalPlusReserved:
      return 2;
    case NabtsSuffixKind::kBundle:
      return kNabtsMaxDataBlockBytes;
  }
  return 0;
}

/// Data-block bytes a packet with suffix |kind| carries (CEA-516 §3.3: zero,
/// 26, 27 or 28).
constexpr size_t nabts_data_block_bytes(NabtsSuffixKind kind) {
  return kNabtsMaxDataBlockBytes - nabts_suffix_bytes(kind);
}

/**
 * @brief What the suffix had to say about the data block
 *
 * The longitudinal parity byte and the per-byte parity bits form a product code
 * (CEA-516 §3.4): the longitudinal syndrome says which *bit* is wrong and the
 * failing byte parity says which *byte*, so between them a single-bit error
 * anywhere in the block is located and corrected.
 */
enum class NabtsBlockIntegrity {
  // No suffix, so nothing was checked and nothing is claimed. Not a fault: PS
  // 0,0 is a legal packet that simply carries no check of its own.
  kUnchecked,
  // The longitudinal check passed as transmitted.
  kClean,
  // One bit was wrong and the product code put it back.
  kCorrected,
  // The longitudinal check failed and the product code could not localise the
  // error to one byte and one bit. The data block is returned as transmitted.
  kUncorrectable,
};

/**
 * @brief One NABTS data packet, decoded
 *
 * The prefix is Hamming 8/4 throughout, so |valid| means all five bytes
 * survived correction and everything below can be relied on. A packet whose
 * prefix did not decode says nothing at all — not even which channel it was
 * addressed to — and is the one case a caller must check before reading the
 * rest.
 */
struct NabtsPacket {
  /// All five prefix bytes survived Hamming 8/4 correction (§3.2.2).
  bool valid = false;

  /// Packet address P, which §3.2.3 makes identical to the data channel number.
  uint16_t channel = 0;

  /// Continuity index (§3.2.4), 0-15. Incremented per packet of a channel
  /// except on a synchronizing packet, where it need not follow.
  uint8_t continuity = 0;

  /// PS b2 (§3.2.5): this packet starts a data group.
  bool synchronizing = false;

  /// PS b4 (§3.2.5): the data block is not completely full of useful data.
  /// Note that §3.2.5 does not make this the marker of a group's last packet.
  bool not_full = false;

  NabtsSuffixKind suffix = NabtsSuffixKind::kNone;

  /// Data-block bytes carried, i.e. nabts_data_block_bytes(suffix). Zero for a
  /// bundle packet, which is counted and skipped.
  size_t data_length = 0;

  NabtsBlockIntegrity integrity = NabtsBlockIntegrity::kUnchecked;

  /// The data block, corrected where the product code could. Only the first
  /// |data_length| entries are meaningful.
  std::array<uint8_t, kNabtsMaxDataBlockBytes> data{};

  /// How sure the detector was of each data-block byte, 0 (the detector could
  /// as well have decided otherwise) to 255 (as clear-cut as an undamaged
  /// signal makes it). A byte apiece rather than the float the slicer measures:
  /// what this feeds is a weighting in a vote, not an arithmetic, and 256 levels
  /// are far more than that needs.
  std::array<uint8_t, kNabtsMaxDataBlockBytes> confidence{};

  /// Whether this packet contributes bytes to its group's data.
  bool carries_data() const { return valid && data_length > 0; }
};

/**
 * @brief Decode one packet as sliced from a data line
 *
 * @param packet Packet bytes in transmission coding, prefix first
 * @param length Bytes available; anything short of kNabtsPacketBytes yields an
 *               invalid packet rather than a partial read
 *
 * @param confidence Per-byte detector confidence for the same 33 bytes, or null
 *                   where none was measured — which is read as full confidence,
 *                   since a detector that cannot express doubt has not
 *                   expressed any (see NabtsPacket::confidence)
 *
 * Hamming-decodes the prefix, derives the data-block extent from PS, and runs
 * the suffix check over the block. Never throws and never reads past |length|.
 *
 * The product-code correction assumes the odd byte parity §3.3 requires of data
 * blocks in a type-zero data group. That is the only group type broadcast
 * teletext uses (§4.2.2) and the only one an FSS receiver accepts (§8.4.2.2),
 * but a packet on its own cannot prove its group's type: the group header is in
 * the synchronizing packet, which this may not be. A private-use group (GT =
 * 15) whose bytes do not carry parity therefore risks a correction it did not
 * ask for — bounded to one bit, and only when the longitudinal check has
 * already failed, so a clean packet of any group type is never touched.
 */
NabtsPacket nabts_decode_packet(
    const uint8_t* packet, size_t length,
    const TeletextPacketConfidence* confidence = nullptr);

}  // namespace tbc::vbi

#endif  // TBC_VBI_NABTS_PACKET_H
