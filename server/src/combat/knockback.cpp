#include "combat/knockback.hpp"

#include "sim/step.hpp"
#include "sim/world.hpp"

namespace ac::sim {

// S06 冻结的签名只是占位（空实现），S09 按 v1 sim.ts applyKnockback 补上 dtMs 参数并落地。
void applyKnockback(World& world, uint32_t dtMs) noexcept {
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    Entity* found = entityById(world, world.activeIds[i]);
    if (found == nullptr || !found->active || found->kind != EntityKind::kPlayer) continue;
    Entity& entity = *found;
    if (entity.knock.knockMs <= 0.0) continue;
    const double left = entity.knock.knockMs - static_cast<double>(dtMs);
    entity.knock.knockMs = left > 0.0 ? left : 0.0;
    if (entity.downed.downed) continue;
    entity.vel.x = entity.knock.knockVx;
    entity.vel.z = entity.knock.knockVz;
    entity.vel.y = 0.0;
  }
}

}  // namespace ac::sim
