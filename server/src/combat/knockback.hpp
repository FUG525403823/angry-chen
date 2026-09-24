#pragma once
// S09 §5.3/§5.7：击退状态（v1 entity.combat 的 knockMs/knockVx/knockVz）。
// S08 的 Entity 只带了武器/怒气/倒地，击退由 S09 补上（阶段 6 applyKnockback 的落地）。
namespace ac::combat {

struct KnockbackState {
  double knockMs = 0.0;
  double knockVx = 0.0;
  double knockVz = 0.0;
};

inline void resetKnockbackState(KnockbackState& state) noexcept {
  state.knockMs = 0.0;
  state.knockVx = 0.0;
  state.knockVz = 0.0;
}

}  // namespace ac::combat
