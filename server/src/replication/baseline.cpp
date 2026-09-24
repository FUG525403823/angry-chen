#include "replication/baseline.hpp"

namespace ac::replication {
namespace {

std::size_t findIndexById(const net::SnapshotBaseline& mirror, uint16_t id) noexcept {
  for (std::size_t i = 0u; i < mirror.records.size(); ++i) {
    if (mirror.records[i].id == id) return i;
    if (mirror.records[i].id > id) break;  // 升序：越过后不可能再命中
  }
  return mirror.records.size();
}

}  // namespace

void reserveBaseline(ClientBaseline& baseline) noexcept {
  baseline.mirror.records.reserve(kBaselineCapacity);
}

void resetBaseline(ClientBaseline& baseline) noexcept {
  baseline.mirror.records.clear();
  baseline.mirror.tick = 0u;
  baseline.lastFullTick = 0u;
  baseline.framesSinceFull = 0u;
}

bool shouldForceFull(const ClientBaseline& baseline, uint32_t tick) noexcept {
  if (baseline.mirror.tick == 0u) return true;
  // 无符号减法：tick 比 lastFullTick 小时会回绕成极大值，同样判「该全量了」，不会漏发。
  return tick - baseline.lastFullTick >= kFullSnapshotIntervalTicks;
}

void advanceBaseline(ClientBaseline& baseline, uint32_t tick, const net::EntityRecord* records,
                     std::size_t recordCount, const uint16_t* removedIds, std::size_t removedCount,
                     bool replacedAll) noexcept {
  net::SnapshotBaseline& mirror = baseline.mirror;
  if (replacedAll) {
    mirror.records.assign(records, records + recordCount);  // 全量：镜像 = 帧内记录
  } else {
    for (std::size_t i = 0u; i < removedCount; ++i) {
      const std::size_t index = findIndexById(mirror, removedIds[i]);
      if (index < mirror.records.size()) {
        mirror.records.erase(mirror.records.begin() + static_cast<std::ptrdiff_t>(index));
      }
    }
    for (std::size_t i = 0u; i < recordCount; ++i) {
      const net::EntityRecord& record = records[i];
      const std::size_t index = findIndexById(mirror, record.id);
      if (index < mirror.records.size()) {
        mirror.records[index] = record;
        continue;
      }
      std::size_t position = 0u;
      while (position < mirror.records.size() && mirror.records[position].id < record.id) ++position;
      mirror.records.insert(mirror.records.begin() + static_cast<std::ptrdiff_t>(position), record);
    }
  }
  mirror.tick = tick;
  ++baseline.framesSinceFull;
}

const net::EntityRecord* baselineFind(const ClientBaseline& baseline, uint16_t id) noexcept {
  return baseline.mirror.find(id);
}

}  // namespace ac::replication
