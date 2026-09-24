#pragma once
// S04 §5.4：分片发送与重组（组 ID、序号、上限 8、60 tick 超时丢弃）。
#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "net/wire.hpp"

namespace ac::net {

inline constexpr std::size_t kMaxFragmentPayload = 1176u;  // 1200 - 8 - 12 - 4
inline constexpr uint32_t kFragmentTimeoutTicks = 60u;     // 3s（20 tick/s）

// 逻辑消息 -> 分片数：0 字节得 0 片；超上限（8 片 / 9408 字节）不算可分。
std::size_t fragmentCountFor(std::size_t messageBytes) noexcept;
bool isSplittable(std::size_t messageBytes) noexcept;

// 发送：逻辑消息 > kMaxFragmentPayload 时切成 ≤ kMaxFragments 片，
// 每片 = 通用包头 + （可靠时）可靠扩展头 + 分片头 + 载荷切片，type = 9，flags 置 moreFragments。
// 不可分时返回空 vector（由调用方按 §5.4 拒绝）。
std::vector<std::vector<uint8_t>> splitMessage(const PacketHeader& header, const ReliableExt& ext,
                                               std::span<const uint8_t> message, uint16_t fragId);

// 接收：重组键 = (session, channelType, fragId)。
struct FragmentKey {
  uint16_t session = 0u;
  uint8_t type = 0u;
  uint16_t fragId = 0u;

  bool operator==(const FragmentKey& other) const noexcept {
    return session == other.session && type == other.type && fragId == other.fragId;
  }
  bool operator<(const FragmentKey& other) const noexcept {
    if (session != other.session) return session < other.session;
    if (type != other.type) return type < other.type;
    return fragId < other.fragId;
  }
};

class Reassembler {
 public:
  enum class Status : uint8_t { kIncomplete = 0u, kComplete = 1u, kBadValue = 2u };

  // 按 fragIndex 落位；count > 8 或 index >= count 立即判 kBadValue（该组丢弃）。
  Status add(const FragmentKey& key, uint16_t index, uint16_t count, std::span<const uint8_t> payload,
             uint32_t nowTick, std::vector<uint8_t>& out);

  // 组超时回收：60 tick 未收齐即丢弃，返回丢弃的组数。
  std::size_t expire(uint32_t nowTick);

  std::size_t activeGroups() const noexcept { return groups_.size(); }
  std::size_t timedOutCount() const noexcept { return timedOutCount_; }
  std::size_t completedCount() const noexcept { return completedCount_; }
  void clear();

 private:
  struct Group {
    uint16_t count = 0u;
    uint32_t firstTick = 0u;
    std::size_t receivedCount = 0u;
    std::vector<std::vector<uint8_t>> slices;
    std::vector<bool> received;
  };

  std::map<FragmentKey, Group> groups_;
  std::size_t timedOutCount_ = 0u;
  std::size_t completedCount_ = 0u;
};

}  // namespace ac::net
