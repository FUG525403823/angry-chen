#pragma once
// S16：补给箱补弹阶段（stepWorld 阶段 10 resolveCombat 之后）。
// 无 pickup 实体时零开销直接返回（fixture 世界恒无 pickup → 对拍零回归）。
#include <cstdint>

namespace ac::sim {
struct World;

// 每 tick 调用：玩家按住交互键且在任一补给箱 kAmmoCrateRangeM 内 → 蓄力 ammoRefillMs，
// 满 kAmmoCrateRefillMs 后补满三弹匣 + 备弹（含升级上限）；松手/离距/已满 → 蓄力归零。
void resolveAmmoCrates(World& world, uint32_t dtMs) noexcept;
}  // namespace ac::sim
