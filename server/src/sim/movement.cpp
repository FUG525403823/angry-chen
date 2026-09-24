// S06 §5.2：移动集成的唯一实现。运算顺序逐字照抄计划代码块（ADR-010：禁止"顺手优化"）。
#include "sim/movement.hpp"

#include <cmath>

#include "core/quantize.hpp"

namespace ac::sim {

MoveState moveStateOf(const Entity& entity) noexcept {
  return MoveState{entity.pos, entity.vel, entity.yaw, entity.pitch};
}

void storeMoveState(Entity& entity, const MoveState& state) noexcept {
  entity.pos = state.pos;
  entity.vel = state.vel;
  entity.yaw = state.yaw;
  entity.pitch = state.pitch;
}

void applyCommandToState(MoveState& state, const Command* command, double speedMultiplier) noexcept {
  if (command == nullptr) {
    state.vel = ac::Vec3{0.0, 0.0, 0.0};
    return;
  }
  state.yaw = command->yaw;
  state.pitch = command->pitch;
  const uint16_t yawTableUnits = ac::quantizeAngle(command->yaw);
  const double speed = ((command->buttons & ac::config::kButtonSprint) != 0
                            ? ac::config::kSprintSpeedMps
                            : ac::config::kMoveSpeedMps) *
                       speedMultiplier;
  const double fx = ac::sinUnits(yawTableUnits);
  const double fz = ac::cosUnits(yawTableUnits);
  const double rx = ac::cosUnits(yawTableUnits);
  const double rz = -ac::sinUnits(yawTableUnits);
  double vx = fx * command->moveX + rx * command->moveY;
  double vz = fz * command->moveX + rz * command->moveY;
  const double magSq = vx * vx + vz * vz;
  if (magSq > 1.0) {
    const double inv = 1.0 / std::sqrt(magSq);
    vx *= inv;
    vz *= inv;
  }
  state.vel.x = vx * speed;
  state.vel.y = 0.0;
  state.vel.z = vz * speed;
}

void integrateState(MoveState& state, double dtSeconds) noexcept {
  state.pos.x += state.vel.x * dtSeconds;
  state.pos.y += state.vel.y * dtSeconds;
  state.pos.z += state.vel.z * dtSeconds;
}

void clampHorizontalSpeed(MoveState& state, double maxSpeedMps) noexcept {
  const double magSq = state.vel.x * state.vel.x + state.vel.z * state.vel.z;
  const double maxSq = maxSpeedMps * maxSpeedMps;
  if (!(magSq > maxSq)) return;
  const double scale = maxSpeedMps / std::sqrt(magSq);
  state.vel.x *= scale;
  state.vel.z *= scale;
}

}  // namespace ac::sim
