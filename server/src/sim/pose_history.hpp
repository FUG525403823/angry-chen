#pragma once
// S05 §5.3：20 槽姿态环（回滚命中用）。定长、零分配；只记 kind == player。
#include <array>
#include <cstddef>
#include <cstdint>

#include "core/math.hpp"
#include "sim/entity_table.hpp"

namespace ac::sim {

inline constexpr std::size_t kPoseHistorySlots = static_cast<std::size_t>(ac::kPoseHistorySlots);
inline constexpr std::size_t kPoseHistoryPlayers = 8u;
inline constexpr uint32_t kRewindLimitMs = static_cast<uint32_t>(ac::kRewindLimitMs);
static_assert(kPoseHistorySlots == 20u && kRewindLimitMs == 200u,
              "§5.3：槽数与回滚上限逐字复用确定性内核的 20 / 200");

// §5.3：每玩家一条 5 double + u16 id，含对齐共 48 字节。
struct PoseRecord {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
  uint16_t id = kNoEntityId;
};
static_assert(sizeof(PoseRecord) == 48u, "§5.3：每条目含对齐共 48 字节");

using PoseSlot = std::array<PoseRecord, kPoseHistoryPlayers>;
static_assert(sizeof(PoseSlot) == 384u, "§5.3：每槽 8 玩家 × 48 字节");

struct PoseHistory {
  PoseSlot slots[kPoseHistorySlots];  // 7680 字节（§5.3 的一次性预分配区）
  uint32_t newestTick = 0u;           // 环头：最近一次记录的 tick
  uint32_t writeCount = 0u;           // 已写槽数（< 20 表示历史不足）
};
static_assert(sizeof(PoseHistory::slots) == 7680u, "§5.3：20 槽 × 8 玩家 × 48 字节 = 7680");

struct SampledPose {
  bool isOk = false;
  bool found = false;
  bool clamped = false;  // 仅诊断：ms 越限；true 不代表按 200 夹取
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
};

struct World;  // 前置声明：recordPoseHistory 的实现放在 world.cpp，避免头文件互相包含

// §5.3：每 tick 末尾调用一次，只记 kind == player。
void recordPoseHistory(PoseHistory& history, const World& world) noexcept;

// 整槽重写：写新一圈之前必须清空，否则上一圈的玩家会残留成幽灵记录。
inline void clearPoseSlot(PoseSlot& slot) noexcept {
  for (PoseRecord& record : slot) record = PoseRecord{};
}

namespace detail {

inline const PoseRecord* poseRecordInSlot(const PoseHistory& history, std::size_t slot,
                                          uint16_t entityId) noexcept {
  for (const PoseRecord& record : history.slots[slot]) {
    if (record.id == entityId) return &record;
  }
  return nullptr;
}

// 插值只做 + - * /；alpha == 1 / 0 时直接返回端点，保证 ms = 0 无插值误差。
inline double interpolatePose(double older, double newer, double alpha) noexcept {
  if (alpha >= 1.0) return newer;
  if (alpha <= 0.0) return older;
  return older + (newer - older) * alpha;
}

}  // namespace detail

// §5.3：ms = 请求回退量（调用方传 min(rttMs / 2, 200)）。
// 返回 true 表示 out.isOk（可用回滚姿态）；ms > kRewindLimitMs 越限时不回滚（out.clamped 仅诊断）。
inline bool samplePoseAgo(const PoseHistory& history, uint32_t ms, uint16_t entityId,
                          SampledPose& out) noexcept {
  out = SampledPose{};
  if (ms > kRewindLimitMs) {
    out.clamped = true;
    return false;
  }
  if (history.writeCount == 0u || entityId == kNoEntityId) return false;

  const uint32_t tickMs = static_cast<uint32_t>(ac::kTickMs);
  const uint32_t ageTicks = ms / tickMs;
  const uint32_t remainder = ms % tickMs;
  const uint32_t newestTick = history.newestTick;
  const uint32_t oldestTick = newestTick - (history.writeCount - 1u);

  // 目标 = newestTick - ms / 50（可为小数）；历史不足时夹到最老槽，插值退化为端点值。
  uint32_t newerTick = ageTicks < newestTick ? newestTick - ageTicks : 0u;
  if (newerTick > newestTick || newerTick < oldestTick) newerTick = oldestTick;
  const uint32_t olderTick = newerTick > oldestTick ? newerTick - 1u : newerTick;

  const PoseRecord* newerRecord =
      detail::poseRecordInSlot(history, static_cast<std::size_t>(newerTick % kPoseHistorySlots), entityId);
  const PoseRecord* olderRecord =
      detail::poseRecordInSlot(history, static_cast<std::size_t>(olderTick % kPoseHistorySlots), entityId);
  if (newerRecord == nullptr && olderRecord == nullptr) return false;  // 无该实体的记录

  out.found = true;
  const PoseRecord& from = olderRecord != nullptr ? *olderRecord : *newerRecord;
  const PoseRecord& to = newerRecord != nullptr ? *newerRecord : *olderRecord;
  const double alpha =
      newerTick == olderTick ? 1.0 : 1.0 - static_cast<double>(remainder) / static_cast<double>(tickMs);
  out.x = detail::interpolatePose(from.x, to.x, alpha);
  out.y = detail::interpolatePose(from.y, to.y, alpha);
  out.z = detail::interpolatePose(from.z, to.z, alpha);
  out.yaw = detail::interpolatePose(from.yaw, to.yaw, alpha);
  out.pitch = detail::interpolatePose(from.pitch, to.pitch, alpha);
  out.isOk = true;
  return true;
}

}  // namespace ac::sim
