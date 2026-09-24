#include "replication/backpressure.hpp"

namespace ac::replication {
namespace {

// 丢弃只作用于快照通道：事件通道是可靠通道，永不丢（§5 行 70）。
void noteDroppedFrames(OutboundBudget& budget, ac::metrics::CounterRegistry* counters,
                       std::size_t frames) noexcept {
  budget.droppedSnapshots += frames;
  budget.consecutiveDrops += frames;
  ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kSlowClientDrops, frames);
}

void takeSnapshotBytes(OutboundBudget& budget, std::size_t bytes) noexcept {
  const std::size_t taken = bytes < budget.queuedBytes ? bytes : budget.queuedBytes;
  budget.queuedBytes -= taken;
  const std::size_t fromSnapshots =
      taken < budget.queuedSnapshotBytes ? taken : budget.queuedSnapshotBytes;
  budget.queuedSnapshotBytes -= fromSnapshots;
  if (fromSnapshots > 0u) {
    budget.queuedSnapshotCount =
        budget.queuedSnapshotCount > 1u ? budget.queuedSnapshotCount - 1u : 0u;
  }
  if (budget.queuedSnapshotBytes == 0u) budget.queuedSnapshotCount = 0u;
}

void acceptSnapshot(OutboundBudget& budget, std::size_t frameBytes) noexcept {
  budget.queuedBytes += frameBytes;
  budget.queuedSnapshotBytes += frameBytes;
  ++budget.queuedSnapshotCount;
  budget.snapshotBytesTotal += frameBytes;
  ++budget.snapshotCount;
  if (frameBytes > budget.maxSnapshotBytes) budget.maxSnapshotBytes = frameBytes;
}

}  // namespace

QueueVerdict enqueueSnapshot(OutboundBudget& budget, std::size_t frameBytes,
                             ac::metrics::CounterRegistry* counters,
                             ac::metrics::GaugeRegistry* gauges) noexcept {
  QueueVerdict verdict = QueueVerdict::kEnqueue;

  if (frameBytes > kMaxSnapshotBytes) {
    // 单帧就超上限：编码器不会产出这种帧（S03 §5.3 的 encodeSnapshot 先失败），不排队。
    noteDroppedFrames(budget, counters, 1u);
    verdict = QueueVerdict::kDropSnapshot;
  } else {
    if (budget.queuedBytes + frameBytes > kOutboundBacklogBytes) {
      // §5 行 70「旧快照直接丢」：腾掉队列里的旧快照（事件字节保留），让最新帧进队。
      const std::size_t staleFrames = budget.queuedSnapshotCount;
      budget.queuedBytes -= budget.queuedSnapshotBytes;
      budget.queuedSnapshotBytes = 0u;
      budget.queuedSnapshotCount = 0u;
      noteDroppedFrames(budget, counters, staleFrames);
    }
    if (budget.queuedBytes + frameBytes > kOutboundBacklogBytes) {
      // 丢完旧快照还是放不下（事件字节已占满预算）：本帧也不入队。
      noteDroppedFrames(budget, counters, 1u);
      verdict = QueueVerdict::kDropSnapshot;
    }
    if (budget.consecutiveDrops >= kSlowClientDropFrames) {
      if (verdict == QueueVerdict::kEnqueue) noteDroppedFrames(budget, counters, 1u);
      verdict = QueueVerdict::kDisconnect;  // 慢客户端：Disconnect(reason = 7) + 30 s 宽限期
    } else if (verdict == QueueVerdict::kEnqueue) {
      acceptSnapshot(budget, frameBytes);
    }
  }

  publishQueueGauges(budget, gauges);
  return verdict;
}

void enqueueEvent(OutboundBudget& budget, std::size_t frameBytes) noexcept {
  budget.queuedBytes += frameBytes;
  ++budget.eventsQueued;
}

void noteDrained(OutboundBudget& budget, std::size_t bytes) noexcept {
  takeSnapshotBytes(budget, bytes);  // 快照先出队：它占了队列字节账的绝大部分
  budget.consecutiveDrops = 0u;      // 写成功 = 客户端跟得上，连续丢弃重新计数
  publishQueueGauges(budget, nullptr);
}

bool isBacklogOverHalf(const OutboundBudget& budget) noexcept {
  return budget.queuedBytes > kBacklogDownshiftBytes;
}

double averageSnapshotBytes(const OutboundBudget& budget) noexcept {
  if (budget.snapshotCount == 0u) return 0.0;
  return static_cast<double>(budget.snapshotBytesTotal) / static_cast<double>(budget.snapshotCount);
}

void publishQueueGauges(const OutboundBudget& budget, ac::metrics::GaugeRegistry* gauges) noexcept {
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSendQueueBytes,
                          static_cast<double>(budget.queuedBytes));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSnapshotBytesAvg,
                          averageSnapshotBytes(budget));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSnapshotBytesMax,
                          static_cast<double>(budget.maxSnapshotBytes));
}

void resetOutboundBudget(OutboundBudget& budget) noexcept {
  budget = OutboundBudget{};
}

}  // namespace ac::replication
