// S06 用例：命令应用、积分、静态碰撞、分离、局部步进与阶段顺序（计划 §5.1–§5.6 / §6 / §7）。
#include "tiny_test.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "allocation_probe.hpp"
#include "config/player.hpp"
#include "core/math.hpp"
#include "core/rng.hpp"
#include "sim/local_step.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"
#include "world_test_support.hpp"

namespace {

constexpr uint32_t kSeed = 0x5EED2026u;
namespace sim = ac::sim;

uint64_t bits(double value) { return std::bit_cast<uint64_t>(value); }

sim::EntityId spawnPlayer(sim::World& world, double x, double z) {
  return sim::spawnEntity(world, sim::EntityKind::kPlayer, ac::Vec3{x, 0.0, z}).id;
}

sim::EntityId spawnSheep(sim::World& world, double x, double z) {
  return sim::spawnEntity(world, sim::EntityKind::kSheep, ac::Vec3{x, 0.0, z}).id;
}

sim::EntityId spawnPickup(sim::World& world, double x, double z) {
  return sim::spawnEntity(world, sim::EntityKind::kPickup, ac::Vec3{x, 0.0, z}).id;
}

sim::Command command(double moveX, double moveY, double yaw) {
  sim::Command c{};
  c.moveX = moveX;
  c.moveY = moveY;
  c.yaw = yaw;
  return c;
}

bool stepOnce(sim::World& world, const sim::Command* c) {
  return sim::stepWorld(world, c, c == nullptr ? 0u : 1u, ac::config::kStepDtMs);
}

void runTicks(sim::World& world, const sim::Command& c, int ticks) {
  for (int i = 0; i < ticks; ++i) stepOnce(world, &c);
}

const sim::Entity& at(const sim::World& world, sim::EntityId id) { return world.entities[id - 1u]; }

}  // namespace

AC_TEST(step_rejects_wrong_dt_ms) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 1.0, 2.0);
  const sim::Command c = command(1.0, 0.0, 0.0);
  AC_CHECK(!sim::stepWorld(*world, &c, 1u, 49u));
  AC_CHECK(!sim::stepWorld(*world, &c, 1u, 51u));
  AC_CHECK(!sim::stepWorld(*world, &c, 1u, 0u));
  AC_CHECK_EQ(world->tick, 0u);
  AC_CHECK_EQ(world->timeMs, 0u);
  AC_CHECK_EQ(at(*world, id).pos.z, 2.0);
  AC_CHECK_EQ(at(*world, id).vel.z, 0.0);
  AC_CHECK(sim::stepWorld(*world, &c, 1u, ac::config::kStepDtMs));
  AC_CHECK_EQ(world->tick, 1u);
  AC_CHECK_EQ(world->timeMs, 50u);
}

AC_TEST(step_header_advances_tick_and_time) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  spawnPlayer(*world, 1.0, 2.0);
  sim::Event event{};
  event.eventId = 7u;
  event.type = 3u;
  AC_CHECK(sim::pushEvent(*world, event));
  AC_CHECK_EQ(world->eventCount, 1u);
  world->eventCursor = 9u;
  AC_CHECK(stepOnce(*world, nullptr));
  AC_CHECK_EQ(world->tick, 1u);
  AC_CHECK_EQ(world->timeMs, 50u);
  AC_CHECK_EQ(world->eventCount, 0u);
  AC_CHECK_EQ(world->eventCursor, 0u);
  AC_CHECK(stepOnce(*world, nullptr));
  AC_CHECK_EQ(world->tick, 2u);
  AC_CHECK_EQ(world->timeMs, 100u);
}

AC_TEST(step_walk_advances_4_5_meters) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 20.0, 0.0);
  const sim::Command c = command(1.0, 0.0, 0.0);
  runTicks(*world, c, 20);
  std::printf("stepWalkZ=%.17g\n", at(*world, id).pos.z);
  AC_CHECK_EQ(at(*world, id).pos.z, 4.5);  // 实测恰好 4.5
  AC_CHECK_EQ(at(*world, id).pos.x, 20.0);
  AC_CHECK_EQ(at(*world, id).pos.y, 0.0);
  AC_CHECK_EQ(at(*world, id).vel.z, 4.5);
}

AC_TEST(step_sprint_advances_6_3_meters) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 20.0, 0.0);
  sim::Command c = command(1.0, 0.0, 0.0);
  c.buttons = ac::config::kButtonSprint;
  runTicks(*world, c, 20);
  std::printf("stepSprintZ=%.17g\n", at(*world, id).pos.z);
  AC_CHECK_NEAR(at(*world, id).pos.z, 6.3, 1e-12);
  AC_CHECK_EQ(at(*world, id).vel.z, 6.3);
}

AC_TEST(step_missing_command_zeroes_speed) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId first = spawnPlayer(*world, 20.0, 4.0);
  const sim::EntityId second = spawnPlayer(*world, 22.0, 6.0);
  const sim::Command c = command(1.0, 0.0, 0.0);
  AC_CHECK(sim::stepWorld(*world, &c, 1u, ac::config::kStepDtMs));
  AC_CHECK_EQ(at(*world, first).pos.z, 4.0 + 0.225);  // 拿到命令的那个前进一格
  AC_CHECK_EQ(at(*world, first).vel.z, 4.5);
  AC_CHECK_EQ(at(*world, second).pos.z, 6.0);  // 缺命令：速度归零、位置不动
  AC_CHECK_EQ(at(*world, second).vel.z, 0.0);
  AC_CHECK_EQ(at(*world, second).vel.x, 0.0);
}

AC_TEST(step_command_sets_yaw_and_pitch) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 20.0, 20.0);
  sim::Command c = command(0.0, 0.0, 0.75);
  c.pitch = -0.25;
  AC_CHECK(stepOnce(*world, &c));
  AC_CHECK_EQ(at(*world, id).yaw, 0.75);
  AC_CHECK_EQ(at(*world, id).pitch, -0.25);
  AC_CHECK_EQ(at(*world, id).vel.x, 0.0);
  AC_CHECK_EQ(at(*world, id).vel.z, 0.0);
}

AC_TEST(step_diagonal_input_is_normalized) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 20.0, 20.0);
  const sim::Command c = command(1.0, 1.0, 0.0);
  AC_CHECK(stepOnce(*world, &c));
  const double vx = at(*world, id).vel.x;
  const double vz = at(*world, id).vel.z;
  AC_CHECK_NEAR(std::sqrt(vx * vx + vz * vz), 4.5, 1e-12);
  AC_CHECK_NEAR(vx, vz, 1e-12);
  AC_CHECK_NEAR(vx, 4.5 / std::sqrt(2.0), 1e-12);
}

AC_TEST(step_forward_vector_uses_shared_table) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 0.0, 20.0);
  const sim::Command c = command(1.0, 0.0, ac::kPi / 2.0);  // yaw = 90° → 前向 = +x
  runTicks(*world, c, 20);
  std::printf("stepForwardX=%.17g\n", at(*world, id).pos.x);
  AC_CHECK_EQ(at(*world, id).pos.x, 4.5);  // 实测恰好 4.5
  AC_CHECK_EQ(at(*world, id).pos.z, 20.0);  // 表里 cos(90°) 恰好为 0，不产生侧向漂移
}

AC_TEST(step_integrates_velocity_into_position) {
  sim::MoveState state{};
  state.vel.z = 4.5;
  sim::integrateState(state, 50.0 / 1000.0);
  AC_CHECK_EQ(state.pos.z, 0.225);
  sim::integrateState(state, 0.05);
  AC_CHECK_EQ(state.pos.z, 0.45);
  AC_CHECK_EQ(state.pos.x, 0.0);
  sim::MoveState slow{};
  slow.vel.x = 1.0;
  slow.vel.z = 1.0;
  sim::clampHorizontalSpeed(slow, 1.0);
  AC_CHECK_NEAR(slow.vel.x, 1.0 / std::sqrt(2.0), 1e-12);
  AC_CHECK_NEAR(slow.vel.z, slow.vel.x, 1e-15);
  sim::clampHorizontalSpeed(slow, 4.5);
  AC_CHECK_NEAR(slow.vel.x, 1.0 / std::sqrt(2.0), 1e-15);  // 未超限时一个字节都不动
  sim::MoveState rescued{};
  rescued.vel.x = 2.0;
  rescued.vel.z = 2.0;
  sim::clampHorizontalSpeed(rescued, ac::config::kReviveSpeedClampMps);  // §5.1 阶段 7 的救援上限 1.5
  AC_CHECK_NEAR(std::sqrt(rescued.vel.x * rescued.vel.x + rescued.vel.z * rescued.vel.z), 1.5, 1e-12);
}

// S09 补上 S08 §9.2-1 的欠账：阶段 1 对「按住交互 + 救援距离内有倒地队友」的玩家限速（v1 sim.ts:109-115）。
AC_TEST(step_clamps_reviver_speed_when_holding_interact) {
  AC_CHECK_EQ(ac::config::kReviveRangeM, 2.0);
  AC_CHECK_EQ(ac::config::kReviveSpeedClampMps, 1.5);
  struct Case {
    bool downed;
    bool interact;
    double victimX;
    double expectedSpeed;
  };
  const Case cases[4] = {
      {true, true, 1.0, ac::config::kReviveSpeedClampMps},   // 按住交互、队友 1m 内 → 限速
      {true, false, 1.0, ac::config::kMoveSpeedMps},         // 不按交互 → 不限速
      {false, true, 1.0, ac::config::kMoveSpeedMps},         // 队友没倒地 → 不限速
      {true, true, 3.0, ac::config::kMoveSpeedMps},          // 超出救援距离 2.0m → 不限速
  };
  for (const Case& item : cases) {
    std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
    const sim::EntityId reviver = spawnPlayer(*world, 0.0, 20.0);
    const sim::EntityId victim = spawnPlayer(*world, item.victimX, 20.0);
    if (item.downed) {
      world->entities[victim - 1u].downed.downed = true;
      world->entities[victim - 1u].hp = 0.0;
    }
    sim::Command c = command(1.0, 0.0, ac::kPi);  // 前向 -z 的步行 4.5 m/s
    if (item.interact) c.buttons = ac::config::kButtonInteract;
    sim::Command commands[2] = {c, c};  // 第 k 条命令给升序第 k 名玩家
    AC_CHECK(sim::stepWorld(*world, commands, 2u, ac::config::kStepDtMs));
    const sim::Entity& reviverEntity = at(*world, reviver);
    const double speed =
        std::sqrt(reviverEntity.vel.x * reviverEntity.vel.x + reviverEntity.vel.z * reviverEntity.vel.z);
    AC_CHECK_NEAR(speed, item.expectedSpeed, 1e-12);
    AC_CHECK_EQ(reviverEntity.vel.x, 0.0);
    AC_CHECK_NEAR(reviverEntity.pos.z, 20.0 - item.expectedSpeed * 0.05, 1e-12);
    if (item.downed) {
      const sim::Entity& victimEntity = at(*world, victim);
      AC_CHECK_EQ(victimEntity.vel.x, 0.0);
      AC_CHECK_EQ(victimEntity.vel.z, 0.0);  // 倒地玩家不移动
    }
  }
}

AC_TEST(step_barn_push_out_stops_at_z_face) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 0.0, 30.0);
  const sim::Command c = command(1.0, 0.0, ac::kPi);  // yaw = 180° → 前向 = -z
  runTicks(*world, c, 400);
  AC_CHECK_EQ(at(*world, id).pos.z, 4.4);  // 4.0 + 半径 0.4
  AC_CHECK_EQ(at(*world, id).vel.z, 0.0);
  AC_CHECK_EQ(at(*world, id).pos.x, 0.0);
}

AC_TEST(step_fence_clamps_x_and_clears_velocity) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 0.0, 0.0);
  const sim::Command c = command(0.0, 1.0, 0.0);  // 右移 = +x
  runTicks(*world, c, 400);
  AC_CHECK_EQ(at(*world, id).pos.x, 39.35);  // 40 - 0.25 - 0.4
  AC_CHECK_EQ(at(*world, id).vel.x, 0.0);
}

AC_TEST(step_barn_corner_picks_nearest_face) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId nearX = spawnPlayer(*world, 3.0, 0.0);
  const sim::EntityId nearZ = spawnPlayer(*world, 0.0, 3.0);
  AC_CHECK(stepOnce(*world, nullptr));
  AC_CHECK_EQ(at(*world, nearX).pos.x, 4.4);  // 离 x 面更近 → 贴 x 面
  AC_CHECK_EQ(at(*world, nearX).pos.z, 0.0);
  AC_CHECK_EQ(at(*world, nearZ).pos.z, 4.4);  // 离 z 面更近 → 贴 z 面
  AC_CHECK_EQ(at(*world, nearZ).pos.x, 0.0);

  std::unique_ptr<sim::World> center = sim::createWorld(kSeed);
  const sim::EntityId middle = spawnPlayer(*center, 0.0, 0.0);
  AC_CHECK(stepOnce(*center, nullptr));
  AC_CHECK_EQ(at(*center, middle).pos.x, 4.4);  // 四面等距：按 §5.5 的不等号取 x 面
  AC_CHECK_EQ(at(*center, middle).pos.z, 0.0);
}

AC_TEST(step_barn_skipped_above_height) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 0.0, 0.0);
  world->entities[id - 1u].pos.y = 5.0;  // §5.5：pos.y >= 5 直接跳过谷仓推离
  AC_CHECK(stepOnce(*world, nullptr));
  AC_CHECK_EQ(at(*world, id).pos.x, 0.0);
  AC_CHECK_EQ(at(*world, id).pos.z, 0.0);
  AC_CHECK_EQ(at(*world, id).pos.y, 5.0);
}

AC_TEST(step_separation_pushes_apart_to_radii) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId a = spawnPlayer(*world, 10.0, 0.0);
  const sim::EntityId b = spawnPlayer(*world, 10.5, 0.0);
  AC_CHECK(stepOnce(*world, nullptr));
  const double ax = at(*world, a).pos.x;
  const double bx = at(*world, b).pos.x;
  std::printf("stepSeparation=%.17g\n", bx - ax);
  AC_CHECK_NEAR(bx - ax, 0.8, 1e-12);  // 0.4 + 0.4
  AC_CHECK_NEAR((ax + bx) / 2.0, 10.25, 1e-12);  // 各推一半 → 中点守恒
  AC_CHECK_EQ(at(*world, a).pos.z, 0.0);
}

AC_TEST(step_separation_is_symmetric_half_push) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId a = spawnSheep(*world, 10.0, 0.0);
  const sim::EntityId b = spawnSheep(*world, 10.2, 0.0);
  ac::test::freezeSheepAi(*world);  // S09 起阶段 4/5 会驱动羊的 AI，本用例只测分离
  AC_CHECK(stepOnce(*world, nullptr));
  const double ax = at(*world, a).pos.x;
  const double bx = at(*world, b).pos.x;
  AC_CHECK_NEAR(bx - ax, 1.0, 1e-12);  // 0.5 + 0.5
  AC_CHECK_NEAR(ax, 10.0 - 0.4, 1e-12);
  AC_CHECK_NEAR(bx, 10.2 + 0.4, 1e-12);
  AC_CHECK_NEAR((ax + bx) / 2.0, 10.1, 1e-12);
}

AC_TEST(step_separation_zero_distance_uses_x_axis) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId a = spawnSheep(*world, 10.0, 0.0);
  const sim::EntityId b = spawnSheep(*world, 10.0, 0.0);
  ac::test::freezeSheepAi(*world);  // S09 起阶段 4/5 会驱动羊的 AI，本用例只测零距离分离
  AC_CHECK(stepOnce(*world, nullptr));
  AC_CHECK_NEAR(at(*world, a).pos.x, 9.5, 1e-12);  // 按 (+1, 0) 推：各半
  AC_CHECK_NEAR(at(*world, b).pos.x, 10.5, 1e-12);
  AC_CHECK_EQ(at(*world, a).pos.z, 0.0);
  AC_CHECK_EQ(at(*world, b).pos.z, 0.0);
}

AC_TEST(step_separation_respects_kind_radii) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId sheep = spawnSheep(*world, 10.0, 0.0);
  const sim::EntityId pickup = spawnPickup(*world, 10.2, 0.0);
  ac::test::freezeSheepAi(*world);  // S09 起阶段 4/5 会驱动羊的 AI，本用例只测半径表
  AC_CHECK(stepOnce(*world, nullptr));
  const double distance =
      std::fabs(at(*world, pickup).pos.x - at(*world, sheep).pos.x);
  AC_CHECK_NEAR(distance, ac::config::kSheepRadiusM + ac::config::kPickupRadiusM, 1e-12);
}

AC_TEST(step_local_step_equals_authority) {
  std::unique_ptr<sim::World> authority = sim::createWorld(kSeed);
  std::unique_ptr<sim::World> predicted = sim::createWorld(kSeed);
  const sim::EntityId authorityId = spawnPlayer(*authority, 10.0, 10.0);
  const sim::EntityId predictedId = spawnPlayer(*predicted, 10.0, 10.0);
  AC_CHECK_EQ(authorityId, predictedId);

  ac::Rng rng = ac::createRng(0x51D3u, ac::RngStream::kFx);
  for (int tick = 0; tick < 240; ++tick) {
    sim::Command c{};
    c.moveX = static_cast<double>(static_cast<int32_t>(rng.nextU32() % 3u)) - 1.0;
    c.moveY = static_cast<double>(static_cast<int32_t>(rng.nextU32() % 3u)) - 1.0;
    c.yaw = static_cast<double>(rng.nextU32() % 628u) / 100.0 - ac::kPi;
    c.pitch = static_cast<double>(rng.nextU32() % 314u) / 100.0 - ac::kPi / 2.0;
    c.buttons = (rng.nextU32() % 4u == 0u) ? ac::config::kButtonSprint : 0u;

    AC_CHECK(sim::stepWorld(*authority, &c, 1u, ac::config::kStepDtMs));
    const sim::LocalStepResult stepped = sim::localStep(*predicted, &c, 1u, ac::config::kStepDtMs, predictedId);
    AC_CHECK(stepped.isOk);
    AC_CHECK_EQ(bits(at(*authority, authorityId).pos.x), bits(at(*predicted, predictedId).pos.x));
    AC_CHECK_EQ(bits(at(*authority, authorityId).pos.y), bits(at(*predicted, predictedId).pos.y));
    AC_CHECK_EQ(bits(at(*authority, authorityId).pos.z), bits(at(*predicted, predictedId).pos.z));
    AC_CHECK_EQ(bits(at(*authority, authorityId).yaw), bits(at(*predicted, predictedId).yaw));
  }
  AC_CHECK_EQ(authority->tick, 240u);
  AC_CHECK_EQ(predicted->tick, 240u);
  AC_CHECK_EQ(predicted->timeMs, 240u * 50u);
}

AC_TEST(step_local_step_rejects_wrong_dt) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 12.0, 0.0);
  const sim::EntityId sheep = spawnSheep(*world, 20.0, 0.0);
  const sim::Command c = command(1.0, 0.0, 0.0);
  AC_CHECK(!sim::localStep(*world, &c, 1u, 49u, id).isOk);
  AC_CHECK(!sim::localStep(*world, nullptr, 1u, ac::config::kStepDtMs, id).isOk);
  AC_CHECK(!sim::localStep(*world, &c, 0u, ac::config::kStepDtMs, id).isOk);
  AC_CHECK(!sim::localStep(*world, &c, 1u, ac::config::kStepDtMs, 900u).isOk);
  AC_CHECK(!sim::localStep(*world, &c, 1u, ac::config::kStepDtMs, sheep).isOk);
  AC_CHECK_EQ(world->tick, 0u);
  AC_CHECK_EQ(world->timeMs, 0u);
  AC_CHECK_EQ(at(*world, id).pos.z, 0.0);

  const sim::LocalStepResult stepped = sim::localStep(*world, &c, 1u, ac::config::kStepDtMs, id);
  AC_CHECK(stepped.isOk);
  AC_CHECK_EQ(stepped.tick, 1u);
  AC_CHECK_EQ(stepped.timeMs, 50u);
  AC_CHECK_EQ(world->tick, 1u);
  AC_CHECK_EQ(world->timeMs, 50u);
  AC_CHECK_EQ(at(*world, id).pos.z, 0.225);
  AC_CHECK_EQ(at(*world, id).pos.x, 12.0);
  AC_CHECK_EQ(at(*world, sheep).pos.x, 20.0);      // 不触碰其他实体
  AC_CHECK_EQ(world->stats.aliveSheep, 0u);        // 不写统计量
  AC_CHECK_EQ(world->eventCount, 0u);              // 不写事件池
  AC_CHECK_EQ(world->poseHistory.writeCount, 0u);  // 不记姿态环
}

AC_TEST(step_hot_path_makes_no_heap_requests) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  constexpr uint32_t kPlayers = 64u;
  for (uint32_t i = 0u; i < kPlayers; ++i) {
    const double angle = static_cast<double>(i) * 0.4;
    spawnPlayer(*world, 10.0 + 6.0 * std::cos(angle), 10.0 + 6.0 * std::sin(angle));
    spawnSheep(*world, -1.0 * std::sin(angle) * 8.0, 8.0 * std::cos(angle));
  }
  sim::Command commands[kPlayers];
  for (uint32_t i = 0u; i < kPlayers; ++i) {
    commands[i] = command((i % 3u == 0u) ? 1.0 : -1.0, (i % 5u == 0u) ? 1.0 : 0.0,
                          static_cast<double>(i) * 0.1);
    commands[i].buttons = (i % 2u == 0u) ? ac::config::kButtonSprint : 0u;
  }
  for (uint32_t warm = 0u; warm < 10u; ++warm) {
    AC_CHECK(sim::stepWorld(*world, commands, kPlayers, ac::config::kStepDtMs));
  }
  const ac::test::AllocationScope window;
  for (uint32_t i = 0u; i < 10000u; ++i) {
    sim::stepWorld(*world, commands, kPlayers, ac::config::kStepDtMs);
  }
  const std::size_t allocations = window.since();
  std::printf("stepWorldAllocations=%zu\n", allocations);
  AC_CHECK_EQ(allocations, 0u);
}
