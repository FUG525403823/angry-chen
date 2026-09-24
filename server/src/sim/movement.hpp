#pragma once
// S06 §5.2/§5.3：内存侧命令、位移状态子集与移动集成（逐位复刻 v1 applyCommandToState / integrateState）。
#include <cstdint>

#include "config/player.hpp"
#include "core/math.hpp"
#include "sim/entity_table.hpp"

namespace ac::sim {

// §5.2：内存侧命令。字段顺序 = 线上载荷顺序（S03 §5.2 的 14 字节布局），但类型是解码后的内存形：
//   moveX / moveY 线上 i8（±127）→ 除 127 得 double ∈ [-1, 1]；
//   yaw / pitch 线上 u16 角度单位 → radiansFromUnits 得 double 弧度（状态与对拍一律 double 弧度）；
//   buttons 位域见 config::kButton*；seq u16 回绕；clientTick = v1 的 Command.tick。
struct Command {
  double moveX = 0.0;
  double moveY = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
  uint8_t buttons = 0u;
  uint8_t switchTo = 0u;
  uint16_t seq = 0u;
  uint32_t clientTick = 0u;
};

// §5.2：位移状态子集。实体、预测与对拍共用同一套运算；yaw/pitch 与 Entity 同为 double 弧度。
struct MoveState {
  ac::Vec3 pos{0.0, 0.0, 0.0};
  ac::Vec3 vel{0.0, 0.0, 0.0};
  double yaw = 0.0;
  double pitch = 0.0;
};

MoveState moveStateOf(const Entity& entity) noexcept;
void storeMoveState(Entity& entity, const MoveState& state) noexcept;

// §5.2 代码块整段落地（禁止改写运算顺序）：缺命令 → 速度归零、位置不动。
void applyCommandToState(MoveState& state, const Command* command, double speedMultiplier) noexcept;
void integrateState(MoveState& state, double dtSeconds) noexcept;
void clampHorizontalSpeed(MoveState& state, double maxSpeedMps) noexcept;

}  // namespace ac::sim
