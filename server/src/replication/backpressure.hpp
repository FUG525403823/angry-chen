#pragma once
// S12 §5（§3 交付物 3）：出站队列字节账、超预算丢快照、连续丢弃判定慢客户端。
//
// 冻结项：预算 64 KiB（单连接）、单帧上限 2048 B、连续 60 帧丢弃 → Disconnect(reason = 7)；
// 丢弃只作用于**快照通道**（不可靠），事件通道是可靠通道，永不丢。
#include <cstddef>
#include <cstdint>

#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "net/keepalive.hpp"

namespace ac::replication {

inline constexpr std::size_t kOutboundBacklogBytes = ac::net::kOutboundBacklogBytes;  // 65536
static_assert(kOutboundBacklogBytes == 65536u, "§5：单连接队列预算 64 KiB（与 S04 §5 同名同值）");
inline constexpr std::size_t kBacklogDownshiftBytes = kOutboundBacklogBytes / 2u;     // 32 KiB 降档信号
inline constexpr std::size_t kMaxSnapshotBytes = 2048u;                               // §5 单快照上限（含包头）
inline constexpr uint32_t kSlowClientDropFrames = 60u;                                // 连续 60 帧（3 s）
inline constexpr uint8_t kDisconnectReasonSlowConsumer = 7u;                          // slowConsumer

enum class QueueVerdict : uint8_t {
  kEnqueue = 0,   // 已入队
  kDropSnapshot,  // 超预算或超单帧上限：本帧不入队（事件通道不受影响）
  kDisconnect,    // 连续丢弃达 60 帧：Disconnect(7)，随后进入 30 s 宽限期
};

struct OutboundBudget {
  std::size_t queuedBytes = 0u;  // 已拷贝未写完成的字节；健康连接稳态应接近 0
  uint64_t snapshotBytesTotal = 0u;
  uint32_t snapshotCount = 0u;
  uint32_t droppedSnapshots = 0u;
  uint32_t consecutiveDrops = 0u;
  uint32_t maxSnapshotBytes = 0u;
  uint32_t eventsQueued = 0u;
};

QueueVerdict enqueueSnapshot(OutboundBudget& budget, std::size_t frameBytes,
                             ac::metrics::CounterRegistry* counters,
                             ac::metrics::GaugeRegistry* gauges) noexcept;
void enqueueEvent(OutboundBudget& budget, std::size_t frameBytes) noexcept;  // 永不丢
void noteDrained(OutboundBudget& budget, std::size_t bytes) noexcept;        // 套接字写完成
bool isBacklogOverHalf(const OutboundBudget& budget) noexcept;
double averageSnapshotBytes(const OutboundBudget& budget) noexcept;
void publishQueueGauges(const OutboundBudget& budget, ac::metrics::GaugeRegistry* gauges) noexcept;
void resetOutboundBudget(OutboundBudget& budget) noexcept;

}  // namespace ac::replication
