#pragma once
// S09 §5.2/§5.7：每只羊的 AI 状态（纯数据，不依赖 sim/**，供 Entity 直接内嵌）。
// 对应 v1 ai/sheepState.ts 的 TargetState 与 ai/flocking.ts 的 FlockNeighbors；定长数组，零分配。
#include <cstdint>

#include "config/sheep.hpp"

namespace ac::ai {

// v1 createTargetState / resetTargetState：仇恨槽（容量 = SHEEP_AI.aggroSlots）+ 当前目标。
struct TargetState {
  double aggro[ac::config::kSheepAggroSlots]{};
  uint16_t targetId = 0u;
  double targetDistanceM = 0.0;
};

// §5.7 有界邻居槽：容量恒为 SHEEP_AI.maxNeighbors；distSq 升序、同距按 id 升序（与遍历顺序无关）。
struct FlockNeighbors {
  int32_t capacity = ac::config::kSheepMaxNeighbors;
  int32_t count = 0;
  double xs[ac::config::kSheepMaxNeighbors]{};
  double zs[ac::config::kSheepMaxNeighbors]{};
  double dirXs[ac::config::kSheepMaxNeighbors]{};
  double dirZs[ac::config::kSheepMaxNeighbors]{};
  double distSq[ac::config::kSheepMaxNeighbors]{};
  uint16_t ids[ac::config::kSheepMaxNeighbors]{};
  double centroidX = 0.0;
  double centroidZ = 0.0;
  double averageDirX = 0.0;
  double averageDirZ = 0.0;
};

inline void resetTargetState(TargetState& state) noexcept {
  for (int32_t i = 0; i < ac::config::kSheepAggroSlots; ++i) state.aggro[i] = 0.0;
  state.targetId = 0u;
  state.targetDistanceM = 0.0;
}

inline void resetFlockNeighbors(FlockNeighbors& neighbors) noexcept {
  neighbors.count = 0;
  neighbors.centroidX = 0.0;
  neighbors.centroidZ = 0.0;
  neighbors.averageDirX = 0.0;
  neighbors.averageDirZ = 0.0;
}

// v1 createSheepAiState 的字段初值（chargeDirZ 1 / phase 1 / strafeSign 1 是文字量）。
struct SheepAiState {
  TargetState target{};
  double timerMs = 0.0;
  double cooldownMs = 0.0;
  double chargeDirX = 0.0;
  double chargeDirZ = 1.0;
  double grazeX = 0.0;
  double grazeZ = 0.0;
  int32_t phase = 1;
  double summonMs = 0.0;
  int32_t summoned = 0;
  double strafeSign = 1.0;
  FlockNeighbors flock{};
};

inline void resetSheepAiState(SheepAiState& state) noexcept {
  resetTargetState(state.target);
  state.timerMs = 0.0;
  state.cooldownMs = 0.0;
  state.chargeDirX = 0.0;
  state.chargeDirZ = 1.0;
  state.grazeX = 0.0;
  state.grazeZ = 0.0;
  state.phase = 1;
  state.summonMs = 0.0;
  state.summoned = 0;
  state.strafeSign = 1.0;
  resetFlockNeighbors(state.flock);
}

}  // namespace ac::ai
