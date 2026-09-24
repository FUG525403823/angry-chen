#include "replication/backpressure.hpp"

namespace ac::replication {

void publishQueueGauges(const OutboundBudget& budget, ac::metrics::GaugeRegistry* gauges) noexcept {
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSendQueueBytes,
                          static_cast<double>(budget.queuedBytes));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSnapshotBytesAvg,
                          averageSnapshotBytes(budget));
  ac::metrics::setGaugeIf(gauges, ac::metrics::GaugeId::kSnapshotBytesMax,
                          static_cast<double>(budget.maxSnapshotBytes));
}

double averageSnapshotBytes(const OutboundBudget& budget) noexcept {
  if (budget.snapshotCount == 0u) return 0.0;
  return static_cast<double>(budget.snapshotBytesTotal) / static_cast<double>(budget.snapshotCount);
}

QueueVerdict enqueueSnapshot(OutboundBudget& budget, std::size_t frameBytes,
                             ac::metrics::CounterRegistry* counters,
                             ac::metrics::GaugeRegistry* gauges) noexcept {
  const bool isFrameTooLarge = frameBytes > kMaxSnapshotBytes;
  const bool isOverBudget = budget.queuedBytes + frameBytes > kOutboundBacklogBytes;
  if (isFrameTooLarge || isOverBudget) {
    ++budget.droppedSnapshots;
    ++budget.consecutiveDrops;
    ac::metrics::bumpCounter(counters, ac::metrics::CounterId::kSlowClientDrops);
    publishQueueGauges(budget, gauges);
    return budget.consecutiveDrops >= kSlowClientDropFrames ? QueueVerdict::kDisconnect
                                                            : QueueVerdict::kDropSnapshot;
  }
  budget.queuedBytes += frameBytes;
  budget.snapshotBytesTotal += frameBytes;
  ++budget.snapshotCount;
  budget.consecutiveDrops = 0u;
  if (frameBytes > budget.maxSnapshotBytes) {
    budget.maxSnapshotBytes = static_cast<uint32_t>(frameBytes);
  }
  publishQueueGauges(budget, gauges);
  return QueueVerdict::kEnqueue;
}

void enqueueEvent(OutboundBudget& budget, std::size_t frameBytes) noexcept {
  budget.queuedBytes += frameBytes;
  ++budget.eventsQueued;
}

void noteDrained(OutboundBudget& budget, std::size_t bytes) noexcept {
  budget.queuedBytes = bytes >= budget.queuedBytes ? 0u : budget.queuedBytes - bytes;
}

bool isBacklogOverHalf(const OutboundBudget& budget) noexcept {
  return budget.queuedBytes > kBacklogDownshiftBytes;
}

void resetOutboundBudget(OutboundBudget& budget) noexcept { budget = OutboundBudget{}; }

}  // namespace ac::replication
