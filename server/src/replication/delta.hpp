#pragma once
// S12 §5（§3 交付物 2）：世界 → 线上记录投影 + 差分编码（段序照抄 S03 §5.3）。
//
// 冻结段序：通用包头 → 实体块（count u8 + 15 B × n，id 升序）→ 移除列表（removedCount u8 +
// u16 × m，升序）→ 事件块（eventCount u8 + Σ条目）；未变实体不出现；强制全量时 baselineTick = 0。
// 事件条目的**来源**（sim::Event → net::EventEntry 的映射）属房间侧接线，不在本步交付物内。
#include <cstddef>
#include <cstdint>

#include "net/codec.hpp"
#include "replication/baseline.hpp"
#include "replication/limits.hpp"
#include "sim/world.hpp"

namespace ac::replication {

// v1 snapshot-codec.ts:607 的线上打包：kind 低 2 位 | (flags & 0x3f) << 2；
// flags 逐字继承 v1 computeEntityFlags（玩家 hp<=0 → downed=1，idle → idle=32）。
inline constexpr uint8_t kSnapshotFlagDowned = 1u;
inline constexpr uint8_t kSnapshotFlagIdle = 32u;

uint8_t kindFlagsOf(const sim::Entity& entity) noexcept;

// 世界 → 线上记录（按 EntityId 升序；最多 capacity 条）。
std::size_t projectWorld(const sim::World& world, net::EntityRecord* out,
                         std::size_t capacity) noexcept;

// 移除列表 = 基线里存在、**世界当前真的没有**的 id（升序，最多 capacity 条）。
// 不能拿「本帧记录」当依据：记录数受 S03 的 128 条上限约束，被截断掉的活实体不能被判成删除。
std::size_t collectRemovedIds(const ClientBaseline& baseline, const sim::World& world,
                              uint16_t* out, std::size_t capacity) noexcept;

struct DeltaInput {
  const sim::World* world = nullptr;
  uint16_t session = 0u;
  uint16_t seq = 0u;
  uint16_t lastAckedSeq = 0u;
  const net::EventEntry* events = nullptr;
  std::size_t eventCount = 0u;
  bool isForceFull = false;  // 由 shouldForceFull(baseline, tick) 决定
};

struct DeltaOutcome {
  bool isOk = false;
  bool isFull = false;
  std::size_t bytes = 0u;
  std::size_t recordCount = 0u;
  std::size_t removedCount = 0u;
  std::size_t eventCount = 0u;
  std::size_t truncatedCount = 0u;  // 世界实体数超过 kMaxRecordsPerFrame 时被留在帧外的条数
  uint32_t tick = 0u;
};

// 编码成功时基线随镜像一起前进（§5：不等 ack）；失败时基线保持不动（下一帧仍按旧基线差分）。
DeltaOutcome encodeDelta(const DeltaInput& input, ClientBaseline& baseline, uint8_t* out,
                         std::size_t capacity) noexcept;

}  // namespace ac::replication
