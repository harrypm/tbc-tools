/*
 * File:        nabts_data_group.cpp
 * Module:      tbc-library (shared VBI services)
 * Purpose:     NABTS data group reassembly implementation (CEA-516 §4)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * Ported from decode-orc (github.com/decode-orc/decode-orc,
 * orc/plugins/stages/nabts_sink/nabts_data_group.cpp) at tag v2.7.2 (commit
 * fef0115a). Algorithmic bodies are intact; orc:: -> tbc::vbi::, and the
 * spdlog/fmt/fmt.h dependency in NabtsGroupStats::summary() was replaced with
 * dependency-free snprintf-based formatting (tbc-tools uses Qt logging, not
 * spdlog).
 */

#include "nabts_data_group.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

#include "vbi_nominnmax_undef.h"
namespace tbc::vbi {

namespace {

/// Two Hamming nibbles as one byte, the first the more significant — the
/// concatenation §4.2.5 and §4.2.6 specify for S1,S2 and F1,F2.
constexpr uint16_t concat_nibbles(int high, int low) {
  return static_cast<uint16_t>((high << 4) | low);
}

}  // namespace

NabtsGroupHeader nabts_decode_group_header(const uint8_t* bytes,
                                           size_t length) {
  NabtsGroupHeader header;
  if (bytes == nullptr || length < kNabtsGroupHeaderBytes) {
    return header;
  }

  // §4.2.1: all eight are Hamming 8/4. A group whose size did not decode has no
  // known extent, and one whose type did not decode cannot be routed, so the
  // header is all-or-nothing rather than partially trusted.
  std::array<int, kNabtsGroupHeaderBytes> nibbles{};
  for (size_t i = 0; i < kNabtsGroupHeaderBytes; ++i) {
    nibbles[i] = teletext_hamming84_decode(bytes[i]);
    if (nibbles[i] < 0) {
      return header;
    }
  }

  header.valid = true;
  header.type = static_cast<uint8_t>(nibbles[0]);
  header.continuity = static_cast<uint8_t>(nibbles[1]);
  header.repetition = static_cast<uint8_t>(nibbles[2]);
  header.further_blocks = concat_nibbles(nibbles[3], nibbles[4]);
  header.final_block_bytes = concat_nibbles(nibbles[5], nibbles[6]);
  header.routing = static_cast<uint8_t>(nibbles[7]);
  return header;
}

std::string NabtsGroupStats::summary() const {
  // Replaced decode-orc's fmt::format with snprintf so this stays free of the
  // spdlog dependency tbc-tools does not carry. The report is human-readable
  // diagnostics, so the exact formatting is not load-bearing.
  std::string out;
  out.resize(512);
  int written = std::snprintf(out.data(), out.size(),
      "Data group reassembly\n"
      "  Packets:       %llu seen, %llu prefix rejected, %llu orphaned, %llu bundle\n"
      "  Groups:        %llu completed, %llu superseded, %llu unfinished\n",
      static_cast<unsigned long long>(packets_seen),
      static_cast<unsigned long long>(prefix_failures),
      static_cast<unsigned long long>(orphan_packets),
      static_cast<unsigned long long>(bundle_packets),
      static_cast<unsigned long long>(groups_completed),
      static_cast<unsigned long long>(groups_superseded),
      static_cast<unsigned long long>(groups_unfinished));
  if (written < 0) {
    return "Data group reassembly\n";
  }
  out.resize(static_cast<size_t>(written));

  if (header_failures > 0 || oversized_groups > 0 || refused_groups > 0) {
    char buf[256];
    int w2 = std::snprintf(buf, sizeof(buf),
        "  Refused:       %llu bad header, %llu oversized, %llu over the open-group limit\n",
        static_cast<unsigned long long>(header_failures),
        static_cast<unsigned long long>(oversized_groups),
        static_cast<unsigned long long>(refused_groups));
    if (w2 > 0) {
      out.append(buf, static_cast<size_t>(w2));
    }
  }
  if (non_teletext_groups > 0) {
    char buf2[160];
    int w3 = std::snprintf(buf2, sizeof(buf2),
        "  Not teletext:  %llu completed groups of a type other than zero\n",
        static_cast<unsigned long long>(non_teletext_groups));
    if (w3 > 0) {
      out.append(buf2, static_cast<size_t>(w3));
    }
  }
  return out;
}

void NabtsGroupAssembler::append_block(OpenGroup& group,
                                       const NabtsPacket& packet) {
  if (packet.data_length == 0) {
    return;  // A bundle packet: counted by the caller, contributes no bytes.
  }
  group.last_nonzero_offset = group.stream.size();
  group.last_nonzero_length = packet.data_length;
  group.stream.insert(
      group.stream.end(), packet.data.begin(),
      packet.data.begin() + static_cast<ptrdiff_t>(packet.data_length));
  // Bytes that arrived are present only while the group can still say where
  // they belong; past a continuity index the header contradicts, nothing can.
  group.present.insert(group.present.end(), packet.data_length,
                       group.placeable ? 1 : 0);
  group.confidence.insert(
      group.confidence.end(), packet.confidence.begin(),
      packet.confidence.begin() + static_cast<ptrdiff_t>(packet.data_length));
}

void NabtsGroupAssembler::append_hole(OpenGroup& group, uint32_t blocks) {
  const size_t bytes = static_cast<size_t>(blocks) * group.nominal_block_bytes;
  if (bytes == 0) {
    return;
  }
  // Zero rather than anything meaningful. The mask keeps these out of the
  // record catalogue's vote, and for anything that reads the bytes themselves
  // NUL is X3.110 §6.1.4's transparent control — no presentation effect — so a
  // hole in NAPLPS code costs the drawing nothing beyond what was actually
  // lost.
  group.stream.insert(group.stream.end(), bytes, 0);
  group.present.insert(group.present.end(), bytes, 0);
  group.confidence.insert(group.confidence.end(), bytes, 0);
}

void NabtsGroupAssembler::emit(uint16_t channel, OpenGroup& group,
                               NabtsGroupOutcome outcome) {
  NabtsDataGroup out;
  out.channel = channel;
  out.header = group.header;
  out.outcome = outcome;
  out.packets = group.packets;
  out.blocks_corrected = group.blocks_corrected;
  out.blocks_damaged = group.blocks_damaged;
  out.packets_lost = group.packets_lost;

  // §4.2.6 and §8.4.2.6: F1,F2 is the useful length of the final non-zero data
  // block, and everything before that block is full. Greater than a block is
  // read as full, zero discards the block entirely.
  size_t useful_end = group.last_nonzero_offset + group.last_nonzero_length;
  if (group.header.final_block_bytes == 0) {
    useful_end = group.last_nonzero_offset;
  } else if (group.header.final_block_bytes < group.last_nonzero_length) {
    useful_end = group.last_nonzero_offset + group.header.final_block_bytes;
  }
  useful_end = (std::min)(useful_end, group.stream.size());

  // The header bytes are part of the block they arrived in but not part of the
  // group's data (§4.2.1, §5.1): the record starts where they end.
  if (useful_end > kNabtsGroupHeaderBytes) {
    out.data.assign(
        group.stream.begin() + static_cast<ptrdiff_t>(kNabtsGroupHeaderBytes),
        group.stream.begin() + static_cast<ptrdiff_t>(useful_end));
    // Trimmed identically, so an index into one is an index into the other.
    out.present.assign(
        group.present.begin() + static_cast<ptrdiff_t>(kNabtsGroupHeaderBytes),
        group.present.begin() + static_cast<ptrdiff_t>(useful_end));
    out.confidence.assign(
        group.confidence.begin() +
            static_cast<ptrdiff_t>(kNabtsGroupHeaderBytes),
        group.confidence.begin() + static_cast<ptrdiff_t>(useful_end));
  }

  switch (outcome) {
    case NabtsGroupOutcome::kComplete:
      ++stats_.groups_completed;
      if (group.header.type != kNabtsBroadcastGroupType) {
        ++stats_.non_teletext_groups;
      }
      break;
    case NabtsGroupOutcome::kSuperseded:
      ++stats_.groups_superseded;
      break;
    case NabtsGroupOutcome::kUnfinished:
      ++stats_.groups_unfinished;
      break;
  }

  if (callback_) {
    callback_(out);
  }
}

void NabtsGroupAssembler::begin_group(const NabtsPacket& packet) {
  const NabtsGroupHeader header =
      nabts_decode_group_header(packet.data.data(), packet.data_length);
  if (!header.valid) {
    ++stats_.header_failures;
    return;
  }
  if (header.further_blocks > kNabtsMaxFurtherBlocks) {
    // §8.4.2.5 caps the group at 68 packets. A larger claim is a misread header
    // or a service this cannot follow; either way it is refused before it can
    // reserve the memory it asked for.
    ++stats_.oversized_groups;
    return;
  }

  // A synchronizing packet for a channel that already has one open ends that
  // group (§4.1: the next group's start is the previous one's end).
  const auto existing = open_.find(packet.channel);
  if (existing != open_.end()) {
    emit(packet.channel, existing->second, NabtsGroupOutcome::kSuperseded);
    open_.erase(existing);
  } else if (open_.size() >= kNabtsMaxOpenGroups) {
    ++stats_.refused_groups;
    return;
  }

  OpenGroup group;
  group.header = header;
  group.last_continuity = packet.continuity;
  group.packets = 1;
  // Reserved from the header's own claim rather than the standard's ceiling, so
  // a two-packet group costs two packets' worth.
  group.stream.reserve((std::min)(kNabtsMaxGroupBytes,
                                static_cast<size_t>(header.further_blocks + 1) *
                                    kNabtsMaxDataBlockBytes));
  // The width a lost block of this group is assumed to have had: §4.2 has a
  // group's packets share a suffix code, and the synchronizing packet is the
  // one whose code is certainly this group's.
  group.nominal_block_bytes = packet.data_length;
  if (packet.integrity == NabtsBlockIntegrity::kCorrected) {
    ++group.blocks_corrected;
  } else if (packet.integrity == NabtsBlockIntegrity::kUncorrectable) {
    ++group.blocks_damaged;
  }
  append_block(group, packet);

  // S1,S2 = 0 is a whole group in one packet, so it is complete on arrival.
  if (header.further_blocks == 0) {
    emit(packet.channel, group, NabtsGroupOutcome::kComplete);
    return;
  }
  open_.emplace(packet.channel, std::move(group));
}

void NabtsGroupAssembler::extend_group(const NabtsPacket& packet) {
  const auto it = open_.find(packet.channel);
  if (it == open_.end()) {
    // No group open on this channel. Normal at the head of a recording, which
    // starts part way through whatever was being transmitted.
    ++stats_.orphan_packets;
    return;
  }
  OpenGroup& group = it->second;

  // §3.2.4: the continuity index increments once per packet of the channel, so
  // the gap is how many never arrived. It wraps at 16, which bounds what can be
  // detected: a loss of exactly 16 packets reads as none.
  const uint8_t expected = static_cast<uint8_t>((group.last_continuity + 1) %
                                                kNabtsContinuityModulus);
  if (packet.continuity != expected) {
    const uint8_t gap = static_cast<uint8_t>(
        (packet.continuity + kNabtsContinuityModulus - expected) %
        kNabtsContinuityModulus);
    group.packets_lost += gap;
    // The lost packets carried blocks this group was promised, so they count
    // towards its size — otherwise a group missing packets would never reach
    // its block count and would only ever end superseded.
    const uint32_t room = static_cast<uint32_t>(group.header.further_blocks) -
                          group.further_blocks_seen;
    group.further_blocks_seen = static_cast<uint16_t>(std::min<uint32_t>(
        group.further_blocks_seen + gap, group.header.further_blocks));
    // A gap wider than the group has blocks left is not a loss the header can
    // account for: §3.2.4's index wraps at 16, so a damaged continuity nibble
    // reads as a gap just as a real loss does, and only the header says which
    // is possible. Where it says the gap cannot be real, nothing after it can
    // be placed.
    if (gap > room) {
      group.placeable = false;
    } else {
      append_hole(group, gap);
    }
  }
  group.last_continuity = packet.continuity;
  ++group.packets;

  if (packet.integrity == NabtsBlockIntegrity::kCorrected) {
    ++group.blocks_corrected;
  } else if (packet.integrity == NabtsBlockIntegrity::kUncorrectable) {
    ++group.blocks_damaged;
  }

  append_block(group, packet);
  // §4.2.5 counts every block towards the size, "including any Data Blocks of
  // zero length that occur with a 28-byte Suffix", so a bundle packet advances
  // this even though it contributed no bytes.
  ++group.further_blocks_seen;

  if (group.further_blocks_seen >= group.header.further_blocks) {
    emit(packet.channel, group, NabtsGroupOutcome::kComplete);
    open_.erase(it);
  }
}

void NabtsGroupAssembler::add_packet(const NabtsPacket& packet) {
  ++stats_.packets_seen;
  if (!packet.valid) {
    ++stats_.prefix_failures;
    return;
  }
  if (packet.suffix == NabtsSuffixKind::kBundle) {
    ++stats_.bundle_packets;
  }

  if (packet.synchronizing) {
    begin_group(packet);
  } else {
    extend_group(packet);
  }
}

void NabtsGroupAssembler::flush() {
  for (auto& entry : open_) {
    emit(entry.first, entry.second, NabtsGroupOutcome::kUnfinished);
  }
  open_.clear();
}

}  // namespace tbc::vbi
