#pragma once
// S15 §15.4 D4：延迟发送/延迟投递用的定长包队列。
//
// 用途：ac_bot 的 --latency（半 RTT 出向/入向各延迟一次）。旧实现每个包 new 一个
// std::vector<uint8_t>，30Hz × 4 机器人在整跑次里会做上万次堆分配；这里改成定长环形数组：
//   · 每包一次 memcpy 进定长缓冲，容量满则丢新包并计数（丢包必须可观察，不静默扩容）；
//   · 纯 header、零堆分配（alloc_test 的 alloc_bot_packet_queue_is_heap_free 用既有
//     AllocationScope 口径证明）—— 因此本头文件不含任何容器，只含 std::array 成员。
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "net/wire.hpp"  // kMaxPacketBytes：单个 UDP 载荷的字节上限（§5 冻结，1200B）

namespace ac::net {

// 一条待发/待投递的包：到期时刻 + 载荷长度 + 定长载荷。
struct QueuedPacket {
  std::uint64_t dueMs = 0u;
  std::size_t size = 0u;
  std::array<std::uint8_t, kMaxPacketBytes> bytes{};
};

class DelayedPacketQueue {
 public:
  // 64 = 半 RTT 100ms × 30Hz ≈ 4 包的 16 倍余量；容量是纯实现上限，不参与 §5 判定。
  static constexpr std::size_t kCapacity = 64u;

  // 入队失败（容量满或包超上限）返回 false 并 ++droppedCount，调用方按丢包记账。
  bool push(std::uint64_t dueMs, const std::uint8_t* bytes, std::size_t size) noexcept {
    if (bytes == nullptr || size > kMaxPacketBytes || count_ >= kCapacity) {
      ++dropped_;
      return false;
    }
    QueuedPacket& slot = atMutable(count_);
    slot.dueMs = dueMs;
    slot.size = size;
    if (size > 0u) std::memcpy(slot.bytes.data(), bytes, size);
    ++count_;
    return true;
  }

  std::size_t count() const noexcept { return count_; }
  std::size_t droppedCount() const noexcept { return dropped_; }
  const QueuedPacket& at(std::size_t index) const noexcept {
    return entries_[(head_ + index) % kCapacity];
  }

  // 删除第 index 条（保持其余条目的原顺序）。
  void eraseAt(std::size_t index) noexcept {
    for (std::size_t i = index; i + 1u < count_; ++i) atMutable(i) = atMutable(i + 1u);
    --count_;
    if (count_ == 0u) head_ = 0u;
  }

 private:
  QueuedPacket& atMutable(std::size_t index) noexcept {
    return entries_[(head_ + index) % kCapacity];
  }

  std::array<QueuedPacket, kCapacity> entries_{};
  std::size_t head_ = 0u;
  std::size_t count_ = 0u;
  std::size_t dropped_ = 0u;
};

}  // namespace ac::net
