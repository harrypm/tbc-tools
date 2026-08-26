/*
 * File:        nabts_data_group.h
 * Module:      tbc-library (shared VBI services)
 * Purpose:     Reassemble NABTS data groups from a packet stream (CEA-516 §4)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/nabts_sink/nabts_data_group.h) at tag v2.7.2 (commit
 * fef0115a). Algorithmic bodies are intact; orc:: -> tbc::vbi::.
 */

#ifndef TBC_VBI_NABTS_DATA_GROUP_H
#define TBC_VBI_NABTS_DATA_GROUP_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "nabts_packet.h"

namespace tbc::vbi {

// CEA-516 §4.2.1: the data group header is eight Hamming 8/4 bytes — GT, GC,
// GR, S1, S2, F1, F2, GN — at the head of the synchronizing packet's data
// block.
constexpr size_t kNabtsGroupHeaderBytes = 8;

// CEA-516 §4.2.2: zero is the group type broadcast teletext uses, and §8.4.2.2
// is the only type an FSS receiver accepts. Fifteen is reserved for the service
// provider's private use and will never be standardized.
constexpr uint8_t kNabtsBroadcastGroupType = 0;
constexpr uint8_t kNabtsPrivateGroupType = 15;

// CEA-516 §8.4.2.5: at most 67 further blocks, so 68 packets, so 1904 bytes
// when no suffix is used. Both are stated because both are useful: the block
// count is what S1,S2 is checked against, and the byte count is the allocation
// bound that follows from it.
constexpr uint16_t kNabtsMaxFurtherBlocks = 67;
constexpr size_t kNabtsMaxGroupBytes = 1904;

// Groups held open at once. A group stays open from its synchronizing packet
// until its last block arrives, and packets of different channels interleave
// freely, so one per channel may legitimately be in flight. §3.2.3 permits 4096
// channels; a real service uses a handful and a misread prefix invents the
// rest, so this is what stops noise from opening thousands of buffers. At
// kNabtsMaxGroupBytes each, the ceiling is about 61 kB.
constexpr size_t kNabtsMaxOpenGroups = 32;

/// The eight bytes of CEA-516 §4.2, decoded.
struct NabtsGroupHeader {
  /// All eight bytes survived Hamming 8/4 correction.
  bool valid = false;
  /// GT (§4.2.2).
  uint8_t type = 0;
  /// GC (§4.2.3). §8.4.2.3 lets a service leave this un-incremented, so it is
  /// reported rather than relied on.
  uint8_t continuity = 0;
  /// GR (§4.2.4): non-zero means the group is expected to repeat.
  uint8_t repetition = 0;
  /// S1,S2 (§4.2.5): data blocks following the synchronizing packet's own.
  uint16_t further_blocks = 0;
  /// F1,F2 (§4.2.6): bytes of useful data in the final non-zero data block.
  uint16_t final_block_bytes = 0;
  /// GN (§4.2.7). §8.4.2.7 has FSS receivers ignore it; reported for
  /// diagnostics.
  uint8_t routing = 0;
};

/**
 * @brief Decode a data group header
 *
 * @param bytes Head of the synchronizing packet's data block
 * @param length Bytes available; fewer than kNabtsGroupHeaderBytes yields an
 * invalid header rather than a partial read
 */
NabtsGroupHeader nabts_decode_group_header(const uint8_t* bytes, size_t length);

/// Why a group ended.
enum class NabtsGroupOutcome {
  /// Every block S1,S2 promised arrived.
  kComplete,
  /// A synchronizing packet for the same channel arrived first, so this group
  /// will never be finished. Its bytes are handed on anyway — a truncated
  /// record header still identifies its record.
  kSuperseded,
  /// The pass ended with the group still open.
  kUnfinished,
};

/// One reassembled data group.
struct NabtsDataGroup {
  /// Channel the group's packets were addressed to (§3.2.3).
  uint16_t channel = 0;
  NabtsGroupHeader header;
  NabtsGroupOutcome outcome = NabtsGroupOutcome::kComplete;

  /// Group data: everything after the eight header bytes, trimmed to the
  /// useful length F1,F2 states (§4.2.6, §8.4.2.6). For a type-zero group this
  /// is exactly one teletext record (§5.1).
  std::vector<uint8_t> data;

  /// One entry per byte of @ref data: non-zero where that byte arrived, zero
  /// where it stands in for one a lost packet carried. Empty when the group
  /// lost nothing, which saves carrying a mask that is all ones.
  ///
  /// §3.2.4's continuity index says how many packets never arrived, so a hole
  /// can be held open at the width the lost blocks would have filled instead of
  /// letting the bytes after it close up. That keeps every later byte at the
  /// offset the record gave it, which is what lets two copies of one record be
  /// compared position for position however much each of them lost. The width
  /// is an estimate — the lost packets' own suffix codes went with them, so the
  /// group's nominal block length stands in — and where the continuity index
  /// disagrees with the block count the header declared it is not trusted at
  /// all, and the rest of the group is marked absent rather than misplaced.
  std::vector<uint8_t> present;

  /// One entry per byte of @ref data: how sure the detector was of it, on the
  /// 0-255 scale of NabtsPacket::confidence. Zero where the byte never arrived.
  /// Empty when nothing measured any of them, which is read as full confidence
  /// throughout.
  std::vector<uint8_t> confidence;

  /// Packets that contributed, including the synchronizing packet.
  uint32_t packets = 0;
  /// Data blocks whose suffix check the product code had to repair.
  uint32_t blocks_corrected = 0;
  /// Data blocks whose suffix check failed and could not be repaired. Their
  /// bytes are present as transmitted, so a caller may still get a usable
  /// record out of a group with a few of these.
  uint32_t blocks_damaged = 0;
  /// Packets the continuity index says never arrived (§3.2.4). A group with any
  /// is missing bytes from somewhere in the middle of |data|.
  uint32_t packets_lost = 0;

  /// Whether the group arrived whole and undamaged.
  bool intact() const {
    return outcome == NabtsGroupOutcome::kComplete && packets_lost == 0 &&
           blocks_damaged == 0;
  }
};

/// What a pass over a packet stream made of it.
struct NabtsGroupStats {
  uint64_t packets_seen = 0;
  /// Packets whose prefix did not survive Hamming 8/4 (§3.2.2).
  uint64_t prefix_failures = 0;
  /// Synchronizing packets whose group header did not decode (§4.2.1).
  uint64_t header_failures = 0;
  /// Synchronizing packets claiming more than §8.4.2.5 permits.
  uint64_t oversized_groups = 0;
  /// Synchronizing packets refused because kNabtsMaxOpenGroups were already
  /// open.
  uint64_t refused_groups = 0;
  /// Standard packets arriving on a channel with no group open — the normal
  /// state of affairs when a recording starts part way through a group.
  uint64_t orphan_packets = 0;
  /// Bundle packets (§3.4 PS 1,1): counted, continuity consumed, contents
  /// skipped.
  uint64_t bundle_packets = 0;
  uint64_t groups_completed = 0;
  uint64_t groups_superseded = 0;
  uint64_t groups_unfinished = 0;
  /// Completed groups whose type was not zero, and so not broadcast teletext
  /// (§4.2.2). Delivered to the callback, which decides.
  uint64_t non_teletext_groups = 0;

  /// Human-readable summary for the stage report.
  std::string summary() const;
};

/**
 * @brief Turns a temporally ordered packet stream into data groups
 *
 * Fed packets in the order they were broadcast — frame, then field, then
 * ascending line, which is the order a sink emits them — and calls back
 * once per group as it completes.
 *
 * Groups of different channels interleave, so one is held open per channel
 * (§4.1: a group is the packets of one packet address). Loss is detected from
 * the continuity index, which §3.2.4 increments per packet of a channel; a gap
 * is recorded and the group carries on, because the bytes that did arrive are
 * still worth having.
 *
 * Not thread safe, and deliberately so: the assembler's whole value is that it
 * sees the stream in transmission order, which one thread walking the emitted
 * packets is what guarantees.
 */
class NabtsGroupAssembler {
 public:
  using GroupCallback = std::function<void(const NabtsDataGroup&)>;

  /// Called once per group, as it ends. A group whose type is not zero is
  /// delivered too: §4.2.2 reserves other types rather than forbidding them,
  /// and what to do with one is the caller's decision to make.
  void set_group_callback(GroupCallback callback) {
    callback_ = std::move(callback);
  }

  /// Take in one packet. An invalid packet is counted and dropped: without a
  /// channel there is no group to file it under.
  void add_packet(const NabtsPacket& packet);

  /// End the pass: every group still open is reported as unfinished.
  /// Idempotent.
  void flush();

  const NabtsGroupStats& stats() const { return stats_; }

 private:
  struct OpenGroup {
    NabtsGroupHeader header;
    /// Every data-block byte the group has yielded, the synchronizing packet's
    /// eight header bytes included, so an offset into this is an offset into
    /// the group as §4.2.6 counts it.
    std::vector<uint8_t> stream;
    /// Parallel to @ref stream: whether each byte arrived or stands in for one
    /// a lost packet carried (see NabtsDataGroup::present).
    std::vector<uint8_t> present;
    /// Parallel to @ref stream: detector confidence per byte, zero in a hole.
    std::vector<uint8_t> confidence;
    /// Data-block bytes the synchronizing packet carried, which is the width a
    /// lost block is assumed to have had. §4.2 has a group's packets share a
    /// suffix code in practice, and a lost packet's own is lost with it.
    size_t nominal_block_bytes = 0;
    /// False once the continuity index has said something the group header
    /// contradicts, after which no byte of this group can be placed.
    bool placeable = true;
    /// Blocks after the synchronizing packet's own.
    uint16_t further_blocks_seen = 0;
    /// Where the last data block with any bytes in it started, and how long it
    /// was — which is what F1,F2 is a length within (§4.2.6). The final
    /// non-zero block need not be the last packet: zero-length blocks with
    /// 28-byte suffixes may follow it.
    size_t last_nonzero_offset = 0;
    size_t last_nonzero_length = 0;
    uint8_t last_continuity = 0;
    uint32_t packets = 0;
    uint32_t blocks_corrected = 0;
    uint32_t blocks_damaged = 0;
    uint32_t packets_lost = 0;
  };

  void append_block(OpenGroup& group, const NabtsPacket& packet);
  void append_hole(OpenGroup& group, uint32_t blocks);
  void emit(uint16_t channel, OpenGroup& group, NabtsGroupOutcome outcome);
  void begin_group(const NabtsPacket& packet);
  void extend_group(const NabtsPacket& packet);

  std::map<uint16_t, OpenGroup> open_;
  NabtsGroupStats stats_;
  GroupCallback callback_;
};

}  // namespace tbc::vbi

#endif  // TBC_VBI_NABTS_DATA_GROUP_H
