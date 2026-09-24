#pragma once
// S04 §5.7：测试用内存总线——丢包率、单向延迟与抖动、乱序、按毫秒推进的 pump。
// 没有真实网络也能复现丢包/延迟/乱序，后续所有网络集成测试都复用它。
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <vector>

#include "core/rng.hpp"

namespace ac::net {

struct EndpointId {
  uint32_t id = 0u;  // 测试内自定的端点编号，与 UDP 端口无关

  bool operator==(const EndpointId& other) const noexcept { return id == other.id; }
};

class MemoryTransport {
 public:
  // §5.7：抽样走 fx 流（表现层流），绝不消费 ai/spawn，保证对拍可复现。
  explicit MemoryTransport(uint32_t seed = 0x5EEDu) noexcept
      : rng_(ac::createRng(seed, ac::RngStream::kFx)) {}

  void setLossRate(double rate) noexcept { lossRate_ = clamp01(rate); }
  void setLatencyMs(uint32_t base, uint32_t jitter) noexcept {
    latencyMs_ = base;
    jitterMs_ = jitter;
  }
  void setReorderRate(double rate) noexcept { reorderRate_ = clamp01(rate); }

  // send 没有时间参数：以最近一次 pump 的 nowMs 作为发送时刻（§5.7 的签名如此）。
  void send(EndpointId from, EndpointId to, std::span<const uint8_t> bytes);

  void pump(uint32_t nowMs,
            const std::function<void(EndpointId, std::span<const std::uint8_t>)>& onDeliver);

  std::size_t droppedCount() const noexcept { return droppedCount_; }
  std::size_t sentCount() const noexcept { return sentCount_; }
  std::size_t deliveredCount() const noexcept { return deliveredCount_; }
  std::size_t inFlightCount() const noexcept { return inFlight_.size(); }
  uint32_t nowMs() const noexcept { return nowMs_; }
  void clear();

 private:
  struct InFlight {
    EndpointId from{};
    EndpointId to{};
    std::vector<uint8_t> bytes;
    uint32_t deliverAtMs = 0u;
    uint64_t seq = 0u;
  };

  static double clamp01(double value) noexcept {
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
  }

  ac::Rng rng_{};
  std::deque<InFlight> inFlight_{};
  double lossRate_ = 0.0;
  double reorderRate_ = 0.0;
  uint32_t latencyMs_ = 0u;
  uint32_t jitterMs_ = 0u;
  uint32_t nowMs_ = 0u;
  uint64_t nextSeq_ = 1u;
  std::size_t sentCount_ = 0u;
  std::size_t droppedCount_ = 0u;
  std::size_t deliveredCount_ = 0u;
};

inline void MemoryTransport::send(EndpointId from, EndpointId to, std::span<const uint8_t> bytes) {
  ++sentCount_;
  if (lossRate_ > 0.0 && rng_.nextDouble() < lossRate_) {
    ++droppedCount_;
    return;
  }
  InFlight packet{};
  packet.from = from;
  packet.to = to;
  packet.bytes.assign(bytes.begin(), bytes.end());
  uint32_t delay = latencyMs_;
  if (jitterMs_ > 0u) delay += rng_.nextU32() % (jitterMs_ + 1u);
  packet.deliverAtMs = nowMs_ + delay;
  packet.seq = nextSeq_++;

  // 乱序：按概率与同一条流上最近入队的包交换送达时刻（"相邻包交换"）。
  if (reorderRate_ > 0.0 && !inFlight_.empty() && rng_.nextDouble() < reorderRate_) {
    for (auto it = inFlight_.rbegin(); it != inFlight_.rend(); ++it) {
      if (it->from.id == from.id && it->to.id == to.id) {
        const uint32_t swapped = it->deliverAtMs;
        it->deliverAtMs = packet.deliverAtMs;
        packet.deliverAtMs = swapped;
        break;
      }
    }
  }
  inFlight_.push_back(std::move(packet));
}

inline void MemoryTransport::pump(
    uint32_t nowMs, const std::function<void(EndpointId, std::span<const std::uint8_t>)>& onDeliver) {
  nowMs_ = nowMs;
  std::stable_sort(inFlight_.begin(), inFlight_.end(), [](const InFlight& a, const InFlight& b) {
    if (a.deliverAtMs != b.deliverAtMs) return a.deliverAtMs < b.deliverAtMs;
    return a.seq < b.seq;  // 同刻到达的按发送序交付
  });
  while (!inFlight_.empty() && inFlight_.front().deliverAtMs <= nowMs) {
    const InFlight packet = std::move(inFlight_.front());
    inFlight_.pop_front();  // 先出队：onDeliver 里回包会再次 send
    ++deliveredCount_;
    onDeliver(packet.from, std::span<const uint8_t>(packet.bytes.data(), packet.bytes.size()));
  }
}

inline void MemoryTransport::clear() {
  inFlight_.clear();
  sentCount_ = 0u;
  droppedCount_ = 0u;
  deliveredCount_ = 0u;
  nowMs_ = 0u;
  nextSeq_ = 1u;
}

}  // namespace ac::net
