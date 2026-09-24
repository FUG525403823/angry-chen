#include "metrics/counters.hpp"

#include <cstring>

namespace ac::metrics {
namespace {

struct CounterDef {
  CounterId id;
  const char* name;
};

constexpr CounterDef kCounterDefs[] = {
    {CounterId::kMalformedFrames, "ac_malformed_frames_total"},
    {CounterId::kOversizedFrames, "ac_oversized_frames_total"},
    {CounterId::kRateLimitedFrames, "ac_rate_limited_frames_total"},
    {CounterId::kDroppedFrames, "ac_dropped_frames_total"},
    {CounterId::kPoseSuspect, "ac_pose_suspect_total"},
    {CounterId::kPoseRejected, "ac_pose_rejected_total"},
    {CounterId::kHardCorrect, "ac_hard_correct_total"},
    {CounterId::kRewindClamped, "ac_rewind_clamped_total"},
    {CounterId::kSpeedViolations, "ac_speed_violations_total"},
    {CounterId::kSnapshotRateDownshifts, "ac_snapshot_rate_downshifts_total"},
    {CounterId::kSlowClientDrops, "ac_slow_client_drops_total"},
    {CounterId::kRoomBudgetExceeded, "ac_room_budget_exceeded_total"},
    {CounterId::kTickSkips, "ac_tick_skips_total"},
    {CounterId::kSnapshotsSent, "ac_snapshots_sent_total"},
    {CounterId::kSnapshotBytesTotal, "ac_snapshot_bytes_total"},
    {CounterId::kGraceStarts, "ac_grace_starts_total"},
    {CounterId::kGraceReconnects, "ac_grace_reconnects_total"},
    {CounterId::kGraceTimeouts, "ac_grace_timeouts_total"},
    {CounterId::kBytesOut, "ac_bytes_out_total"},
    {CounterId::kBytesIn, "ac_bytes_in_total"},
    {CounterId::kFramesOut, "ac_frames_out_total"},
    {CounterId::kFramesIn, "ac_frames_in_total"},
    {CounterId::kEventsSent, "ac_events_sent_total"},
    {CounterId::kEventsDropped, "ac_events_dropped_total"},
    {CounterId::kCorruptLines, "ac_corrupt_lines_total"},
    {CounterId::kHttpRateLimited, "ac_http_rate_limited_total"},
    {CounterId::kHttpCacheHits, "ac_http_cache_hits_total"},
};
constexpr std::size_t kCounterDefCount = sizeof(kCounterDefs) / sizeof(kCounterDefs[0]);
static_assert(kCounterDefCount == kCounterCount, "每个计数恰好一条名字");

constexpr bool defsAreOrdered() noexcept {
  for (std::size_t i = 0u; i < kCounterDefCount; ++i) {
    if (static_cast<std::size_t>(kCounterDefs[i].id) != i) return false;
  }
  return true;
}
static_assert(defsAreOrdered(), "名字表顺序必须与 CounterId 的值一一对应（取值口按值索引）");

}  // namespace

void resetCounters(CounterRegistry& registry) noexcept {
  for (uint64_t& value : registry.values) value = 0u;
}

void addCounter(CounterRegistry& registry, CounterId id, uint64_t delta) noexcept {
  const std::size_t index = static_cast<std::size_t>(id);
  if (index >= kCounterCount) return;
  registry.values[index] += delta;
}

uint64_t counterValue(const CounterRegistry& registry, CounterId id) noexcept {
  const std::size_t index = static_cast<std::size_t>(id);
  return index < kCounterCount ? registry.values[index] : 0u;
}

const char* counterName(CounterId id) noexcept {
  const std::size_t index = static_cast<std::size_t>(id);
  return index < kCounterCount ? kCounterDefs[index].name : "";
}

bool isCounterRegistered(const char* name) noexcept {
  if (name == nullptr) return false;
  for (const CounterDef& def : kCounterDefs) {
    if (std::strcmp(def.name, name) == 0) return true;
  }
  return false;
}

std::size_t counterCount() noexcept { return kCounterCount; }

}  // namespace ac::metrics
