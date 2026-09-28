#pragma once
// S03 §5.4 / S10 §5.7-5：`sim::Event` → `net::EventEntry` 的投影。事件条目的**生产**属房间/广播侧
// （`replication/delta.hpp` 文件头注释里那条缝）。
//
// 纪律：类型号与字段次序照抄 S03 §5.4 的冻结表（实现落在 `net::EventEntry` 的变体上，不新造类型号）；
// 量化口径与 v1 `packages/shared/src/net/codec.ts` 的 `encodeEventPayload` 一致（`clamp(round(x))`，
// 其中 round 是 `floor(x + 0.5)`，见 §8.5 风险 1）；命中点用 `ac::quantizePosition`（厘米，i16）。
#include <cstddef>
#include <cstdint>

#include "net/codec.hpp"
#include "sim/world.hpp"

namespace ac::room {

// 单条映射：可映射返回 true 并写入 out（eventId 由调用方分配）。
// type 10（phaseChange）两侧都没有生产者（v1 `encodeEventPayload` 也没有这个分支）⇒ 不可映射：
// 不占 eventId、不产生条目；调用方只消费掉这条 sim 事件。
bool mapSimEvent(const ac::sim::Event& event, uint32_t eventId, ac::net::EventEntry& out) noexcept;

// 把 `world.events[world.eventCursor, world.eventCount)` 投影成条目，**追加**到 `out[offset, capacity)`，
// 返回本次追加的条数。被消费的事件推进 `world.eventCursor`（跨 tick 不重放）。
// 容量不足时停在该事件之前（游标不前移），由调用方在下一帧续投。
// `nextEventId` 是房间的幂等键水位：从 1 起单调递增、全局唯一、永不重用（S03 §5.4）。
std::size_t projectRoomEvents(ac::sim::World& world, ac::net::EventEntry* out, std::size_t offset,
                              std::size_t capacity, uint32_t& nextEventId) noexcept;

}  // namespace ac::room
