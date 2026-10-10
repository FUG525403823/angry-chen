// S16 用例：补给箱补弹、波次升级表、清波发点/准备重置、补给箱随比赛生成/回收。
#include "tiny_test.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "combat/damage.hpp"
#include "combat/weapon.hpp"
#include "config/pickup.hpp"
#include "config/upgrades.hpp"
#include "config/weapons.hpp"
#include "core/math.hpp"
#include "room/room.hpp"
#include "room/rooms.hpp"
#include "room/session.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace {
constexpr uint32_t kSeed = 0x51E00000u;
constexpr uint32_t kRegistryStartMs = 0x51A7u;
namespace sim = ac::sim;

sim::EntityId spawnPlayer(sim::World& world, double x, double z) {
  return sim::spawnEntity(world, sim::EntityKind::kPlayer, ac::Vec3{x, 0.0, z}).id;
}

sim::EntityId spawnPickup(sim::World& world, double x, double z) {
  return sim::spawnEntity(world, sim::EntityKind::kPickup, ac::Vec3{x, 0.0, z}).id;
}

sim::Command interactCommand() {
  sim::Command c{};
  c.buttons = ac::config::kButtonInteract;
  return c;
}

sim::Command walkCommand() {
  sim::Command c{};
  c.moveX = 1.0;  // yaw=0 时前进方向是 +z（同 step_walk_advances_4_5_meters）
  return c;
}

void runTicks(sim::World& world, const sim::Command& c, int ticks) {
  for (int i = 0; i < ticks; ++i) {
    AC_CHECK(sim::stepWorld(world, &c, 1u, ac::config::kStepDtMs));
  }
}

sim::Entity& at(sim::World& world, sim::EntityId id) { return world.entities[id - 1u]; }

void drainAmmo(sim::Entity& player) {
  for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
    player.weapon.magInSlot[slot] = 0;
  }
  player.weapon.reserveAmmo = 0;
}

// 房间流测试的最小 harness（抄 match_flow_test.cpp 的口径）。
struct CrateHarness {
  ac::room::RoomRegistry registry{};
  std::vector<ac::room::Session> sessions{};
  ac::room::Room* room = nullptr;
  uint64_t nowMs = 100000u;
  ac::room::RoomDeps deps{};

  static void onMatchState(void*, const ac::room::Room&, ac::room::Session&,
                           const ac::net::MatchState&) {}
  static void onReplicate(void*, ac::room::Room&) {}

  CrateHarness() {
    ac::room::initRoomRegistry(registry, kRegistryStartMs);
    sessions.push_back(ac::room::createSession(1u, nowMs));
    deps.user = this;
    deps.sendMatchState = &onMatchState;
    deps.replicate = &onReplicate;
  }

  void join() {
    AC_CHECK(ac::room::setSessionName(sessions[0], "alpha"));
    const ac::room::JoinOutcome outcome = ac::room::join(registry, "0000", sessions[0], nowMs);
    AC_CHECK(outcome == ac::room::JoinOutcome::kOk);
    room = ac::room::findRoom(registry, sessions[0].roomCode);
    AC_CHECK(room != nullptr);
  }

  void advance(uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; ++i) {
      nowMs += 50u;
      AC_CHECK(ac::room::updateRoom(*room, deps, nowMs));
    }
  }

  void startMatch() {
    AC_CHECK(ac::room::roomSetReady(*room, sessions[0], true, 0u, deps));
    AC_CHECK(ac::room::tryStartMatch(*room, sessions[0]) == ac::room::StartOutcome::kOk);
    advance(30u);  // 1500ms loading
    AC_CHECK(room->phase == ac::room::MatchPhase::kPlaying);
  }
};
}  // namespace

AC_TEST(upgrade_multiplier_table_values) {
  using ac::config::upgradeDamageMultiplier;
  using ac::config::upgradeSpeedMultiplier;
  using ac::config::upgradeReloadTimeMultiplier;
  using ac::config::upgradeReserveBonus;
  AC_CHECK_EQ(upgradeDamageMultiplier(0), 1.0);
  AC_CHECK_NEAR(upgradeDamageMultiplier(1), 1.15, 1e-12);
  AC_CHECK_NEAR(upgradeDamageMultiplier(2), 1.30, 1e-12);
  AC_CHECK_NEAR(upgradeDamageMultiplier(5), 1.75, 1e-12);
  AC_CHECK_EQ(upgradeSpeedMultiplier(0), 1.0);
  AC_CHECK_NEAR(upgradeSpeedMultiplier(1), 1.08, 1e-12);
  AC_CHECK_NEAR(upgradeSpeedMultiplier(5), 1.40, 1e-12);
  AC_CHECK_EQ(upgradeReloadTimeMultiplier(0), 1.0);
  AC_CHECK_NEAR(upgradeReloadTimeMultiplier(1), 0.88, 1e-12);
  AC_CHECK_NEAR(upgradeReloadTimeMultiplier(5), 0.40, 1e-12);  // 下限 0.4，不为 0
  AC_CHECK_EQ(upgradeReserveBonus(0), 0);
  AC_CHECK_EQ(upgradeReserveBonus(3), 120);
  AC_CHECK_EQ(upgradeReserveBonus(5), 200);
  // 越界等级夹紧到 [0, 5]。
  AC_CHECK_EQ(upgradeDamageMultiplier(-1), 1.0);
  AC_CHECK_EQ(upgradeDamageMultiplier(99), upgradeDamageMultiplier(5));
  AC_CHECK_EQ(upgradeReserveBonus(-2), 0);
  AC_CHECK_EQ(upgradeReserveBonus(100), upgradeReserveBonus(5));
}

AC_TEST(upgrade_damage_multiplier_applies_to_compute_damage) {
  ac::combat::DamageResult base{}, upgraded{};
  ac::combat::computeDamage(ac::config::kWeapons[0], ac::config::HitPart::kTorso, 5.0, false, 0.0, base, 1.0);
  // 2 级伤害 = ×1.30；multipler 放在 weapon.damage 之后，位等价基线验证。
  ac::combat::computeDamage(ac::config::kWeapons[0], ac::config::HitPart::kTorso, 5.0, false, 0.0, upgraded,
                            ac::config::upgradeDamageMultiplier(2));
  AC_CHECK_NEAR(upgraded.hpDamage, base.hpDamage * 1.30, 1e-12);
  // 默认乘数 1.0 与旧口径位一致。
  AC_CHECK(base.hpDamage > 0.0);
}

AC_TEST(upgrade_reload_time_multiplier_shortens_reload) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  state.magInSlot[0] = 0;
  state.reserveAmmo = 30;
  AC_CHECK(ac::combat::tryStartReload(state, 0.0, 1.0));
  AC_CHECK_EQ(state.reloadEndsAtMs, ac::config::kWeapons[0].reloadMs);
  ac::combat::WeaponState fast;
  ac::combat::resetWeaponState(fast);
  fast.magInSlot[0] = 0;
  fast.reserveAmmo = 30;
  AC_CHECK(ac::combat::tryStartReload(fast, 0.0, ac::config::upgradeReloadTimeMultiplier(2)));
  AC_CHECK_NEAR(fast.reloadEndsAtMs, ac::config::kWeapons[0].reloadMs * 0.76, 1e-9);
}

AC_TEST(upgrade_speed_multiplier_moves_faster) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  const sim::EntityId id = spawnPlayer(*world, 20.0, 0.0);
  at(*world, id).upgrade.speedLevel = 1u;  // +8%
  runTicks(*world, walkCommand(), 20);
  // 基础 4.5 m/s × 1.08 = 4.86；50ms × 20 tick。
  AC_CHECK_NEAR(at(*world, id).pos.z, 4.5 * 1.08, 1e-9);
}

AC_TEST(upgrade_crate_refill_after_hold) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  spawnPickup(*world, 0.0, 0.0);
  const sim::EntityId player = spawnPlayer(*world, 1.0, 0.0);
  drainAmmo(at(*world, player));
  runTicks(*world, interactCommand(), 30);  // 1500ms 按住 E
  const sim::Entity& p = at(*world, player);
  for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
    AC_CHECK_EQ(p.weapon.magInSlot[slot], ac::config::kWeapons[slot].mag);
  }
  AC_CHECK_EQ(p.weapon.reserveAmmo, ac::config::kReserveAmmoInitial);
}

AC_TEST(upgrade_crate_refill_uses_upgraded_reserve_cap) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  spawnPickup(*world, 0.0, 0.0);
  const sim::EntityId player = spawnPlayer(*world, 1.0, 0.0);
  at(*world, player).upgrade.reserveLevel = 2u;  // 备弹上限 +80
  drainAmmo(at(*world, player));
  runTicks(*world, interactCommand(), 30);
  const sim::Entity& p = at(*world, player);
  AC_CHECK_EQ(p.weapon.reserveAmmo, ac::config::kReserveAmmoInitial + 80);
  for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
    AC_CHECK_EQ(p.weapon.magInSlot[slot], ac::config::kWeapons[slot].mag);
  }
}

AC_TEST(upgrade_crate_needs_hold_and_range) {
  // 松手后蓄力归零：按住 20 tick 再松手 1 tick，再按住 30 tick 仍能补满（不要求连续 50 tick）。
  {
    std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
    spawnPickup(*world, 0.0, 0.0);
    const sim::EntityId player = spawnPlayer(*world, 1.0, 0.0);
    drainAmmo(at(*world, player));
    runTicks(*world, interactCommand(), 20);
    sim::Command idle{};
    runTicks(*world, idle, 1);  // 松手
    runTicks(*world, interactCommand(), 30);
    AC_CHECK_EQ(at(*world, player).weapon.reserveAmmo, ac::config::kReserveAmmoInitial);
  }
  // 超出交互半径：不补弹。
  {
    std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
    spawnPickup(*world, 0.0, 0.0);
    const sim::EntityId player = spawnPlayer(*world, 10.0, 0.0);  // 10m 之外
    drainAmmo(at(*world, player));
    runTicks(*world, interactCommand(), 60);
    AC_CHECK_EQ(at(*world, player).weapon.reserveAmmo, 0);
  }
  // 没按交互键：不补弹。
  {
    std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
    spawnPickup(*world, 0.0, 0.0);
    const sim::EntityId player = spawnPlayer(*world, 1.0, 0.0);
    drainAmmo(at(*world, player));
    runTicks(*world, walkCommand(), 60);
    AC_CHECK_EQ(at(*world, player).weapon.reserveAmmo, 0);
  }
}

AC_TEST(upgrade_crate_spawns_at_match_start_and_recycles) {
  CrateHarness harness;
  harness.join();
  AC_CHECK_EQ(harness.room->ammoCrateCount, 0u);  // 大厅/loading 无箱
  harness.startMatch();
  AC_CHECK_EQ(harness.room->ammoCrateCount, ac::config::kAmmoCrateCount);
  // 箱子 id 在玩家（pid 1）之后，pid==EntityId 不变。
  for (uint8_t i = 0u; i < harness.room->ammoCrateCount; ++i) {
    AC_CHECK(harness.room->ammoCrateIds[i] > 1u);
    const sim::Entity& crate = harness.room->world->entities[harness.room->ammoCrateIds[i] - 1u];
    AC_CHECK(crate.kind == sim::EntityKind::kPickup);
    AC_CHECK(crate.active);
  }
  // 重开一局（ended → resetMatchForRestart）→ 旧箱回收、升级清零。
  ac::room::Room& room = *harness.room;
  sim::Entity* const player = ac::room::playerEntityAt(room, 1u);
  AC_CHECK(player != nullptr);
  player->upgrade.points = 3u;
  player->upgrade.damageLevel = 2u;
  room.phase = ac::room::MatchPhase::kEnded;
  AC_CHECK(ac::room::tryStartMatch(room, harness.sessions[0]) == ac::room::StartOutcome::kOk);
  AC_CHECK_EQ(room.ammoCrateCount, 0u);
  AC_CHECK_EQ(player->upgrade.points, 0u);
  AC_CHECK_EQ(player->upgrade.damageLevel, 0u);
}

AC_TEST(upgrade_wave_clear_grants_point_and_resets_ready) {
  CrateHarness harness;
  harness.join();
  harness.startMatch();
  ac::room::Room& room = *harness.room;
  AC_CHECK_EQ(harness.sessions[0].ready, true);
  sim::Entity* const entity = ac::room::playerEntityAt(room, 1u);
  AC_CHECK(entity != nullptr);
  AC_CHECK_EQ(entity->upgrade.points, 0u);
  AC_CHECK(ac::room::handleWaveCleared(room, harness.nowMs));
  AC_CHECK(room.phase == ac::room::MatchPhase::kIntermission);
  AC_CHECK_EQ(entity->upgrade.points, 1u);  // 清波发 1 点
  AC_CHECK_EQ(harness.sessions[0].ready, false);  // 准备重置，需重按才能提前开波
  // 再清一波进下一波间：点累积。
  room.phase = ac::room::MatchPhase::kPlaying;
  AC_CHECK(ac::room::handleWaveCleared(room, harness.nowMs));
  AC_CHECK_EQ(entity->upgrade.points, 2u);
  AC_CHECK(room.phase == ac::room::MatchPhase::kIntermission);
}
