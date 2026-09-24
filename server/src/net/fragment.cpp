// S04 §5.4：分片与重组的纯逻辑实现。
#include "net/fragment.hpp"

#include <algorithm>

namespace ac::net {

std::size_t fragmentCountFor(std::size_t messageBytes) noexcept {
  if (messageBytes == 0u) return 0u;
  return (messageBytes + kMaxFragmentPayload - 1u) / kMaxFragmentPayload;
}

bool isSplittable(std::size_t messageBytes) noexcept {
  if (messageBytes == 0u || messageBytes > kMaxLogicalMessageBytes) return false;
  return fragmentCountFor(messageBytes) <= kMaxFragments;
}

std::vector<std::vector<uint8_t>> splitMessage(const PacketHeader& header, const ReliableExt& ext,
                                               std::span<const uint8_t> message, uint16_t fragId) {
  std::vector<std::vector<uint8_t>> slices;
  const std::size_t count = fragmentCountFor(message.size());
  if (count == 0u || count > kMaxFragments) return slices;

  PacketHeader sliceHeader = header;
  sliceHeader.type = static_cast<uint8_t>(PacketType::kFragment);
  sliceHeader.flags = static_cast<uint16_t>((header.flags & kFlagReliable) | kFlagMoreFragments);
  const bool hasExt = (sliceHeader.flags & kFlagReliable) != 0u;
  const std::size_t offset = payloadOffset(sliceHeader);

  slices.reserve(count);
  for (std::size_t index = 0u; index < count; ++index) {
    const std::size_t begin = index * kMaxFragmentPayload;
    const std::size_t end = std::min(begin + kMaxFragmentPayload, message.size());
    std::vector<uint8_t> packet(offset + (end - begin), 0u);
    ByteWriter writer(packet.data(), packet.size());
    writeHeader(writer, sliceHeader);
    if (hasExt) writeReliableExt(writer, ext);
    writeFragmentHeader(writer,
                        FragmentHeader{fragId, static_cast<uint8_t>(index), static_cast<uint8_t>(count)});
    writer.writeBytes(message.data() + begin, end - begin);
    slices.push_back(std::move(packet));
  }
  return slices;
}

Reassembler::Status Reassembler::add(const FragmentKey& key, uint16_t index, uint16_t count,
                                     std::span<const uint8_t> payload, uint32_t nowTick,
                                     std::vector<uint8_t>& out) {
  out.clear();
  if (count == 0u || count > kMaxFragments || index >= count) {
    groups_.erase(key);
    return Status::kBadValue;
  }
  Group& group = groups_[key];
  if (group.count == 0u) {
    group.count = count;
    group.firstTick = nowTick;
    group.slices.assign(count, {});
    group.received.assign(count, false);
    group.receivedCount = 0u;
  } else if (group.count != count) {
    groups_.erase(key);  // 同组片数前后不一致：整组丢弃
    return Status::kBadValue;
  }
  if (!group.received[index]) {
    group.received[index] = true;
    group.slices[index].assign(payload.begin(), payload.end());
    ++group.receivedCount;
  }
  if (group.receivedCount < group.count) return Status::kIncomplete;

  std::size_t total = 0u;
  for (const std::vector<uint8_t>& slice : group.slices) total += slice.size();
  out.reserve(total);
  for (const std::vector<uint8_t>& slice : group.slices) {
    out.insert(out.end(), slice.begin(), slice.end());
  }
  groups_.erase(key);
  ++completedCount_;
  return Status::kComplete;
}

std::size_t Reassembler::expire(uint32_t nowTick) {
  std::size_t dropped = 0u;
  for (auto it = groups_.begin(); it != groups_.end();) {
    if (nowTick - it->second.firstTick >= kFragmentTimeoutTicks) {
      it = groups_.erase(it);
      ++dropped;
    } else {
      ++it;
    }
  }
  timedOutCount_ += dropped;
  return dropped;
}

void Reassembler::clear() {
  groups_.clear();
  timedOutCount_ = 0u;
  completedCount_ = 0u;
}

}  // namespace ac::net
