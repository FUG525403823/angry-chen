// S11 §5/§6：回退上限 min(rttMs/2, 200)、越限不回滚、环内缺失时的诊断计数与当下姿态回退。
#include "tiny_test.hpp"

#include <cstdint>
#include <memory>

#include "config/player.hpp"
#include "core/math.hpp"
#include "metrics/counters.hpp"
#include "security/rewind.hpp"
#include "sim/entity_table.hpp"
#include "sim/pose_history.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace sec = ac::security;
namespace metrics = ac::metrics;
namespace sim = ac::sim;

namespace {

struct Fixture {
  std::unique_ptr<sim::World> world;
  uint16_t id = 0u;

  // z = 8：在谷仓投影之外（|z| > 4.4），静态碰撞不会把位置推走，断言可钉住具体数值。
  // 姿态环由 stepWorld 末尾记录一次（S05 §5.3），这里绝不重复调用 recordPoseHistory。
  Fixture() {
    world = sim::createWorld(0x9E37u);
    const sim::SpawnResult spawned =
        sim::spawnEntity(*world, sim::EntityKind::kPlayer, ac::Vec3{1.0, 0.0, 8.0});
    AC_CHECK(spawned.isOk);
    id = spawned.id;
  }

  void advanceTo(double x) {
    sim::Entity* entity = sim::entityById(*world, id);  // 实体可能已 despawn（过期用例）
    if (entity != nullptr) entity->pos = ac::Vec3{x, 0.0, 8.0};
    sim::stepWorld(*world, nullptr, 0u, ac::config::kStepDtMs);
  }

  void despawnPlayer() {
    AC_CHECK(sim::despawnEntity(*world, id));
    sim::stepWorld(*world, nullptr, 0u, ac::config::kStepDtMs);
  }
};

}  // namespace

AC_TEST(rewind_ms_boundaries) {
  AC_CHECK_EQ(sec::rewindMs(0u), 0u);
  AC_CHECK_EQ(sec::rewindMs(1u), 0u);
  AC_CHECK_EQ(sec::rewindMs(200u), 100u);
  AC_CHECK_EQ(sec::rewindMs(400u), 200u);
  AC_CHECK_EQ(sec::rewindMs(401u), 200u);  // 取样时长夹到上限（401 本身已越限，见下一用例）
  AC_CHECK(!sec::isRewindOverLimit(400u));
  AC_CHECK(sec::isRewindOverLimit(401u));   // 精确比较 rttMs/2 = 200.5 > 200
  AC_CHECK_EQ(sec::rewindMs(600u), 200u);
  AC_CHECK_EQ(sec::rewindMs(0xFFFFFFFFu), 200u);
}

AC_TEST(rewind_limit_is_two_hundred_ms) {
  AC_CHECK_EQ(sec::kRewindLimitMs, 200u);
  AC_CHECK_EQ(sec::kRewindLimitMs, static_cast<uint32_t>(ac::kRewindLimitMs));
  AC_CHECK_EQ(sec::kRewindLimitMs, static_cast<uint32_t>(sim::kRewindLimitMs));
}

AC_TEST(rewind_over_limit_not_ok_and_counted) {
  sim::PoseHistory history{};
  metrics::CounterRegistry counters{};
  const sec::RewindOutcome out = sec::sampleRewindPose(history, 600u, 1u, &counters);
  AC_CHECK(!out.isOk);
  AC_CHECK(out.isClamped);
  AC_CHECK(!out.isFound);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRewindClamped), 1u);
}

AC_TEST(rewind_boundary_both_sides) {
  Fixture fixture;
  fixture.advanceTo(1.0);
  metrics::CounterRegistry counters{};
  const sec::RewindOutcome atLimit = sec::sampleRewindPose(fixture.world->poseHistory, 400u, fixture.id, &counters);
  AC_CHECK(atLimit.isOk);  // rttMs/2 == 200 恰好在上限内
  AC_CHECK(!atLimit.isClamped);
  const sec::RewindOutcome overLimit = sec::sampleRewindPose(fixture.world->poseHistory, 401u, fixture.id, &counters);
  AC_CHECK(!overLimit.isOk);  // 401 → rttMs/2 = 200.5 > 200 越限（不取整）→ 不回滚
  AC_CHECK(overLimit.isClamped);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRewindClamped), 1u);
}

AC_TEST(rewind_empty_history_falls_back_to_current) {
  sim::PoseHistory history{};
  metrics::CounterRegistry counters{};
  const sec::RewindOutcome out = sec::sampleRewindPose(history, 100u, 1u, &counters);
  AC_CHECK(!out.isOk);
  AC_CHECK(out.isClamped);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRewindClamped), 1u);
}

AC_TEST(rewind_unknown_entity_is_not_ok) {
  Fixture fixture;
  metrics::CounterRegistry counters{};
  const sec::RewindOutcome missing = sec::sampleRewindPose(fixture.world->poseHistory, 100u, 9999u, &counters);
  AC_CHECK(!missing.isOk);
  AC_CHECK(!missing.isFound);
  AC_CHECK(missing.isClamped);
  const sec::RewindOutcome noEntity = sec::sampleRewindPose(fixture.world->poseHistory, 100u, sim::kNoEntityId, nullptr);
  AC_CHECK(!noEntity.isOk);
}

AC_TEST(rewind_zero_ms_returns_newest_sample) {
  Fixture fixture;
  fixture.advanceTo(2.0);
  const sec::RewindOutcome out = sec::sampleRewindPose(fixture.world->poseHistory, 0u, fixture.id, nullptr);
  AC_CHECK(out.isOk);
  AC_CHECK(out.isFound);
  AC_CHECK_NEAR(out.pose.x, 2.0, 1e-12);
  AC_CHECK_NEAR(out.pose.z, 8.0, 1e-12);
}

AC_TEST(rewind_samples_one_tick_ago) {
  Fixture fixture;
  fixture.advanceTo(2.0);
  fixture.advanceTo(3.0);
  const sec::RewindOutcome out = sec::sampleRewindPose(fixture.world->poseHistory, 100u, fixture.id, nullptr);
  AC_CHECK(out.isOk);
  AC_CHECK_NEAR(out.pose.x, 2.0, 1e-12);  // rttMs/2 = 50 → 1 tick 前
}

AC_TEST(rewind_aged_out_entity_is_clamped) {
  Fixture fixture;
  fixture.despawnPlayer();
  for (int32_t i = 0; i < 6; ++i) fixture.advanceTo(0.0);
  metrics::CounterRegistry counters{};
  const sec::RewindOutcome out = sec::sampleRewindPose(fixture.world->poseHistory, 400u, fixture.id, &counters);
  AC_CHECK(!out.isOk);  // 环里已没有该实体 → 按当下姿态判定
  AC_CHECK(out.isClamped);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kRewindClamped), 1u);
}
