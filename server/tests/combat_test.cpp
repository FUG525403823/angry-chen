// S08 §6/§7：战斗、武器、弹道、伤害、倒地与怒气的用例（≥26 条；逐条对齐 v1 的常量与运算顺序）。
#include "tiny_test.hpp"

#include <cmath>
#include <cstdint>
#include <memory>

#include "allocation_probe.hpp"
#include "combat/damage.hpp"
#include "combat/downed.hpp"
#include "combat/rage.hpp"
#include "combat/raycast.hpp"
#include "combat/resolve.hpp"
#include "combat/weapon.hpp"
#include "config/combat.hpp"
#include "config/player.hpp"
#include "config/weapons.hpp"
#include "net/codec.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace {

constexpr uint32_t kSeed = 20250808u;
// v1 的 yaw=0 指向 +Z（forwardFromYaw = (sin, 0, cos)）：朝 +X 射击必须用 yaw = pi/2。
constexpr double kYawX = ac::kPi / 2.0;
using ac::sim::Command;
using ac::sim::Entity;
using ac::sim::EntityId;
using ac::sim::EntityKind;
using ac::sim::SpawnParams;
using ac::sim::World;

std::unique_ptr<World> makeWorld() { return ac::sim::createWorld(kSeed); }

EntityId addPlayer(World& world, double x, double z, uint8_t team = 0u, double yaw = 0.0, double hp = 100.0,
                   double armor = 50.0) {
  SpawnParams params{};
  params.kind = EntityKind::kPlayer;
  params.pos = ac::Vec3{x, 0.0, z};
  params.yaw = yaw;
  params.team = team;
  params.hp = hp;
  params.armor = armor;
  return ac::sim::spawnEntity(world, params).id;
}

EntityId addSheep(World& world, double x, double z, double hp = 100.0, double armor = 0.0,
                  uint8_t sheepKind = 0u, double yaw = 0.0) {
  SpawnParams params{};
  params.kind = EntityKind::kSheep;
  params.pos = ac::Vec3{x, 0.0, z};
  params.yaw = yaw;
  params.team = 1u;
  params.hp = hp;
  params.armor = armor;
  const ac::sim::SpawnResult result = ac::sim::spawnEntity(world, params);
  if (result.isOk) ac::sim::entityById(world, result.id)->sheepKind = sheepKind;
  return result.id;
}

Command fireCommand(uint16_t seq, double yaw = 0.0, double pitch = 0.0, uint8_t extraButtons = 0u) {
  Command command{};
  command.seq = seq;
  command.yaw = yaw;
  command.pitch = pitch;
  command.buttons = static_cast<uint8_t>(ac::config::kButtonFire | extraButtons);
  return command;
}

Command idleCommand(uint8_t buttons = 0u) {
  Command command{};
  command.buttons = buttons;
  return command;
}

std::size_t countEvents(const World& world, uint8_t type) {
  std::size_t count = 0u;
  for (std::size_t i = 0u; i < world.eventCount; ++i) {
    if (world.events[i].type == type) ++count;
  }
  return count;
}

const ac::sim::Event* findEvent(const World& world, uint8_t type, std::size_t occurrence = 0u) {
  std::size_t seen = 0u;
  for (std::size_t i = 0u; i < world.eventCount; ++i) {
    if (world.events[i].type != type) continue;
    if (seen == occurrence) return &world.events[i];
    ++seen;
  }
  return nullptr;
}

// 从 (eyeX, 1.6, eyeZ) 瞄准 (targetX, targetY, targetZ) 的俯仰角（用例里用 std::atan2 做预言，允许）。
double pitchTowards(double eyeX, double eyeZ, double targetX, double targetY, double targetZ) {
  const double dx = targetX - eyeX;
  const double dz = targetZ - eyeZ;
  const double horizontal = std::sqrt(dx * dx + dz * dz);
  return std::atan2(targetY - ac::config::kEyeHeightM, horizontal);
}

}  // namespace

// ---------- §5.1 / §5.2 武器表、射速与抖动 ----------

AC_TEST(combat_weapon_table_matches_v1) {
  AC_CHECK_EQ(ac::config::kWeaponSlotCount, 3);
  const ac::config::WeaponDef& pistol = ac::config::kWeapons[0];
  AC_CHECK_EQ(pistol.damage, 25.0);
  AC_CHECK_EQ(pistol.pellets, 1);
  AC_CHECK_EQ(pistol.rpm, 300.0);
  AC_CHECK(!pistol.isAuto);
  AC_CHECK_EQ(pistol.mag, 12);
  AC_CHECK_EQ(pistol.reloadMs, 1400.0);
  AC_CHECK_EQ(pistol.spreadDeg, 0.8);
  AC_CHECK_EQ(pistol.falloffStartM, 30.0);
  AC_CHECK_EQ(pistol.falloffPerM, 0.0);
  AC_CHECK_EQ(pistol.headshotMultiplier, 2.0);
  const ac::config::WeaponDef& rifle = ac::config::kWeapons[1];
  AC_CHECK_EQ(rifle.damage, 20.0);
  AC_CHECK(rifle.isAuto);
  AC_CHECK_EQ(rifle.mag, 30);
  AC_CHECK_EQ(rifle.reloadMs, 2000.0);
  AC_CHECK_EQ(rifle.spreadDeg, 0.6);
  AC_CHECK_EQ(rifle.falloffStartM, 40.0);
  const ac::config::WeaponDef& shotgun = ac::config::kWeapons[2];
  AC_CHECK_EQ(shotgun.damage, 12.0);
  AC_CHECK_EQ(shotgun.pellets, 8);
  AC_CHECK_EQ(shotgun.rpm, 70.0);
  AC_CHECK_EQ(shotgun.mag, 6);
  AC_CHECK_EQ(shotgun.reloadMs, 2600.0);
  AC_CHECK_EQ(shotgun.spreadDeg, 4.0);
  AC_CHECK_EQ(shotgun.falloffStartM, 12.0);
  AC_CHECK_EQ(ac::config::kReserveAmmoInitial, 120);
  AC_CHECK_EQ(ac::config::kSpreadGrowthPerShotDeg, 0.1);
  AC_CHECK_EQ(ac::config::kSpreadMaxDeg, 0.25);
  AC_CHECK_EQ(ac::config::kSpreadDecayDelayMs, 350u);
  AC_CHECK_EQ(ac::config::kSpreadDecayPerSecondDeg, 6.0);
  AC_CHECK_EQ(ac::config::kRecoilPitchPerShotDeg, 0.35);
  AC_CHECK_EQ(ac::config::kRecoilYawJitterDeg, 0.2);
  AC_CHECK_EQ(ac::config::kShotMaxDistanceM, 160.0);
  AC_CHECK_EQ(ac::config::kEyeHeightM, 1.6);
  AC_CHECK_EQ(ac::config::kAimPitchLimitRad, 1.5533);
}

AC_TEST(combat_rpm_interval_formula) {
  AC_CHECK_EQ(ac::config::rpmToIntervalMs(300.0), 200.0);
  AC_CHECK_EQ(ac::config::rpmToIntervalMs(600.0), 100.0);
  AC_CHECK_EQ(ac::config::rpmToIntervalMs(70.0), 60000.0 / 70.0);
  AC_CHECK_EQ(ac::config::rpmToIntervalMs(600.0, 1.25), 80.0);
  AC_CHECK(std::isinf(ac::config::rpmToIntervalMs(0.0)));
  AC_CHECK(std::isinf(ac::config::rpmToIntervalMs(-1.0)));
}

AC_TEST(combat_jitter_matches_v1_vectors) {
  // 参考值由 v1 自己的 config/weapons.ts signedJitter 现算（node 一次性导出后钉在这里）。
  AC_CHECK_EQ(ac::config::signedJitter(0u, 0x9e3u), 0.48450629133731127);
  AC_CHECK_EQ(ac::config::signedJitter(0u, 0x51fu), -0.7551555768586695);
  AC_CHECK_EQ(ac::config::signedJitter(1u, 0x9e3u), 0.353745027910918);
  AC_CHECK_EQ(ac::config::signedJitter(1u, 0x51fu), -0.10930804442614317);
  AC_CHECK_EQ(ac::config::signedJitter(2u, 0x9e3u), -0.34515067655593157);
  AC_CHECK_EQ(ac::config::signedJitter(2u, 0x51fu), -0.19227918703109026);
  AC_CHECK_EQ(ac::config::signedJitter(7u, 0x9e3u), -0.03742089169099927);
  AC_CHECK_EQ(ac::config::signedJitter(7u, 0x51fu), 0.9284353018738329);
  AC_CHECK_EQ(ac::config::signedJitter(250u, 0x9e3u), 0.08351995190605521);
  AC_CHECK_EQ(ac::config::signedJitter(250u, 0x51fu), -0.22820942848920822);
  AC_CHECK_EQ(ac::config::unitJitter(0u, 0u), 0.0);
  AC_CHECK_EQ(ac::config::unitJitter(1u, 0u), 0.19553080783225596);
}

// ---------- §5.1 武器状态机 ----------

AC_TEST(combat_weapon_state_initial) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  AC_CHECK_EQ(state.activeSlot, 0u);
  AC_CHECK_EQ(state.magInSlot[0], 12);
  AC_CHECK_EQ(state.magInSlot[1], 30);
  AC_CHECK_EQ(state.magInSlot[2], 6);
  AC_CHECK_EQ(state.reserveAmmo, 120);
  AC_CHECK_EQ(state.spreadDeg, 0.0);
  AC_CHECK_EQ(state.nextFireAllowedAtMs, 0.0);
  AC_CHECK(!ac::combat::isReloading(state));
  AC_CHECK_EQ(ac::combat::activeMag(state), 12);
  AC_CHECK_EQ(ac::combat::activeWeaponDef(state).damage, 25.0);
}

AC_TEST(combat_fire_throttles_by_interval) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  AC_CHECK(ac::combat::tryFire(state, 0.0));
  AC_CHECK_EQ(state.magInSlot[0], 11);
  AC_CHECK_EQ(state.nextFireAllowedAtMs, 200.0);
  AC_CHECK(!ac::combat::tryFire(state, 199.0));
  AC_CHECK(!ac::combat::tryFire(state, 199.999));
  AC_CHECK(ac::combat::tryFire(state, 200.0));
  AC_CHECK_EQ(state.magInSlot[0], 10);
  AC_CHECK_EQ(state.nextFireAllowedAtMs, 400.0);
  // 空仓阻止开火（不推进 nextFireAllowedAtMs）
  state.magInSlot[0] = 0;
  AC_CHECK(!ac::combat::tryFire(state, 400.0));
  AC_CHECK_EQ(state.nextFireAllowedAtMs, 400.0);
}

AC_TEST(combat_fire_blocked_during_reload) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  AC_CHECK(ac::combat::tryFire(state, 0.0));
  AC_CHECK(ac::combat::tryStartReload(state, 0.0));
  AC_CHECK_EQ(state.reloadEndsAtMs, 1400.0);
  AC_CHECK(ac::combat::isReloading(state));
  AC_CHECK(!ac::combat::tryFire(state, 100.0));
  AC_CHECK_EQ(ac::combat::reloadRemainingMs(state, 400.0), 1000.0);
  AC_CHECK_EQ(ac::combat::reloadRemainingMs(state, 1400.0), 0.0);
}

AC_TEST(combat_reload_refills_from_reserve) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  AC_CHECK(ac::combat::tryFire(state, 0.0));
  AC_CHECK(ac::combat::tryFire(state, 200.0));
  AC_CHECK(ac::combat::tryFire(state, 400.0));
  AC_CHECK_EQ(state.magInSlot[0], 9);
  AC_CHECK(ac::combat::tryStartReload(state, 400.0));
  AC_CHECK(!ac::combat::updateWeapon(state, 1799.0, 50.0));
  AC_CHECK(ac::combat::updateWeapon(state, 1800.0, 50.0));
  AC_CHECK_EQ(state.magInSlot[0], 12);
  AC_CHECK_EQ(state.reserveAmmo, 117);
  AC_CHECK(!ac::combat::isReloading(state));
}

AC_TEST(combat_reload_partial_when_reserve_short) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  state.magInSlot[0] = 9;
  state.reserveAmmo = 2;
  AC_CHECK(ac::combat::tryStartReload(state, 0.0));
  AC_CHECK(ac::combat::updateWeapon(state, 1400.0, 50.0));
  AC_CHECK_EQ(state.magInSlot[0], 11);
  AC_CHECK_EQ(state.reserveAmmo, 0);
}

AC_TEST(combat_reload_rejected_when_full_or_empty_reserve) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  AC_CHECK(!ac::combat::tryStartReload(state, 0.0));  // 满仓
  state.magInSlot[0] = 0;
  state.reserveAmmo = 0;
  AC_CHECK(!ac::combat::tryStartReload(state, 0.0));  // 无备弹
  state.reserveAmmo = 5;
  AC_CHECK(ac::combat::tryStartReload(state, 0.0));
  AC_CHECK(!ac::combat::tryStartReload(state, 10.0));  // 已在换弹
}

AC_TEST(combat_cancel_reload_and_switch_slot) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  state.magInSlot[0] = 5;
  AC_CHECK(ac::combat::tryStartReload(state, 0.0));
  ac::combat::cancelReload(state);
  AC_CHECK(!ac::combat::isReloading(state));
  AC_CHECK(!ac::combat::switchSlot(state, 0u, 0.0));   // 同槽不变
  AC_CHECK(!ac::combat::switchSlot(state, 3u, 0.0));   // 越界拒绝
  AC_CHECK(ac::combat::switchSlot(state, 1u, 100.0));
  AC_CHECK_EQ(state.activeSlot, 1u);
  AC_CHECK_EQ(ac::combat::activeMag(state), 30);
  AC_CHECK_EQ(ac::combat::activeWeaponDef(state).damage, 20.0);
  // 换枪清换弹、并把 nextFireAllowedAtMs 抬到当前时刻（不早于 nowMs）
  state.nextFireAllowedAtMs = 50.0;
  AC_CHECK(ac::combat::switchSlot(state, 2u, 100.0));
  AC_CHECK_EQ(state.activeSlot, 2u);
  AC_CHECK_EQ(state.nextFireAllowedAtMs, 100.0);
  AC_CHECK_EQ(ac::combat::activeMag(state), 6);
}

AC_TEST(combat_spread_grows_and_caps) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  AC_CHECK(ac::combat::tryFire(state, 0.0));
  AC_CHECK_EQ(state.spreadDeg, 0.1);
  AC_CHECK(ac::combat::tryFire(state, 200.0));
  AC_CHECK_EQ(state.spreadDeg, 0.2);
  AC_CHECK(ac::combat::tryFire(state, 400.0));
  AC_CHECK_EQ(state.spreadDeg, 0.25);
  AC_CHECK(ac::combat::tryFire(state, 600.0));
  AC_CHECK_EQ(state.spreadDeg, 0.25);
}

AC_TEST(combat_spread_decays_after_delay) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  AC_CHECK(ac::combat::tryFire(state, 0.0));
  AC_CHECK_EQ(state.spreadDeg, 0.1);
  // 距最后一发 < 350ms：不衰减（lastShotAt = nextFireAllowedAtMs - interval = 0）
  AC_CHECK(!ac::combat::updateWeapon(state, 349.0, 50.0));
  AC_CHECK_EQ(state.spreadDeg, 0.1);
  // 距最后一发 >= 350ms：按 6 度/秒 衰减
  AC_CHECK(!ac::combat::updateWeapon(state, 350.0, 50.0));
  AC_CHECK_EQ(state.spreadDeg, 0.0);  // 0.1 - 6*50/1000 = -0.2 -> 夹到 0
  // 衰减到 0 就停住，不会变负
  ac::combat::updateWeapon(state, 1000.0, 1000.0);
  AC_CHECK_EQ(state.spreadDeg, 0.0);
}

// ---------- §5.5 伤害 ----------

AC_TEST(combat_damage_by_part_pistol) {
  ac::combat::DamageResult out{};
  ac::combat::computeDamage(ac::config::kWeapons[0], ac::config::HitPart::kTorso, 5.0, false, 0.0, out);
  AC_CHECK_EQ(out.hpDamage, 25.0);
  AC_CHECK_EQ(out.armorDamage, 0.0);
  AC_CHECK(!out.isHeadshot);
  ac::combat::computeDamage(ac::config::kWeapons[0], ac::config::HitPart::kHead, 5.0, false, 0.0, out);
  AC_CHECK_EQ(out.hpDamage, 50.0);
  AC_CHECK(out.isHeadshot);
  ac::combat::computeDamage(ac::config::kWeapons[0], ac::config::HitPart::kLimb, 5.0, false, 0.0, out);
  AC_CHECK_EQ(out.hpDamage, 18.75);
}

AC_TEST(combat_damage_armor_absorbs_first) {
  ac::combat::DamageResult out{};
  ac::combat::computeDamage(ac::config::kWeapons[1], ac::config::HitPart::kTorso, 5.0, false, 50.0, out);
  AC_CHECK_EQ(out.armorDamage, 12.0);
  AC_CHECK_EQ(out.hpDamage, 8.0);
  ac::combat::computeDamage(ac::config::kWeapons[0], ac::config::HitPart::kTorso, 5.0, false, 50.0, out);
  AC_CHECK_EQ(out.armorDamage, 15.0);
  AC_CHECK_EQ(out.hpDamage, 10.0);
  AC_CHECK_EQ(out.armorDamage + out.hpDamage, 25.0);
  // 护甲为 0 时全部打到生命
  ac::combat::computeDamage(ac::config::kWeapons[0], ac::config::HitPart::kTorso, 5.0, false, 0.0, out);
  AC_CHECK_EQ(out.armorDamage, 0.0);
  AC_CHECK_EQ(out.hpDamage, 25.0);
}

AC_TEST(combat_damage_falloff_clamps) {
  ac::config::WeaponDef synthetic = ac::config::kWeapons[0];
  synthetic.damage = 100.0;
  synthetic.falloffStartM = 10.0;
  synthetic.falloffPerM = 0.02;
  ac::combat::DamageResult out{};
  ac::combat::computeDamage(synthetic, ac::config::HitPart::kTorso, 5.0, false, 0.0, out);
  AC_CHECK_EQ(out.hpDamage, 100.0);
  ac::combat::computeDamage(synthetic, ac::config::HitPart::kTorso, 20.0, false, 0.0, out);
  AC_CHECK_EQ(out.hpDamage, 80.0);
  // 下限 0.5：100 * (1 - 0.02*90) = -0.8 -> 夹到 0.5
  ac::combat::computeDamage(synthetic, ac::config::HitPart::kTorso, 100.0, false, 0.0, out);
  AC_CHECK_EQ(out.hpDamage, 50.0);
}

AC_TEST(combat_damage_rage_multiplier) {
  ac::combat::DamageResult plain{};
  ac::combat::DamageResult raged{};
  ac::combat::computeDamage(ac::config::kWeapons[1], ac::config::HitPart::kTorso, 5.0, false, 0.0, plain);
  ac::combat::computeDamage(ac::config::kWeapons[1], ac::config::HitPart::kTorso, 5.0, true, 0.0, raged);
  AC_CHECK_EQ(plain.hpDamage, 20.0);
  AC_CHECK_EQ(raged.hpDamage, 26.0);
  AC_CHECK_EQ(ac::config::kRageDamageMultiplier, 1.3);
}

// ---------- §5.3 射线求交 ----------

AC_TEST(combat_ray_sphere_hit_and_miss) {
  ac::combat::RayHit hit;
  ac::combat::rayVsSphere(0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.0, 0.0, 1.0, 160.0, hit);
  AC_CHECK(hit.hit);
  AC_CHECK_EQ(hit.t, 4.0);
  AC_CHECK_EQ(hit.x, 4.0);
  AC_CHECK_EQ(hit.nx, -1.0);
  ac::combat::rayVsSphere(0.0, 0.0, 0.0, -1.0, 0.0, 0.0, 5.0, 0.0, 0.0, 1.0, 160.0, hit);
  AC_CHECK(!hit.hit);
  ac::combat::rayVsSphere(0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.0, 0.0, 1.0, 3.0, hit);
  AC_CHECK(!hit.hit);  // 超出 maxDist
  ac::combat::rayVsSphere(5.0, 0.0, 0.0, 1.0, 0.0, 0.0, 5.0, 0.0, 0.0, 1.0, 160.0, hit);
  AC_CHECK(hit.hit);
  AC_CHECK_EQ(hit.t, 0.0);  // 起点在球内：t 夹到 0
}

AC_TEST(combat_ray_aabb_slab_axes) {
  ac::combat::RayHit hit;
  ac::combat::rayVsAabb(-5.0, 0.5, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, -1.0, 2.0, 1.0, 1.0, 160.0, hit);
  AC_CHECK(hit.hit);
  AC_CHECK_EQ(hit.t, 6.0);
  AC_CHECK_EQ(hit.nx, -1.0);
  AC_CHECK_EQ(hit.ny, 0.0);
  ac::combat::rayVsAabb(-5.0, 2.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, -1.0, 2.0, 1.0, 1.0, 160.0, hit);
  AC_CHECK(!hit.hit);  // 平行且在外
  ac::combat::rayVsAabb(0.0, 0.5, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, -1.0, 2.0, 1.0, 1.0, 160.0, hit);
  AC_CHECK(hit.hit);
  AC_CHECK_EQ(hit.t, 1.0);
  AC_CHECK_EQ(hit.nx, -1.0);
}

AC_TEST(combat_ray_capsule_side_and_cap) {
  ac::combat::RayHit hit;
  // 竖直胶囊 (0,1,0) 半径 0.5：水平射线从 x=-5 打侧面
  ac::combat::rayVsCapsule(-5.0, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.5, 160.0, hit);
  AC_CHECK(hit.hit);
  AC_CHECK_EQ(hit.t, 4.5);
  AC_CHECK_EQ(hit.nx, -1.0);
  AC_CHECK_EQ(hit.ny, 0.0);
  // 从上往下打端盖
  ac::combat::rayVsCapsule(0.0, 10.0, 0.0, 0.0, -1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.5, 160.0, hit);
  AC_CHECK(hit.hit);
  AC_CHECK_EQ(hit.t, 8.5);
  AC_CHECK_EQ(hit.ny, 1.0);
  // 擦不到：偏移 0.6 > 半径 0.5
  ac::combat::rayVsCapsule(-5.0, 1.0, 0.6, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.5, 160.0, hit);
  AC_CHECK(!hit.hit);
}

AC_TEST(combat_trace_player_capsule_part) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId shooter = addPlayer(*world, -5.0, 20.0, 0u, 0.0);
  const EntityId target = addPlayer(*world, 0.0, 20.0, 1u, 0.0);
  ac::sim::ShotTrace trace;
  ac::sim::createShotTrace(trace);
  // 玩家胶囊 = 线段 [y+0.4, y+1.3] + 半径 0.4（顶端半球到 1.7）：瞄 1.55 打端盖，
  // 1.55/1.7 = 0.912 >= 0.85 -> 头部。
  const ac::Vec3 headDir = ac::yawPitchToDirection(kYawX, pitchTowards(-5.0, 20.0, 0.0, 1.55, 20.0));
  ac::sim::traceRay(*world, nullptr, shooter, -5.0, ac::config::kEyeHeightM, 20.0, headDir.x, headDir.y,
                    headDir.z, 160.0, 0.0, trace);
  AC_CHECK(trace.hit);
  AC_CHECK_EQ(trace.targetId, target);
  AC_CHECK(trace.targetKind == EntityKind::kPlayer);
  AC_CHECK(trace.part == ac::config::HitPart::kHead);
  AC_CHECK(trace.distanceM > 4.0 && trace.distanceM < 5.0);
  // 压到腰部高度（0.5/1.7 = 0.294 < 0.4）-> 四肢
  const ac::Vec3 direction = ac::yawPitchToDirection(kYawX, pitchTowards(-5.0, 20.0, 0.0, 0.5, 20.0));
  ac::sim::traceRay(*world, nullptr, shooter, -5.0, ac::config::kEyeHeightM, 20.0, direction.x, direction.y,
                    direction.z, 160.0, 0.0, trace);
  AC_CHECK(trace.hit);
  AC_CHECK(trace.part == ac::config::HitPart::kLimb);
}

AC_TEST(combat_trace_barn_blocks_shot) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId shooter = addPlayer(*world, -20.0, 0.0, 0u, 0.0);
  addPlayer(*world, 20.0, 0.0, 1u, 0.0);
  ac::sim::ShotTrace trace;
  ac::sim::createShotTrace(trace);
  // 谷仓占 x/z ∈ [-4, 4]、y ∈ [0, 5]：平射穿过谷仓 -> 打不到后面的目标
  ac::sim::traceRay(*world, nullptr, shooter, -20.0, ac::config::kEyeHeightM, 0.0, 1.0, 0.0, 0.0, 160.0, 0.0,
                    trace);
  AC_CHECK(!trace.hit);
  // 同一条平射搬到 z = 20 的空车道：能打到那一侧的另一个目标，
  // 证明上一条是「被谷仓挡住」而不是「本来就没打中」。
  const EntityId far = addPlayer(*world, 20.0, 20.0, 1u, 0.0);
  ac::sim::traceRay(*world, nullptr, shooter, -20.0, ac::config::kEyeHeightM, 20.0, 1.0, 0.0, 0.0, 160.0, 0.0,
                    trace);
  AC_CHECK(trace.hit);
  AC_CHECK_EQ(trace.targetId, far);
  AC_CHECK(trace.distanceM > 39.0);
}

AC_TEST(combat_trace_sheep_boxes) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId shooter = addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  const EntityId sheep = addSheep(*world, 0.0, 20.0, 100.0, 0.0, 0u, 0.0);
  ac::sim::ShotTrace trace;
  ac::sim::createShotTrace(trace);
  // 躯干盒（局部 z ∈ [-0.66, 0.66]）内、局部高度 0.56 ∈ [0.34, 0.78) -> torso
  const double bodyPitch = pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0);
  ac::Vec3 direction = ac::yawPitchToDirection(kYawX, bodyPitch);
  ac::sim::traceRay(*world, nullptr, shooter, -6.0, ac::config::kEyeHeightM, 20.0, direction.x, direction.y,
                    direction.z, 160.0, 0.0, trace);
  AC_CHECK(trace.hit);
  AC_CHECK_EQ(trace.targetId, sheep);
  AC_CHECK(trace.targetKind == EntityKind::kSheep);
  AC_CHECK(trace.part == ac::config::HitPart::kTorso);
  // 局部高度 0.9 >= headMinM 0.78 -> 头部（从背后打也算头，v1 的口径就是高度阈值）
  const double headPitch = pitchTowards(-6.0, 20.0, 0.0, 0.9, 20.0);
  direction = ac::yawPitchToDirection(kYawX, headPitch);
  ac::sim::traceRay(*world, nullptr, shooter, -6.0, ac::config::kEyeHeightM, 20.0, direction.x, direction.y,
                    direction.z, 160.0, 0.0, trace);
  AC_CHECK(trace.hit);
  AC_CHECK(trace.part == ac::config::HitPart::kHead);
  // 局部高度 0.2 < torsoMinM 0.34 -> 四肢
  const double limbPitch = pitchTowards(-6.0, 20.0, 0.0, 0.05, 20.0);
  direction = ac::yawPitchToDirection(kYawX, limbPitch);
  ac::sim::traceRay(*world, nullptr, shooter, -6.0, ac::config::kEyeHeightM, 20.0, direction.x, direction.y,
                    direction.z, 160.0, 0.0, trace);
  AC_CHECK(trace.hit);
  AC_CHECK(trace.part == ac::config::HitPart::kLimb);
  // 前伸头盒：站在羊正前方（局部 +Z）平射，头部盒先被命中
  const EntityId frontShooter = addPlayer(*world, 0.0, 26.0, 0u, 0.0);
  const ac::Vec3 back = ac::yawPitchToDirection(ac::kPi, 0.0);  // 朝 -Z
  ac::sim::traceRay(*world, nullptr, frontShooter, 0.0, 1.0, 26.0, back.x, back.y, back.z, 160.0, 0.0, trace);
  AC_CHECK(trace.hit);
  AC_CHECK(trace.part == ac::config::HitPart::kHead);
}

// ---------- §5.6 权威结算（整步进）----------

AC_TEST(combat_step_player_hits_sheep_torso) {
  std::unique_ptr<World> world = makeWorld();
  addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  const EntityId sheep = addSheep(*world, 0.0, 20.0);
  const Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0));
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const Entity* target = ac::sim::entityById(*world, sheep);
  AC_CHECK(target != nullptr);
  if (target != nullptr) {
    AC_CHECK_EQ(target->hp, 75.0);
    AC_CHECK_EQ(target->armor, 0.0);
  }
  AC_CHECK_EQ(countEvents(*world, ac::sim::kEventPlayerHit), 1u);
  const ac::sim::Event* hit = findEvent(*world, ac::sim::kEventPlayerHit);
  AC_CHECK(hit != nullptr);
  if (hit != nullptr) {
    AC_CHECK_EQ(hit->value, 25.0);
    AC_CHECK_EQ(hit->flags, 0u);
    AC_CHECK_EQ(hit->targetId, sheep);
    AC_CHECK_EQ(hit->tick, world->tick);
    AC_CHECK_NEAR(hit->z, 20.0, 0.1);  // 命中点含散布抖动（手枪 0.8 度）
  }
}

AC_TEST(combat_step_armor_then_hp_and_event_value) {
  std::unique_ptr<World> world = makeWorld();
  addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  const EntityId sheep = addSheep(*world, 0.0, 20.0, 100.0, 50.0);
  const Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0));
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const Entity* target = ac::sim::entityById(*world, sheep);
  AC_CHECK(target != nullptr);
  if (target != nullptr) {
    AC_CHECK_EQ(target->armor, 35.0);
    AC_CHECK_EQ(target->hp, 90.0);
  }
  const ac::sim::Event* hit = findEvent(*world, ac::sim::kEventPlayerHit);
  AC_CHECK(hit != nullptr);
  if (hit != nullptr) AC_CHECK_EQ(hit->value, 25.0);
}

AC_TEST(combat_step_kill_despawns_and_grants_rage) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId shooter = addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  const EntityId sheep = addSheep(*world, 0.0, 20.0, 10.0, 0.0);
  const Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0));
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const Entity* target = ac::sim::entityById(*world, sheep);
  AC_CHECK(target == nullptr);  // 击杀即 despawn：旧 id 不再能取到实体
  const ac::sim::Event* killed = findEvent(*world, ac::sim::kEventSheepKilled);
  AC_CHECK(killed != nullptr);
  if (killed != nullptr) {
    AC_CHECK_EQ(killed->kind, 0u);
    AC_CHECK_EQ(killed->targetId, sheep);
    AC_CHECK_EQ(killed->value, 25.0);  // 击杀事件带的是这一击的总伤害
  }
  const Entity* gunner = ac::sim::entityById(*world, shooter);
  AC_CHECK(gunner != nullptr);
  if (gunner != nullptr) {
    AC_CHECK_EQ(gunner->rage.value, 8.0);
    AC_CHECK_EQ(gunner->rage.lastCombatAtMs, 50.0);
  }
}

AC_TEST(combat_step_headshot_flag_and_elite_rage) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId shooter = addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  const EntityId sheep = addSheep(*world, 0.0, 20.0, 50.0, 0.0, 0u);
  // 精英羊：state = SHEEP_ELITE_STATE(2)，头部一发 50 点直接击杀 -> 20 * 2(爆头) = 40
  ac::sim::entityById(*world, sheep)->state = ac::config::kSheepEliteState;
  const Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.95, 20.0));
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const ac::sim::Event* killed = findEvent(*world, ac::sim::kEventSheepKilled);
  AC_CHECK(killed != nullptr);
  if (killed != nullptr) AC_CHECK_EQ(killed->flags, ac::config::kHitFlagHeadshot | ac::config::kHitFlagKilled);
  const Entity* gunner = ac::sim::entityById(*world, shooter);
  AC_CHECK(gunner != nullptr);
  if (gunner != nullptr) AC_CHECK_EQ(gunner->rage.value, 40.0);
}

AC_TEST(combat_friendly_fire_disabled) {
  std::unique_ptr<World> world = makeWorld();
  addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  const EntityId mate = addPlayer(*world, 0.0, 20.0, 0u, 0.0);
  const Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 1.6, 20.0));
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  AC_CHECK_EQ(ac::sim::entityById(*world, mate)->hp, 100.0);
  AC_CHECK_EQ(countEvents(*world, ac::sim::kEventPlayerHit), 0u);
  AC_CHECK_EQ(world->stats.aliveSheep, 0u);
}

AC_TEST(combat_switch_slot_command_uses_rifle) {
  std::unique_ptr<World> world = makeWorld();
  addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  addSheep(*world, 0.0, 20.0);
  Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0),
                                ac::config::kButtonSwitchWeapon);
  command.switchTo = 1u;
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const Entity* player = ac::sim::entityById(*world, 1u);
  AC_CHECK(player != nullptr);
  if (player != nullptr) {
    AC_CHECK_EQ(player->weapon.activeSlot, 1u);
    AC_CHECK_EQ(player->weapon.magInSlot[1], 29);
  }
  const ac::sim::Event* hit = findEvent(*world, ac::sim::kEventPlayerHit);
  AC_CHECK(hit != nullptr);
  if (hit != nullptr) AC_CHECK_EQ(hit->value, 20.0);
}

AC_TEST(combat_down_then_revive_three_seconds) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId victim = addPlayer(*world, 0.0, 20.0, 0u, 0.0, 10.0, 0.0);
  const EntityId reviver = addPlayer(*world, 1.0, 20.0, 0u, 0.0, 100.0, 50.0);
  addPlayer(*world, -6.0, 20.0, 1u, 0.0, 100.0, 50.0);
  Command commands[3];
  commands[0] = idleCommand();
  commands[1] = idleCommand();
  commands[2] = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 1.0, 20.0));
  AC_CHECK(ac::sim::stepWorld(*world, commands, 3u, 50u));
  const Entity* downed = ac::sim::entityById(*world, victim);
  AC_CHECK(downed != nullptr);
  if (downed != nullptr) {
    AC_CHECK_EQ(downed->hp, 0.0);
    AC_CHECK(downed->downed.downed);
    AC_CHECK(downed->active);  // 倒地不是死亡：不 despawn
  }
  AC_CHECK_EQ(countEvents(*world, ac::sim::kEventPlayerDowned), 1u);

  // 救援：同队、静止、距离 1m 内、按住交互键，3 秒（60 tick）完成
  for (int tick = 0; tick < 60; ++tick) {
    commands[0] = idleCommand();
    commands[1] = idleCommand(ac::config::kButtonInteract);
    AC_CHECK(ac::sim::stepWorld(*world, commands, 2u, 50u));
  }
  const Entity* revived = ac::sim::entityById(*world, victim);
  AC_CHECK(revived != nullptr);
  if (revived != nullptr) {
    AC_CHECK(!revived->downed.downed);
    AC_CHECK_EQ(revived->hp, 50.0);
    AC_CHECK_EQ(revived->downed.reviveProgressMs, 0.0);
  }
  AC_CHECK(countEvents(*world, ac::sim::kEventReviveProgress) > 0u);
  const ac::sim::Event* done = findEvent(*world, ac::sim::kEventReviveDone);
  AC_CHECK(done != nullptr);
  if (done != nullptr) {
    AC_CHECK_EQ(done->value, 50.0);
    AC_CHECK_EQ(done->subjectId, reviver);
    AC_CHECK_EQ(done->targetId, victim);
  }
}

AC_TEST(combat_revive_interrupt_and_reset_delay) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId victim = addPlayer(*world, 0.0, 20.0, 0u, 0.0, 100.0, 50.0);
  const EntityId reviver = addPlayer(*world, 1.0, 20.0, 0u, 0.0, 100.0, 50.0);
  ac::combat::markDowned(ac::sim::entityById(*world, victim)->downed, 0.0);
  auto pushInteract = [&](bool interact) {
    Command commands[2];
    commands[0] = idleCommand();
    commands[1] = idleCommand(interact ? ac::config::kButtonInteract : 0u);
    return ac::sim::stepWorld(*world, commands, 2u, 50u);
  };
  for (int tick = 0; tick < 20; ++tick) AC_CHECK(pushInteract(true));
  const Entity* victimState = ac::sim::entityById(*world, victim);
  AC_CHECK_EQ(victimState->downed.reviveProgressMs, 1000.0);
  AC_CHECK_EQ(victimState->downed.reviverId, reviver);
  AC_CHECK(pushInteract(false));  // 松手 -> interrupted，进度保留、记下重置时刻
  victimState = ac::sim::entityById(*world, victim);
  AC_CHECK_EQ(victimState->downed.reviveProgressMs, 1000.0);
  AC_CHECK_EQ(victimState->downed.reviverId, 0u);
  AC_CHECK_EQ(victimState->downed.resetAtMs, 6050.0);  // 中断 tick 的 now(1050) + 5000
  for (int tick = 0; tick < 100; ++tick) AC_CHECK(pushInteract(false));  // 5 秒后回零
  AC_CHECK_EQ(ac::sim::entityById(*world, victim)->downed.reviveProgressMs, 0.0);
  AC_CHECK(ac::sim::entityById(*world, victim)->downed.downed);
}

AC_TEST(combat_wave_clear_revives_downed) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId victim = addPlayer(*world, 0.0, 20.0, 0u, 0.0);
  ac::combat::markDowned(ac::sim::entityById(*world, victim)->downed, 0.0);
  AC_CHECK_EQ(ac::sim::reviveDownedForWaveClear(*world), 1u);
  const Entity* revived = ac::sim::entityById(*world, victim);
  AC_CHECK(!revived->downed.downed);
  AC_CHECK_EQ(revived->hp, 50.0);
  AC_CHECK_EQ(countEvents(*world, ac::sim::kEventReviveDone), 1u);
  AC_CHECK_EQ(ac::sim::reviveDownedForWaveClear(*world), 0u);
}

AC_TEST(combat_downed_player_cannot_move) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId victim = addPlayer(*world, 0.0, 20.0, 0u, 0.0);
  ac::combat::markDowned(ac::sim::entityById(*world, victim)->downed, 0.0);
  Command command = idleCommand();
  command.moveY = 1.0;
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const Entity* entity = ac::sim::entityById(*world, victim);
  AC_CHECK_EQ(entity->pos.z, 20.0);  // 出生点，倒地后不许移动
  AC_CHECK_EQ(entity->vel.z, 0.0);
  AC_CHECK(ac::combat::canBeRevived(entity->downed));
}

// ---------- §5.7 怒气 ----------

AC_TEST(combat_rage_accumulate_activate_expire) {
  ac::combat::RageState rage;
  ac::combat::resetRageState(rage);
  AC_CHECK_EQ(ac::combat::addKillRage(rage, false, false, 0.0), 8.0);
  AC_CHECK_EQ(rage.value, 8.0);
  AC_CHECK_EQ(ac::combat::addKillRage(rage, true, false, 0.0), 20.0);
  AC_CHECK_EQ(ac::combat::addKillRage(rage, false, true, 0.0), 16.0);
  AC_CHECK_EQ(rage.value, 44.0);
  rage.value = 99.0;
  AC_CHECK_EQ(ac::combat::addKillRage(rage, false, false, 0.0), 8.0);
  AC_CHECK_EQ(rage.value, 100.0);  // 夹到上限
  AC_CHECK(ac::combat::activateRage(rage, 1000.0));
  AC_CHECK_EQ(rage.value, 0.0);
  AC_CHECK_EQ(rage.endsAtMs, 9000.0);
  AC_CHECK(ac::combat::isRageActive(rage, 1000.0));
  AC_CHECK(ac::combat::isRageActive(rage, 8999.0));
  AC_CHECK(!ac::combat::isRageActive(rage, 9000.0));
  // 到期：清零 endsAtMs 并返回 false（v1 updateRage 在 value 已归零时不报告「发生了衰减」）
  AC_CHECK(!ac::combat::updateRage(rage, 9000.0, 50.0));
  AC_CHECK_EQ(rage.endsAtMs, 0.0);
  AC_CHECK(!ac::combat::activateRage(rage, 9000.0));  // 怒气已耗尽
  AC_CHECK_EQ(ac::combat::rageRatio(rage), 0.0);
}

AC_TEST(combat_rage_decays_only_after_idle_delay) {
  ac::combat::RageState rage;
  ac::combat::resetRageState(rage);
  rage.value = 20.0;
  rage.lastCombatAtMs = 0.0;
  AC_CHECK(!ac::combat::updateRage(rage, 9999.0, 50.0));
  AC_CHECK_EQ(rage.value, 20.0);
  AC_CHECK(ac::combat::updateRage(rage, 10000.0, 1000.0));
  AC_CHECK_EQ(rage.value, 15.0);
  ac::combat::noteCombat(rage, 20000.0);
  AC_CHECK(!ac::combat::updateRage(rage, 20000.0, 50.0));
  AC_CHECK_EQ(rage.value, 15.0);
  AC_CHECK(ac::combat::updateRage(rage, 30000.0, 10000.0));
  AC_CHECK_EQ(rage.value, 0.0);  // 衰减到 0 就停住
  AC_CHECK_EQ(ac::combat::rageSecondsLeft(rage, 0.0), 0.0);
}

AC_TEST(combat_rage_fire_rate_80ms_rifle) {
  ac::combat::WeaponState state;
  ac::combat::resetWeaponState(state);
  state.activeSlot = 1u;
  AC_CHECK(ac::combat::tryFire(state, 0.0, ac::config::kRageFireRateMultiplier));
  AC_CHECK_EQ(state.nextFireAllowedAtMs, 80.0);
  AC_CHECK(!ac::combat::tryFire(state, 79.9, ac::config::kRageFireRateMultiplier));
  AC_CHECK(ac::combat::tryFire(state, 80.0, ac::config::kRageFireRateMultiplier));
  AC_CHECK_EQ(state.magInSlot[1], 28);
}

AC_TEST(combat_rage_move_speed_multiplier) {
  std::unique_ptr<World> plain = makeWorld();
  std::unique_ptr<World> raged = makeWorld();
  addPlayer(*plain, 0.0, 20.0, 0u, 0.0);
  addPlayer(*raged, 0.0, 20.0, 0u, 0.0);
  ac::sim::entityById(*raged, 1u)->rage.value = 100.0;
  AC_CHECK(ac::combat::activateRage(ac::sim::entityById(*raged, 1u)->rage, 0.0));
  Command command = idleCommand();
  command.moveY = 1.0;
  for (int tick = 0; tick < 20; ++tick) {
    AC_CHECK(ac::sim::stepWorld(*plain, &command, 1u, 50u));
    AC_CHECK(ac::sim::stepWorld(*raged, &command, 1u, 50u));
  }
  // 用水平位移长度比较（不依赖 moveY 落在哪个世界轴）：出生点 (0, 20)。
  const Entity* plainEntity = ac::sim::entityById(*plain, 1u);
  const Entity* ragedEntity = ac::sim::entityById(*raged, 1u);
  AC_CHECK(plainEntity != nullptr && ragedEntity != nullptr);
  if (plainEntity != nullptr && ragedEntity != nullptr) {
    const double plainStep = std::sqrt(plainEntity->pos.x * plainEntity->pos.x +
                                       (plainEntity->pos.z - 20.0) * (plainEntity->pos.z - 20.0));
    const double ragedStep = std::sqrt(ragedEntity->pos.x * ragedEntity->pos.x +
                                       (ragedEntity->pos.z - 20.0) * (ragedEntity->pos.z - 20.0));
    AC_CHECK(plainStep > 4.4 && plainStep < 4.6);
    AC_CHECK_NEAR(ragedStep, plainStep * ac::config::kRageMoveSpeedMultiplier, 1e-9);
  }
}

AC_TEST(combat_rage_activated_event) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId player = addPlayer(*world, 0.0, 20.0, 0u, 0.0);
  ac::sim::entityById(*world, player)->rage.value = 100.0;
  const Command command = idleCommand(ac::config::kButtonRage);
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const ac::sim::Event* event = findEvent(*world, ac::sim::kEventRageActivated);
  AC_CHECK(event != nullptr);
  if (event != nullptr) {
    AC_CHECK_EQ(event->subjectId, player);
    AC_CHECK_EQ(event->value, ac::config::kRageDurationMs);
  }
  AC_CHECK(ac::combat::isRageActive(ac::sim::entityById(*world, player)->rage, 50.0));
}

// ---------- §7 DoD：零分配、不读 fx 流、类型编号与编码层对齐 ----------

AC_TEST(combat_resolve_reads_no_fx_stream) {
  std::unique_ptr<World> world = makeWorld();
  addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  for (int i = 0; i < 8; ++i) addSheep(*world, 0.0, 20.0 + static_cast<double>(i) * 1.5, 1000.0);
  const uint32_t fxBefore = world->rng.fx.a;
  const uint32_t aiBefore = world->rng.ai.a;
  const uint32_t spawnBefore = world->rng.spawn.a;
  Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0));
  std::size_t hits = 0u;  // 事件缓冲每 tick 清零，必须在 tick 内累加
  for (int tick = 0; tick < 40; ++tick) {
    command.seq = static_cast<uint16_t>(tick);
    AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
    hits += countEvents(*world, ac::sim::kEventPlayerHit);
  }
  AC_CHECK_EQ(world->rng.fx.a, fxBefore);
  AC_CHECK_EQ(world->rng.ai.a, aiBefore);
  AC_CHECK_EQ(world->rng.spawn.a, spawnBefore);
  AC_CHECK(hits > 0u);
}

AC_TEST(combat_hot_path_allocates_nothing) {
  std::unique_ptr<World> world = makeWorld();
  addPlayer(*world, -6.0, 20.0, 0u, 0.0);
  for (int i = 0; i < 8; ++i) addSheep(*world, 0.0, 20.0 + static_cast<double>(i) * 1.5, 100000.0);
  Command command = fireCommand(1u, kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0));
  ac::test::AllocationScope scope;
  for (int tick = 0; tick < 1000; ++tick) {
    command.seq = static_cast<uint16_t>(tick);
    command.buttons = static_cast<uint8_t>(ac::config::kButtonFire | ac::config::kButtonReload |
                                          ac::config::kButtonSwitchWeapon);
    command.switchTo = static_cast<uint8_t>(tick % 3);
    ac::sim::stepWorld(*world, &command, 1u, 50u);
  }
  const std::size_t allocations = scope.since();  // 断言会分配：先读计数
  AC_CHECK_EQ(allocations, static_cast<std::size_t>(0u));
  AC_CHECK(world->timeMs == 50000u);
}

AC_TEST(combat_event_ids_match_codec) {
  AC_CHECK_EQ(ac::sim::kEventPlayerHit, static_cast<uint8_t>(ac::net::EventType::kPlayerHit));
  AC_CHECK_EQ(ac::sim::kEventSheepKilled, static_cast<uint8_t>(ac::net::EventType::kSheepKilled));
  AC_CHECK_EQ(ac::sim::kEventPlayerDowned, static_cast<uint8_t>(ac::net::EventType::kPlayerDowned));
  AC_CHECK_EQ(ac::sim::kEventReviveProgress, static_cast<uint8_t>(ac::net::EventType::kReviveProgress));
  AC_CHECK_EQ(ac::sim::kEventReviveDone, static_cast<uint8_t>(ac::net::EventType::kReviveDone));
  AC_CHECK_EQ(ac::sim::kEventRageActivated, static_cast<uint8_t>(ac::net::EventType::kRageActivated));
  AC_CHECK_EQ(ac::net::eventEntryBytes(ac::sim::kEventPlayerHit), 18u);
  AC_CHECK_EQ(ac::net::eventPayloadWithTypeBytes(ac::sim::kEventPlayerHit), 14u);
  AC_CHECK_EQ(sizeof(ac::sim::Event), 48u);
}

AC_TEST(combat_replay_is_deterministic) {
  std::unique_ptr<World> first = makeWorld();
  std::unique_ptr<World> second = makeWorld();
  addPlayer(*first, -6.0, 20.0, 0u, 0.0);
  addPlayer(*second, -6.0, 20.0, 0u, 0.0);
  addSheep(*first, 0.0, 20.0, 300.0, 40.0, 1u);
  addSheep(*second, 0.0, 20.0, 300.0, 40.0, 1u);
  for (int tick = 0; tick < 200; ++tick) {
    Command command = fireCommand(static_cast<uint16_t>(tick), kYawX, pitchTowards(-6.0, 20.0, 0.0, 0.56, 20.0));
    if (tick % 7 == 0) command.buttons = static_cast<uint8_t>(command.buttons | ac::config::kButtonRage);
    if (tick % 11 == 0) command.buttons = static_cast<uint8_t>(command.buttons | ac::config::kButtonReload);
    AC_CHECK(ac::sim::stepWorld(*first, &command, 1u, 50u));
    AC_CHECK(ac::sim::stepWorld(*second, &command, 1u, 50u));
  }
  const Entity* a = ac::sim::entityById(*first, 1u);
  const Entity* b = ac::sim::entityById(*second, 1u);
  AC_CHECK(a != nullptr && b != nullptr);
  if (a != nullptr && b != nullptr) {
    AC_CHECK_EQ(a->pos.x, b->pos.x);
    AC_CHECK_EQ(a->weapon.magInSlot[0], b->weapon.magInSlot[0]);
    AC_CHECK_EQ(a->weapon.spreadDeg, b->weapon.spreadDeg);
    AC_CHECK_EQ(a->rage.value, b->rage.value);
    AC_CHECK_EQ(a->hp, b->hp);
  }
  AC_CHECK_EQ(first->eventCount, second->eventCount);
  AC_CHECK_EQ(first->stats.eventsDropped, second->stats.eventsDropped);
  AC_CHECK_EQ(first->poseHistory.writeCount, second->poseHistory.writeCount);
}

AC_TEST(combat_trace_ignores_shooter_and_reports_nearest) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId shooter = addPlayer(*world, -8.0, 20.0, 0u, 0.0);
  const EntityId near = addSheep(*world, -2.0, 20.0, 100.0);
  const EntityId far = addSheep(*world, 0.0, 20.0, 100.0);
  ac::sim::ShotTrace trace;
  const ac::Vec3 toNear = ac::yawPitchToDirection(kYawX, pitchTowards(-8.0, 20.0, -2.0, 0.56, 20.0));
  ac::sim::traceRay(*world, nullptr, shooter, -8.0, ac::config::kEyeHeightM, 20.0, toNear.x, toNear.y, toNear.z,
                    160.0, 0.0, trace);
  AC_CHECK(trace.hit);
  AC_CHECK_EQ(trace.targetId, near);  // 升序遍历 + 最近命中者胜
  AC_CHECK(trace.targetId != far);
  // 射手自身不参与：从射手位置向来路（-X）打，身后没有任何实体
  ac::sim::traceRay(*world, nullptr, shooter, -8.0, ac::config::kEyeHeightM, 20.0, -1.0, 0.0, 0.0, 160.0, 0.0,
                    trace);
  AC_CHECK(!trace.hit);
}

AC_TEST(combat_trace_respects_max_distance) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId shooter = addPlayer(*world, -8.0, 20.0, 0u, 0.0);
  addSheep(*world, 0.0, 20.0, 100.0);
  ac::sim::ShotTrace trace;
  const ac::Vec3 aim = ac::yawPitchToDirection(kYawX, pitchTowards(-8.0, 20.0, 0.0, 0.56, 20.0));
  ac::sim::traceRay(*world, nullptr, shooter, -8.0, ac::config::kEyeHeightM, 20.0, aim.x, aim.y, aim.z, 4.0, 0.0,
                    trace);
  AC_CHECK(!trace.hit);  // 目标在 7.4m 外，超出 4m 上限
  ac::sim::traceRay(*world, nullptr, shooter, -8.0, ac::config::kEyeHeightM, 20.0, aim.x, aim.y, aim.z, 160.0, 0.0,
                    trace);
  AC_CHECK(trace.hit);
}

AC_TEST(combat_pellet_count_sets_shotgun_spread_hits) {
  // 8 颗弹丸各掷一次抖动；只统计事件条数（单 tick 最多 8 条 playerHit）
  std::unique_ptr<World> world = makeWorld();
  const EntityId player = addPlayer(*world, -3.0, 20.0, 0u, 0.0);
  ac::sim::entityById(*world, player)->weapon.activeSlot = 2u;
  addSheep(*world, 0.0, 20.0, 100000.0, 0.0);
  const Command command = fireCommand(9u, kYawX, pitchTowards(-3.0, 20.0, 0.0, 0.56, 20.0));
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  const std::size_t hits = countEvents(*world, ac::sim::kEventPlayerHit);
  AC_CHECK(hits > 0u && hits <= 8u);
  AC_CHECK_EQ(ac::sim::entityById(*world, player)->weapon.magInSlot[2], 5);
  const double damage = 100000.0 - ac::sim::entityById(*world, 2u)->hp;
  AC_CHECK(damage >= static_cast<double>(hits) * 12.0);
  AC_CHECK(damage <= static_cast<double>(hits) * 24.0);  // 头部命中按 2 倍
}

AC_TEST(combat_reload_command_refills_after_delay) {
  std::unique_ptr<World> world = makeWorld();
  const EntityId player = addPlayer(*world, 0.0, 20.0, 0u, 0.0);
  ac::sim::entityById(*world, player)->weapon.magInSlot[0] = 3;
  Command command = idleCommand(ac::config::kButtonReload);
  AC_CHECK(ac::sim::stepWorld(*world, &command, 1u, 50u));
  AC_CHECK_EQ(ac::sim::entityById(*world, player)->weapon.reloadEndsAtMs, 1450.0);
  for (int tick = 1; tick < 28; ++tick) {
    const Command idle = idleCommand();
    AC_CHECK(ac::sim::stepWorld(*world, &idle, 1u, 50u));
  }
  AC_CHECK_EQ(ac::sim::entityById(*world, player)->weapon.magInSlot[0], 3);  // 还没到点
  const Command idle = idleCommand();
  AC_CHECK(ac::sim::stepWorld(*world, &idle, 1u, 50u));
  AC_CHECK_EQ(ac::sim::entityById(*world, player)->weapon.magInSlot[0], 12);
  AC_CHECK_EQ(ac::sim::entityById(*world, player)->weapon.reserveAmmo, 111);
}