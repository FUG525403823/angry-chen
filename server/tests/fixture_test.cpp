// S07 §3/§5：把 docs/evidence/fixtures 的跨语言对拍向量喂给 C++ 权威内核，逐 tick 逐字段逐位复现。
#include "fixture_io.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "config/player.hpp"
#include "sim/arena.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"
#include "tiny_test.hpp"

namespace {

using ac::sim::Command;
using ac::sim::Entity;
using ac::sim::EntityKind;
using ac::sim::World;

// v1 createWorld 的语义：世界建好后立刻在 4 个出生点各生成一名玩家（id 1..4 升序）。
// 这是 fixture 里没有记录、但两侧必须一致的世界初态（见 docs/evidence/fixtures/README.md）。
std::unique_ptr<World> createFixtureWorld(uint32_t seed) {
  std::unique_ptr<World> world = ac::sim::createWorld(seed);
  const ac::config::BaseStats& stats = ac::config::kKindBaseStats[static_cast<std::size_t>(EntityKind::kPlayer)];
  for (const ac::Vec3& spawn : ac::sim::arena::kPlayerSpawns) {
    ac::sim::SpawnParams params{};
    params.kind = EntityKind::kPlayer;
    params.pos = spawn;
    // v1 spawnEntity 的 hp/armor 取自 entity.baseStats[kind]；C++ 的 SpawnParams 是显式传参（S05 §5.2），
    // 所以这里必须显式给，否则初态就不一致（hp=0 vs 100）。
    params.hp = stats.hp;
    params.armor = stats.armor;
    ac::sim::spawnEntity(*world, params);
  }
  return world;
}

std::vector<uint16_t> ascendingPlayerIds(const World& world) {
  std::vector<uint16_t> ids;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const uint16_t id = world.activeIds[i];
    const Entity* entity = ac::sim::entityById(world, id);
    if (entity != nullptr && entity->kind == EntityKind::kPlayer) ids.push_back(id);
  }
  return ids;
}

// §5.4 比较顺序：configHash -> 逐 tick（entities 升序 -> events 按序 -> rngState 三流）。
// 返回已比较的 tick 数（configHash 不符时为 0：不跑 tick）。
uint32_t replayFixture(const ac::test::Fixture& fixture, ac::test::DiffSink& diff) {
  diff.stringField(0u, "configHash", fixture.configHash, ac::test::hashHex(ac::test::configHash()));
  if (diff.has) return 0u;

  std::unique_ptr<World> world = createFixtureWorld(fixture.seed);
  uint32_t compared = 0u;
  for (std::size_t tickIndex = 0u; tickIndex < fixture.ticks.size(); ++tickIndex) {
    const ac::test::FixtureTick& tick = fixture.ticks[tickIndex];
    const uint32_t tickNumber = static_cast<uint32_t>(tickIndex) + 1u;
    const std::vector<uint16_t> players = ascendingPlayerIds(*world);
    if (tick.commands.size() > players.size()) {
      diff.stringField(tickNumber, "commands.count", "<= command slots", std::to_string(tick.commands.size()) + " > " + std::to_string(players.size()));
      return compared;
    }
    // 命令槽位 = 升序玩家位置（v1 applyCommands 的 slot 语义）；缺命令的槽位传 nullptr -> 速度归零。
    std::vector<Command> slots;
    slots.reserve(tick.commands.size());
    for (std::size_t i = 0u; i < tick.commands.size(); ++i) {
      const ac::test::FixtureCommand& source = tick.commands[i];
      diff.intField(tickNumber, "commands[" + std::to_string(i) + "].id",
                    static_cast<int64_t>(players[i]), static_cast<int64_t>(source.id));
      Command command{};
      command.moveX = source.moveX;
      command.moveY = source.moveY;
      command.yaw = source.yaw;
      command.pitch = source.pitch;
      command.buttons = source.buttons;
      command.switchTo = source.switchTo;
      command.seq = source.seq;
      command.clientTick = source.clientTick;
      slots.push_back(command);
    }
    ac::sim::stepWorld(*world, slots.empty() ? nullptr : slots.data(), static_cast<uint32_t>(slots.size()), tick.dtMs);
    ++compared;

    // 内部不变量（非 fixture 字段）：阶段 0 的 tick / timeMs 推进。
    diff.intField(tickNumber, "world.tick", static_cast<int64_t>(tickNumber), static_cast<int64_t>(world->tick));
    diff.intField(tickNumber, "world.timeMs", static_cast<int64_t>(tickNumber) * static_cast<int64_t>(ac::config::kStepDtMs),
                  static_cast<int64_t>(world->timeMs));

    diff.intField(tickNumber, "entities.count", static_cast<int64_t>(tick.entities.size()),
                  static_cast<int64_t>(world->activeCount));
    std::size_t entityIndex = 0u;
    for (std::size_t i = 0u; i < world->activeCount; ++i) {
      const uint16_t id = world->activeIds[i];
      const Entity* entity = ac::sim::entityById(*world, id);
      if (entity == nullptr) continue;
      if (entityIndex < tick.entities.size()) {
        const ac::test::FixtureEntity& expected = tick.entities[entityIndex];
        const std::string base = "entities[" + std::to_string(entityIndex) + "]";
        diff.intField(tickNumber, base + ".id", static_cast<int64_t>(expected.id), static_cast<int64_t>(id));
        diff.stringField(tickNumber, base + ".kind", expected.kind,
                         ac::test::kindName(entity->kind));
        diff.doubleField(tickNumber, base + ".pos.x", expected.posX, entity->pos.x);
        diff.doubleField(tickNumber, base + ".pos.y", expected.posY, entity->pos.y);
        diff.doubleField(tickNumber, base + ".pos.z", expected.posZ, entity->pos.z);
        diff.doubleField(tickNumber, base + ".yaw", expected.yaw, entity->yaw);
        diff.doubleField(tickNumber, base + ".pitch", expected.pitch, entity->pitch);
        diff.intField(tickNumber, base + ".hp", static_cast<int64_t>(expected.hp), static_cast<int64_t>(entity->hp));
        diff.intField(tickNumber, base + ".flags", static_cast<int64_t>(expected.flags),
                      static_cast<int64_t>(ac::test::entityFlagsOf(*entity)));
      }
      ++entityIndex;
    }
    // 事件：本批向量 events 恒为空；载荷比较随 S08/S09 的事件结构落地（见 README 的偏差清单）。
    diff.intField(tickNumber, "events.count", static_cast<int64_t>(tick.events.size()),
                  static_cast<int64_t>(world->eventCount));
    diff.intField(tickNumber, "rngState.ai", static_cast<int64_t>(tick.rng.ai), static_cast<int64_t>(world->rng.ai.a));
    diff.intField(tickNumber, "rngState.spawn", static_cast<int64_t>(tick.rng.spawn),
                  static_cast<int64_t>(world->rng.spawn.a));
    diff.intField(tickNumber, "rngState.fx", static_cast<int64_t>(tick.rng.fx), static_cast<int64_t>(world->rng.fx.a));
  }
  return compared;
}

void runFixture(const char* name) {
  ac::test::Fixture fixture;
  std::string error;
  if (!ac::test::loadFixtureByName(name, fixture, error)) {
    AC_FAIL(error.c_str());
    return;
  }
  ac::test::DiffSink diff;
  diff.fixture = fixture.name;
  const uint32_t compared = replayFixture(fixture, diff);
  if (diff.has) {
    AC_FAIL(diff.report().c_str());
    return;
  }
  AC_CHECK_EQ(compared, static_cast<uint32_t>(fixture.ticks.size()));
  std::printf("fixture %s ticks=%u entities=%zu events=%zu hash=%s\n", name, compared,
              fixture.ticks.front().entities.size(), fixture.ticks.front().events.size(),
              fixture.configHash.c_str());
}

}  // namespace

AC_TEST(fixture_still_60t) { runFixture("still-60t"); }
AC_TEST(fixture_line_move_240t) { runFixture("straight-line-240t"); }
AC_TEST(fixture_barn_collision_400t) { runFixture("barn-collision-400t"); }
AC_TEST(fixture_fence_bounds_400t) { runFixture("fence-bounds-400t"); }

// §6 的自检：手工改一位数字必须让比较在**那一个 tick**、那一个字段上以 DIFF 失败。
AC_TEST(fixture_tampered_projection_reports_diff) {
  ac::test::Fixture fixture;
  std::string error;
  if (!ac::test::loadFixtureByName("fence-bounds-400t", fixture, error)) {
    AC_FAIL(error.c_str());
    return;
  }
  ac::test::FixtureTick& last = fixture.ticks.back();
  if (last.entities.empty()) {
    AC_FAIL("fixture has no entities to tamper");
    return;
  }
  last.entities[0].posZ = std::nextafter(last.entities[0].posZ, 1.0e9);
  ac::test::DiffSink diff;
  diff.fixture = fixture.name;
  const uint32_t compared = replayFixture(fixture, diff);
  AC_CHECK(diff.has);
  if (!diff.has) return;
  AC_CHECK_EQ(compared, static_cast<uint32_t>(fixture.ticks.size()));
  AC_CHECK_EQ(diff.tick, static_cast<uint32_t>(fixture.ticks.size()));
  AC_CHECK_EQ(diff.field, std::string("entities[0].pos.z"));
  std::printf("fixtureTamperReport=%s\n", diff.report().c_str());
}

// §6 的自检：改 configHash 必须在跑 tick 之前失败。
AC_TEST(fixture_tampered_hash_fails_before_ticks) {
  ac::test::Fixture fixture;
  std::string error;
  if (!ac::test::loadFixtureByName("still-60t", fixture, error)) {
    AC_FAIL(error.c_str());
    return;
  }
  fixture.configHash = "00000000";
  ac::test::DiffSink diff;
  diff.fixture = fixture.name;
  const uint32_t compared = replayFixture(fixture, diff);
  AC_CHECK(diff.has);
  AC_CHECK_EQ(compared, 0u);
  if (!diff.has) return;
  AC_CHECK_EQ(diff.field, std::string("configHash"));
  std::printf("fixtureHashReport=%s comparedTicks=%u\n", diff.report().c_str(), compared);
}
