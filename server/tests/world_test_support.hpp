#pragma once
// S06 起 stepWorld 要命令缓冲与 dtMs，而 S05 的数据层用例只关心"推进一个 tick"。
// 共享一份"空命令推进"，避免 alloc_test.cpp 与 world_test.cpp 各写一遍。
#include "config/player.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace ac::test {

inline bool stepEmpty(ac::sim::World& world) {
  return ac::sim::stepWorld(world, nullptr, 0u, ac::config::kStepDtMs);
}

}  // namespace ac::test
