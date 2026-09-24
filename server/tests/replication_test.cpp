// S12 §4-6/§6-1：新客户端全量首帧、丢帧自愈、客户端镜像与服务器投影逐字节一致、
// 丢快照不丢事件（事件走可靠通道）、字节预算与报告出口（§4-9/§6-3）。
#include "tiny_test.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "config/player.hpp"
#include "core/scheduler.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "net/codec.hpp"
#include "net/keepalive.hpp"
#include "replication/backpressure.hpp"
#include "replication/baseline.hpp"
#include "replication/delta.hpp"
#include "replication/snapshot_rate.hpp"
#include "sim/entity_table.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"

namespace bp = ac::replication;
namespace metrics = ac::metrics;
namespace net = ac::net;
namespace sim = ac::sim;

namespace {

inline constexpr int kSpanProbeCount = 4;
inline constexpr uint32_t kReportSeed = 0x51E2u;

// 客户端镜像：吃全量/差分帧后必须逐字段等于服务器投影（§4-6 的「镜像逐字节一致」）。
struct ClientMirror {
  std::vector<net::EntityRecord> records;

  void eraseById(uint16_t id) {
    for (std::size_t i = 0u; i < records.size(); ++i) {
      if (records[i].id == id) {
        records.erase(records.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
  }

  void upsert(const net::EntityRecord& record) {
    for (net::EntityRecord& existing : records) {
      if (existing.id == record.id) {
        existing = record;
        return;
      }
    }
    records.push_back(record);
  }

  void apply(const net::SnapshotView& view) {
    for (const uint16_t id : view.removedIds) eraseById(id);
    for (const net::EntityRecord& record : view.records) upsert(record);
  }
};

// 房间侧最小组合：世界 + 每客户端基线 + 出站预算（房间接线本身属后续批次）。
struct Room {
  std::unique_ptr<sim::World> world;
  bp::ClientBaseline baseline;
  bp::OutboundBudget budget;
  uint8_t buffer[bp::kSnapshotCapacityBytes] = {};
  uint16_t nextSeq = 1u;

  Room() {
    world = sim::createWorld(kReportSeed);
    bp::reserveBaseline(baseline);
  }

  uint16_t spawn(sim::EntityKind kind, double x, double y, double z) {
    const sim::SpawnResult spawned = sim::spawnEntity(*world, kind, ac::Vec3{x, y, z});
    AC_CHECK(spawned.isOk);
    return spawned.id;
  }

  void tick() { sim::stepWorld(*world, nullptr, 0u, ac::config::kStepDtMs); }

  bp::DeltaOutcome send() {
    bp::DeltaInput input{};
    input.world = world.get();
    input.session = 7u;
    input.seq = nextSeq++;
    input.isForceFull = bp::shouldForceFull(baseline, world->tick);
    return bp::encodeDelta(input, baseline, buffer, sizeof(buffer));
  }
};

std::vector<net::EntityRecord> projectionOf(const sim::World& world,
                                           std::vector<net::EntityRecord>& scratch) {
  scratch.assign(bp::kMaxRecordsPerFrame, net::EntityRecord{});
  const std::size_t count = bp::projectWorld(world, scratch.data(), bp::kMaxRecordsPerFrame);
  scratch.resize(count);
  return scratch;
}

}  // namespace

AC_TEST(replication_new_client_first_frame_is_full) {
  Room room;
  room.spawn(sim::EntityKind::kPlayer, 1.0, 0.0, 8.0);
  room.spawn(sim::EntityKind::kPlayer, 2.0, 0.0, 8.0);
  room.tick();

  const bp::DeltaOutcome first = room.send();
  AC_CHECK(first.isOk);
  AC_CHECK(first.isFull);
  const net::DecodeResult<net::SnapshotView> decoded =
      net::decodeSnapshot(room.buffer, first.bytes, nullptr);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.baselineTick, 0u);
  AC_CHECK_EQ(decoded.value.records.size(), std::size_t{2});

  room.tick();
  const bp::DeltaOutcome second = room.send();
  AC_CHECK(second.isOk);
  AC_CHECK(!second.isFull);
  const net::DecodeResult<net::SnapshotView> update =
      net::decodeSnapshot(room.buffer, second.bytes, nullptr);
  AC_CHECK(update.isOk);
  AC_CHECK_EQ(update.value.baselineTick, first.tick);
  AC_CHECK(second.bytes < first.bytes);
}

AC_TEST(replication_delta_only_carries_changed_records) {
  Room room;
  const uint16_t moving = room.spawn(sim::EntityKind::kPlayer, 1.0, 0.0, 8.0);
  room.spawn(sim::EntityKind::kPlayer, 20.0, 0.0, 20.0);
  room.tick();
  AC_CHECK(room.send().isFull);

  sim::Command command{};
  command.moveX = 1.0;
  room.tick();
  sim::stepWorld(*room.world, &command, 1u, ac::config::kStepDtMs);
  const bp::DeltaOutcome delta = room.send();
  AC_CHECK(delta.isOk);
  AC_CHECK(!delta.isFull);
  const net::DecodeResult<net::SnapshotView> decoded =
      net::decodeSnapshot(room.buffer, delta.bytes, nullptr);
  AC_CHECK(decoded.isOk);
  AC_CHECK(!decoded.value.records.empty());
  bool isOnlyMover = true;
  for (const net::EntityRecord& record : decoded.value.records) {
    if (record.id != moving) isOnlyMover = false;
  }
  AC_CHECK(isOnlyMover);
}

AC_TEST(replication_removed_ids_are_reported_in_order) {
  Room room;
  const uint16_t first = room.spawn(sim::EntityKind::kPlayer, 1.0, 0.0, 8.0);
  const uint16_t second = room.spawn(sim::EntityKind::kPlayer, 2.0, 0.0, 8.0);
  const uint16_t third = room.spawn(sim::EntityKind::kPlayer, 3.0, 0.0, 8.0);
  room.tick();
  AC_CHECK(room.send().isFull);

  AC_CHECK(sim::despawnEntity(*room.world, second));
  room.tick();
  const bp::DeltaOutcome delta = room.send();
  const net::DecodeResult<net::SnapshotView> decoded =
      net::decodeSnapshot(room.buffer, delta.bytes, nullptr);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.removedIds.size(), std::size_t{1});
  AC_CHECK_EQ(decoded.value.removedIds[0], second);
  AC_CHECK_EQ(static_cast<uint32_t>(delta.removedCount), 1u);
  AC_CHECK(bp::baselineFind(room.baseline, first) != nullptr);
  AC_CHECK(bp::baselineFind(room.baseline, second) == nullptr);
  AC_CHECK(bp::baselineFind(room.baseline, third) != nullptr);
}

AC_TEST(replication_baseline_advances_without_ack) {
  Room room;
  room.spawn(sim::EntityKind::kPlayer, 5.0, 0.0, 8.0);
  room.tick();
  AC_CHECK(room.send().isOk);
  AC_CHECK_EQ(room.baseline.mirror.tick, room.world->tick);
  AC_CHECK_EQ(room.baseline.framesSinceFull, 0u);
  room.tick();
  AC_CHECK(room.send().isOk);
  AC_CHECK_EQ(room.baseline.mirror.tick, room.world->tick);
  AC_CHECK_EQ(room.baseline.framesSinceFull, 1u);
}

AC_TEST(replication_client_rebuilds_state_from_frames) {
  Room room;
  room.spawn(sim::EntityKind::kPlayer, 1.0, 0.0, 8.0);
  room.spawn(sim::EntityKind::kSheep, 6.0, 0.0, 9.0);
  room.spawn(sim::EntityKind::kSheep, -6.0, 0.0, 9.0);
  ClientMirror mirror;
  std::vector<net::EntityRecord> expected;

  for (int tick = 0; tick < 30; ++tick) {
    sim::Command command{};
    command.moveX = 1.0;
    room.tick();
    sim::stepWorld(*room.world, &command, 1u, ac::config::kStepDtMs);
    const bp::DeltaOutcome outcome = room.send();
    AC_CHECK(outcome.isOk);
    const net::DecodeResult<net::SnapshotView> decoded =
        net::decodeSnapshot(room.buffer, outcome.bytes, nullptr);
    AC_CHECK(decoded.isOk);
    mirror.apply(decoded.value);
    const std::vector<net::EntityRecord> server = projectionOf(*room.world, expected);
    AC_CHECK(mirror.records == server);
  }
  AC_CHECK_EQ(mirror.records.size(), std::size_t{3});
}

AC_TEST(replication_forced_full_frame_every_forty_ticks) {
  Room room;
  room.spawn(sim::EntityKind::kPlayer, 1.0, 0.0, 8.0);
  int fullFrames = 0;
  int spans[kSpanProbeCount] = {};
  int spanCount = 0;
  int lastFull = -1;
  for (int tick = 0; tick < 81; ++tick) {
    room.tick();
    const bp::DeltaOutcome outcome = room.send();
    AC_CHECK(outcome.isOk);
    if (outcome.isFull) {
      ++fullFrames;
      if (lastFull >= 0 && spanCount < kSpanProbeCount) spans[spanCount++] = tick - lastFull;
      lastFull = tick;
    }
  }
  AC_CHECK_EQ(fullFrames, 3);
  AC_CHECK_EQ(spans[0], 40);
  AC_CHECK_EQ(spans[1], 40);
  AC_CHECK(!bp::shouldForceFull(room.baseline, room.world->tick + 1));
  AC_CHECK(bp::shouldForceFull(room.baseline, room.world->tick + 40));
}

AC_TEST(replication_reconnect_resets_baseline_to_full) {
  Room room;
  room.spawn(sim::EntityKind::kPlayer, 1.0, 0.0, 8.0);
  room.tick();
  AC_CHECK(room.send().isFull);
  room.tick();
  AC_CHECK(!room.send().isFull);

  bp::resetBaseline(room.baseline);
  room.tick();
  const bp::DeltaOutcome afterReset = room.send();
  AC_CHECK(afterReset.isFull);
  const net::DecodeResult<net::SnapshotView> decoded =
      net::decodeSnapshot(room.buffer, afterReset.bytes, nullptr);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.baselineTick, 0u);
  AC_CHECK_EQ(room.baseline.mirror.tick, room.world->tick);
}

AC_TEST(replication_snapshot_bytes_stay_within_budget) {
  Room room;
  for (int i = 0; i < 4; ++i) room.spawn(sim::EntityKind::kPlayer, 2.0 * i, 0.0, 8.0);
  for (int i = 0; i < 40; ++i) {
    room.spawn(sim::EntityKind::kSheep, -20.0 + 1.5 * i, 0.0, 12.0 + static_cast<double>(i % 3));
  }
  std::size_t fullBytes = 0u;
  std::size_t steadySum = 0u;
  std::size_t steadyCount = 0u;
  std::size_t steadyMax = 0u;
  for (int tick = 0; tick < 60; ++tick) {
    room.tick();
    const bp::DeltaOutcome outcome = room.send();
    AC_CHECK(outcome.isOk);
    AC_CHECK(outcome.bytes <= bp::kSnapshotCapacityBytes);
    if (outcome.isFull) fullBytes = outcome.bytes;
    if (tick >= 20) {
      steadySum += outcome.bytes;
      ++steadyCount;
      if (outcome.bytes > steadyMax) steadyMax = outcome.bytes;
    }
  }
  AC_CHECK(fullBytes > 0u && fullBytes <= bp::kSnapshotCapacityBytes);
  AC_CHECK(steadyMax <= bp::kSteadySnapshotBudgetBytes);
  AC_CHECK(steadyCount > 0u && steadySum / steadyCount <= bp::kSteadySnapshotBudgetBytes);
}

AC_TEST(replication_dropped_snapshot_keeps_events) {
  bp::OutboundBudget budget;
  metrics::CounterRegistry counters{};
  metrics::GaugeRegistry gauges{};
  bp::resetOutboundBudget(budget);

  bp::enqueueEvent(budget, bp::kOutboundBacklogBytes - 100u);
  AC_CHECK_EQ(budget.eventsQueued, 1u);
  const bp::QueueVerdict verdict = bp::enqueueSnapshot(budget, 200u, &counters, &gauges);
  AC_CHECK(verdict == bp::QueueVerdict::kDropSnapshot);
  AC_CHECK_EQ(budget.droppedSnapshots, 1u);
  AC_CHECK_EQ(budget.queuedBytes, bp::kOutboundBacklogBytes - 100u);
  AC_CHECK_EQ(metrics::counterValue(counters, metrics::CounterId::kSlowClientDrops), 1u);
  AC_CHECK_NEAR(metrics::gaugeValue(gauges, metrics::GaugeId::kSendQueueBytes),
                static_cast<double>(bp::kOutboundBacklogBytes - 100u), 1e-9);

  bp::noteDrained(budget, 5000u);
  AC_CHECK(bp::enqueueSnapshot(budget, 200u, &counters, &gauges) == bp::QueueVerdict::kEnqueue);
  AC_CHECK_EQ(budget.consecutiveDrops, 0u);
  AC_CHECK_EQ(budget.eventsQueued, 1u);
}

AC_TEST(replication_slow_consumer_disconnect_after_sixty_drops) {
  AC_CHECK_EQ(bp::kSlowClientDropFrames, 60u);
  AC_CHECK_EQ(bp::kDisconnectReasonSlowConsumer, 7u);
  AC_CHECK_EQ(bp::kOutboundBacklogBytes, 65536u);
  AC_CHECK_EQ(bp::kBacklogDownshiftBytes, 32768u);

  bp::OutboundBudget budget;
  bp::resetOutboundBudget(budget);
  bp::enqueueEvent(budget, bp::kOutboundBacklogBytes);
  bp::QueueVerdict verdict = bp::QueueVerdict::kEnqueue;
  for (uint32_t i = 0u; i < bp::kSlowClientDropFrames; ++i) {
    verdict = bp::enqueueSnapshot(budget, 100u, nullptr, nullptr);
  }
  AC_CHECK(verdict == bp::QueueVerdict::kDisconnect);
  AC_CHECK_EQ(budget.consecutiveDrops, bp::kSlowClientDropFrames);
  AC_CHECK(bp::isBacklogOverHalf(budget));
}

AC_TEST(replication_too_large_frame_is_refused) {
  bp::OutboundBudget budget;
  AC_CHECK(bp::kMaxSnapshotBytes == 2048u);
  AC_CHECK(bp::enqueueSnapshot(budget, bp::kMaxSnapshotBytes, nullptr, nullptr) ==
           bp::QueueVerdict::kEnqueue);
  AC_CHECK_EQ(budget.maxSnapshotBytes, 2048u);
  bp::resetOutboundBudget(budget);
  AC_CHECK(bp::enqueueSnapshot(budget, bp::kMaxSnapshotBytes + 1u, nullptr, nullptr) ==
           bp::QueueVerdict::kDropSnapshot);
  AC_CHECK_EQ(budget.queuedBytes, 0u);
  AC_CHECK_NEAR(bp::averageSnapshotBytes(budget), 0.0, 1e-12);
}

// §7 DoD：§5 的全部数值（2048 B、1228 B、40 KB/s、64 KiB、60 帧、3000 ms、1000 ms、8 ms、40 tick）
// 都以具名常量出现并被断言；同时把「同一数值的两个名字」（编码器容量 vs 单帧上限）钉在一起。
AC_TEST(replication_frozen_values_are_named) {
  AC_CHECK(bp::kMaxSnapshotBytes == 2048u);
  AC_CHECK(bp::kSnapshotCapacityBytes == bp::kMaxSnapshotBytes);
  AC_CHECK_EQ(bp::kSteadySnapshotBudgetBytes, 1228u);
  AC_CHECK_EQ(bp::kOutboundBacklogBytes, 65536u);
  AC_CHECK_EQ(bp::kBacklogDownshiftBytes, bp::kOutboundBacklogBytes / 2u);
  AC_CHECK_EQ(bp::kSlowClientDropFrames, 60u);
  AC_CHECK_EQ(bp::kDisconnectReasonSlowConsumer, 7u);
  AC_CHECK_EQ(bp::kFullSnapshotIntervalTicks, 40u);
  AC_CHECK_EQ(net::kClientBandwidthBytesPerSec, 40960u);  // 40 KB/s（S04 §5 同名常量，本步继承）
  AC_CHECK_EQ(ac::core::kRoomTickBudgetMs, 8u);
  AC_CHECK_EQ(bp::kSnapshotRateCleanPeriodMs, 3000u);
  AC_CHECK_EQ(bp::kSnapshotRateEvalPeriodMs, 1000u);
}

AC_TEST(replication_encode_delta_rejects_bad_input) {
  bp::ClientBaseline baseline;
  bp::reserveBaseline(baseline);
  uint8_t buffer[64] = {};
  bp::DeltaInput input{};
  AC_CHECK(!bp::encodeDelta(input, baseline, buffer, sizeof(buffer)).isOk);
  Room room;
  room.spawn(sim::EntityKind::kPlayer, 1.0, 0.0, 8.0);
  room.tick();
  bp::DeltaInput noCapacity{};
  noCapacity.world = room.world.get();
  AC_CHECK(!bp::encodeDelta(noCapacity, baseline, buffer, 0u).isOk);
  AC_CHECK_EQ(baseline.mirror.tick, 0u);
}

AC_TEST(replication_metric_and_flag_names_hold) {
  AC_CHECK_EQ(metrics::gaugeCount(), 6u);
  AC_CHECK(metrics::isGaugeRegistered("ac_snapshot_rate_x10"));
  AC_CHECK(metrics::isGaugeRegistered("ac_snapshot_bytes_avg"));
  AC_CHECK(metrics::isGaugeRegistered("ac_snapshot_bytes_max"));
  AC_CHECK(metrics::isGaugeRegistered("ac_send_queue_bytes"));
  AC_CHECK(metrics::isGaugeRegistered("ac_tick_schedule_error_ms_p95"));
  AC_CHECK(metrics::isGaugeRegistered("ac_sim_drift_ms"));
  AC_CHECK(!metrics::isGaugeRegistered("ac_not_a_gauge"));
  metrics::GaugeRegistry gauges{};
  metrics::setGauge(gauges, metrics::GaugeId::kSnapshotRateX10, 150.0);
  AC_CHECK_NEAR(metrics::gaugeValue(gauges, metrics::GaugeId::kSnapshotRateX10), 150.0, 1e-12);
  AC_CHECK(std::strcmp(metrics::gaugeName(metrics::GaugeId::kSimDriftMs), "ac_sim_drift_ms") == 0);
  metrics::resetGauges(gauges);
  AC_CHECK_NEAR(metrics::gaugeValue(gauges, metrics::GaugeId::kSnapshotRateX10), 0.0, 1e-12);

  bp::SnapshotRateState rate;
  AC_CHECK(bp::snapshotRateLevel(rate) == bp::RateLevel::kHigh);
  AC_CHECK_EQ(bp::snapshotRateX10(rate), 200u);

  // v1 线上位型：kind 低 2 位 | (flags & 0x3F) << 2（snapshot-codec.ts:607 + computeEntityFlags）
  sim::Entity player{};
  player.kind = sim::EntityKind::kPlayer;
  player.maxHp = 100.0;
  player.hp = 0.0;
  player.idle = true;
  AC_CHECK_EQ(bp::kindFlagsOf(player), static_cast<uint8_t>((1u | 32u) << 2));
  player.idle = false;
  player.hp = 50.0;
  AC_CHECK_EQ(bp::kindFlagsOf(player), 0u);
  sim::Entity sheep{};
  sheep.kind = sim::EntityKind::kSheep;
  AC_CHECK_EQ(bp::kindFlagsOf(sheep), 1u);
}

AC_TEST(replication_report_writes_json_file) {
  Room room;
  for (int i = 0; i < 4; ++i) room.spawn(sim::EntityKind::kPlayer, 3.0 * i, 0.0, 8.0);
  for (int i = 0; i < 24; ++i) {
    room.spawn(sim::EntityKind::kSheep, -18.0 + 1.5 * i, 0.0, 12.0);
  }

  const net::EventEntry events[2] = {
      net::EventEntry{1u, net::PlayerHitEvent{1u, 2u, 7u, 0u, 100, 0, 800}},
      net::EventEntry{2u, net::SheepKilledEvent{3u, 1u, 0u}},
  };
  std::vector<std::size_t> frameBytes;
  ClientMirror mirror;
  std::vector<net::EntityRecord> expected;
  bp::OutboundBudget budget;
  ac::core::TickScheduler scheduler;
  ac::core::startScheduler(scheduler, 0u);
  int64_t driftWorstMs = 0;
  constexpr uint32_t kTicks = 120u;

  for (uint32_t tick = 0u; tick < kTicks; ++tick) {
    // 执行时刻 = 本 tick 的应到时刻 + 确定性抖动（0..4 ms），漂移在同一时刻对齐测量。
    const uint64_t nowMs = static_cast<uint64_t>(tick) * static_cast<uint64_t>(ac::kTickMs) +
                           static_cast<uint64_t>(tick % 5u);
    ac::core::noteTickRun(scheduler, nowMs, 1u);
    const int64_t drift = ac::core::simDriftMs(room.world->timeMs, 0u, nowMs, 0u);
    if (drift > driftWorstMs) driftWorstMs = drift;
    if (-drift > driftWorstMs) driftWorstMs = -drift;
    room.tick();
    bp::DeltaInput input{};
    input.world = room.world.get();
    input.session = 7u;
    input.seq = room.nextSeq++;
    input.events = events;
    input.eventCount = 2u;
    input.isForceFull = bp::shouldForceFull(room.baseline, room.world->tick);
    const bp::DeltaOutcome outcome = bp::encodeDelta(input, room.baseline, room.buffer, sizeof(room.buffer));
    AC_CHECK(outcome.isOk);
    AC_CHECK_EQ(outcome.eventCount, std::size_t{2});

    bp::enqueueEvent(budget, 60u);
    const bp::QueueVerdict verdict = bp::enqueueSnapshot(budget, outcome.bytes, nullptr, nullptr);
    if (verdict == bp::QueueVerdict::kEnqueue) {
      frameBytes.push_back(outcome.bytes);
      bp::noteDrained(budget, outcome.bytes);
    }

    const net::DecodeResult<net::SnapshotView> decoded =
        net::decodeSnapshot(room.buffer, outcome.bytes, nullptr);
    AC_CHECK(decoded.isOk);
    AC_CHECK_EQ(decoded.value.events.size(), std::size_t{2});
    mirror.apply(decoded.value);
    const std::vector<net::EntityRecord> server = projectionOf(*room.world, expected);
    AC_CHECK(mirror.records == server);
  }

  AC_CHECK_EQ(budget.eventsQueued, kTicks);
  AC_CHECK_EQ(budget.droppedSnapshots, 0u);  // 健康阶段（稳态）一帧不丢
  const uint32_t eventsDropped = kTicks - budget.eventsQueued;

  // §7 DoD：报告必须同时给出「丢过快照」与「事件一条没丢」。背压爆发段：先把队列顶到
  // 64 KiB 预算之上（事件通道不受预算约束，照常排队），再连编 5 帧 —— 快照全丢、事件全留。
  const uint32_t dropsBefore = budget.droppedSnapshots;
  const uint32_t eventsBefore = budget.eventsQueued;
  for (int i = 0; i < 40; ++i) {
    bp::enqueueEvent(budget, 2000u);
  }
  for (uint32_t i = 0u; i < 5u; ++i) {
    AC_CHECK(bp::enqueueSnapshot(budget, 400u, nullptr, nullptr) == bp::QueueVerdict::kDropSnapshot);
  }
  AC_CHECK_EQ(budget.droppedSnapshots - dropsBefore, 5u);
  AC_CHECK_EQ(budget.eventsQueued - eventsBefore, 40u);
  AC_CHECK(budget.queuedBytes > bp::kOutboundBacklogBytes);
  AC_CHECK_EQ(eventsDropped, 0u);
  bp::noteDrained(budget, budget.queuedBytes);

  std::vector<std::size_t> sorted = frameBytes;
  for (std::size_t i = 1u; i < sorted.size(); ++i) {
    const std::size_t value = sorted[i];
    std::size_t j = i;
    while (j > 0u && sorted[j - 1u] > value) {
      sorted[j] = sorted[j - 1u];
      --j;
    }
    sorted[j] = value;
  }
  const std::size_t rank = (sorted.size() * 95u + 99u) / 100u;
  const std::size_t p95 = sorted[rank == 0u ? 0u : rank - 1u];
  std::size_t maxBytes = 0u;
  for (const std::size_t bytes : frameBytes) {
    if (bytes > maxBytes) maxBytes = bytes;
  }
  const std::size_t warmup = frameBytes.size() > 20u ? 20u : 0u;
  double steadyMean = 0.0;
  if (frameBytes.size() > warmup) {
    unsigned long long sum = 0u;
    for (std::size_t i = warmup; i < frameBytes.size(); ++i) sum += frameBytes[i];
    steadyMean = static_cast<double>(sum) / static_cast<double>(frameBytes.size() - warmup);
  }

  bp::SnapshotRateState rate;
  uint16_t levels[6] = {};
  int levelCount = 0;
  levels[levelCount++] = bp::snapshotRateX10(rate);
  for (uint32_t nowMs = 1000u; nowMs <= 8000u; nowMs += 1000u) {
    bp::RateTriggers triggers{};
    triggers.isBacklogOverHalf = nowMs <= 2000u;
    if (bp::updateSnapshotRate(rate, nowMs, triggers, nullptr)) {
      levels[levelCount++] = bp::snapshotRateX10(rate);
    }
  }
  AC_CHECK_EQ(levelCount, 5);
  AC_CHECK_EQ(levels[0], 200u);
  AC_CHECK_EQ(levels[1], 150u);
  AC_CHECK_EQ(levels[2], 100u);
  AC_CHECK_EQ(levels[3], 150u);
  AC_CHECK_EQ(levels[4], 200u);

  char json[1024] = {};
  std::snprintf(json, sizeof(json),
                "{\"suite\":\"replication\",\"seed\":%u,\"ticks\":%u,\"snapshotBytesMax\":%llu,"
                "\"snapshotBytesP95\":%llu,\"steadyMeanBytes\":%.3f,\"droppedSnapshots\":%u,"
                "\"eventsDropped\":%u,\"rateLevelsVisited\":[%u,%u,%u,%u,%u],\"scheduleErrorP95Ms\":%.3f,"
                "\"simDriftMsMax\":%lld}",
                kReportSeed, kTicks, static_cast<unsigned long long>(maxBytes),
                static_cast<unsigned long long>(p95), steadyMean, budget.droppedSnapshots,
                eventsDropped, levels[0], levels[1], levels[2], levels[3], levels[4],
                ac::core::tickScheduleErrorP95Ms(scheduler), static_cast<long long>(driftWorstMs));

  AC_CHECK(maxBytes <= bp::kSnapshotCapacityBytes);
  AC_CHECK(steadyMean <= static_cast<double>(bp::kSteadySnapshotBudgetBytes));
  AC_CHECK(!frameBytes.empty());
  if (ac::test::reportPath()[0] != 0) {
    AC_CHECK(ac::test::writeReportFile(json));
  }
}
