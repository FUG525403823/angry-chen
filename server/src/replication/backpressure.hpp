#pragma once
// S12 §5（§3 交付物 3）：出站队列字节账、超预算丢**旧**快照、连续丢弃判定慢客户端。
//
// 冻结项：预算 64 KiB（单连接）、单帧上限 2048 B、连续 60 帧丢弃 → Disconnect(reason = 7)；
// 丢弃只作用于**快照通道**（不可靠）；事件通道是可靠通道，永不丢。
// 丢旧不丢新：§5 行 70「队列已排队字节 + 本帧 > 64 KiB → 旧快照直接丢」——腾掉队列里的旧快照，
// 让最新帧进队（快照是状态而非增量，留着旧帧只会让客户端更滞后）。
#include <cstddef>
#include <cstdint>

#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "replication/limits.hpp"

namespace ac::replication {

inline constexpr uint32_t kSlowClientDropFrames = 60u;          // 连续 60 帧（3 s）
inline constexpr uint8_t kDisconnectReasonSlowConsumer = 7u;    // slowConsumer

enum class QueueVerdict : uint8_t {
  kEnqueue = 0,   // 已入队
  kDropSnapshot,  // 单帧超上限：本帧不入队（事件通道不受影响）
  kDisconnect,    // 连续丢弃达 60 帧：Disconnect(7)，随后进入 30 s 宽限期
};

struct OutboundBudget {
  std::size_t queuedBytes = 0u;          // 已拷贝未写完成的字节；健康连接稳态应接近 0
  std::size_t queuedSnapshotBytes = 0u;  // 其中属于快照通道的部分（超预算时整段丢弃）
  std::size_t queuedSnapshotCount = 0u;
  std::size_t snapshotBytesTotal = 0u;
  std::size_t snapshotCount = 0u;
  std::size_t droppedSnapshots = 0u;
  std::size_t consecutiveDrops = 0u;     // 自上次真正排空以来丢掉的帧数
  std::size_t maxSnapshotBytes = 0u;
  std::size_t eventsQueued = 0u;
};

// 超预算时先丢队列里的旧快照再收下最新帧；单帧超过 kMaxSnapshotBytes 时不收（编码器不会产出）。
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
