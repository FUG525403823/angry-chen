// S07 §5：把 docs/evidence/fixtures 的跨语言对拍向量（schema v2）喂给 C++ 权威内核。
// 逐 tick 重算「当帧全量投影」的 FNV-1a 64 链，并在关键帧上做字段级逐位比较。
#include "fixture_io.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "config/player.hpp"
#include "config/sheep.hpp"
#include "net/codec.hpp"
#include "net/wire.hpp"
#include "replication/delta.hpp"
#include "sim/arena.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"
#include "tiny_test.hpp"
#include "tmp_workdir.hpp"
#include "waves/director.hpp"

namespace {

using ac::sim::Command;
using ac::sim::Entity;
using ac::sim::EntityKind;
using ac::sim::World;

// ---- 初态（setup）：两侧读同一份数据（README §2），不再维护「按场景名硬编码」的隐含同表 ----

bool sheepKindFromName(const std::string& name, ac::config::SheepKind& out) {
  if (name == "grunt") {
    out = ac::config::SheepKind::kGrunt;
    return true;
  }
  if (name == "ram") {
    out = ac::config::SheepKind::kRam;
    return true;
  }
  if (name == "elite") {
    out = ac::config::SheepKind::kElite;
    return true;
  }
  if (name == "king") {
    out = ac::config::SheepKind::kKing;
    return true;
  }
  return false;
}

// v1 createWorld 的语义：世界建好后立刻在 4 个出生点各生成一名玩家（id 1..4 升序），
// 再由 setup 覆盖玩家初态（hp/armor）并按数组顺序生成羊群。生成原语复用 waves::spawnSheepAt
// （v1 的 spawnEntity + applySheepKind + state=graze 同形），不另写一套。
std::unique_ptr<World> createFixtureWorld(const ac::test::Fixture& fixture, ac::test::DiffSink& diff) {
  std::unique_ptr<World> world = ac::sim::createWorld(fixture.seed);
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
  for (const ac::test::FixturePlayerSetup& entry : fixture.setup.players) {
    Entity* player = ac::sim::entityById(*world, entry.id);
    if (player == nullptr) {
      diff.stringField(0u, "setup.players[].id", "existing player", std::to_string(entry.id));
      return world;
    }
    player->hp = entry.hp;
    player->armor = entry.armor;
  }
  for (const ac::test::FixtureSheepSetup& entry : fixture.setup.sheep) {
    ac::config::SheepKind kind = ac::config::SheepKind::kGrunt;
    if (!sheepKindFromName(entry.kind, kind)) {
      diff.stringField(0u, "setup.sheep[].kind", "grunt|ram|elite|king", entry.kind);
      return world;
    }
    if (!ac::waves::spawnSheepAt(*world, kind, entry.x, entry.z)) {
      diff.stringField(0u, "setup.sheep[].spawn", "spawn ok", entry.kind);
      return world;
    }
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

// ---- 当帧全量投影（与 tools/export-fixtures.mjs 的 projectionText 输入同口径） ----

const char* eventTypeName(uint8_t type) {
  // 与 v1 net/protocol.ts 的 EVENT_TYPE_NAMES 逐字同表（type 10 两侧都不产出）。
  switch (type) {
    case ac::sim::kEventPlayerHit:
      return "playerHit";
    case ac::sim::kEventSheepKilled:
      return "sheepKilled";
    case ac::sim::kEventWaveStart:
      return "waveStart";
    case ac::sim::kEventWaveClear:
      return "waveClear";
    case ac::sim::kEventPlayerDowned:
      return "playerDowned";
    case ac::sim::kEventReviveProgress:
      return "reviveProgress";
    case ac::sim::kEventReviveDone:
      return "reviveDone";
    case ac::sim::kEventRageActivated:
      return "rageActivated";
    case ac::sim::kEventMatchEnded:
      return "matchEnded";
    default:
      return "unknown";
  }
}

// 实体顺序 = world.activeIds 的插入顺序（v1 同源；id 复用后新实体排在末尾，因此**不是**升序）。
void projectEntities(const World& world, std::vector<ac::test::FixtureEntity>& out) {
  out.clear();
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    const uint16_t id = world.activeIds[i];
    const Entity* entity = ac::sim::entityById(world, id);
    if (entity == nullptr) continue;
    ac::test::FixtureEntity row;
    row.id = id;
    row.kind = ac::test::kindName(entity->kind);
    row.posX = entity->pos.x;
    row.posY = entity->pos.y;
    row.posZ = entity->pos.z;
    row.yaw = entity->yaw;
    row.pitch = entity->pitch;
    row.hp = entity->hp;
    row.flags = ac::test::entityFlagsOf(*entity, static_cast<double>(world.timeMs));
    out.push_back(std::move(row));
  }
}

void projectEvents(const World& world, std::vector<ac::test::FixtureEvent>& out) {
  out.clear();
  for (std::size_t i = 0u; i < world.eventCount; ++i) {
    const ac::sim::Event& source = world.events[i];
    ac::test::FixtureEvent row;
    row.tick = source.tick;
    row.type = eventTypeName(source.type);
    row.flags = source.flags;
    row.subjectId = source.subjectId;
    row.targetId = source.targetId;
    row.x = source.x;
    row.y = source.y;
    row.z = source.z;
    row.value = source.value;
    row.kind = ac::test::eventCarriesKind(row.type) ? source.kind : 0u;
    out.push_back(std::move(row));
  }
}

// ---- 比较 ----

void compareKeyframe(uint32_t tick, const ac::test::FixtureKeyframe& frame,
                     const std::vector<ac::test::FixtureEntity>& entities,
                     const std::vector<ac::test::FixtureEvent>& events, const ac::test::FixtureRng& rng,
                     ac::test::DiffSink& diff) {
  diff.intField(tick, "entities.count", static_cast<int64_t>(frame.entities.size()),
                static_cast<int64_t>(entities.size()));
  for (std::size_t i = 0u; i < frame.entities.size() && i < entities.size(); ++i) {
    const ac::test::FixtureEntity& expected = frame.entities[i];
    const ac::test::FixtureEntity& actual = entities[i];
    const std::string base = "entities[" + std::to_string(i) + "]";
    diff.intField(tick, base + ".id", static_cast<int64_t>(expected.id), static_cast<int64_t>(actual.id));
    diff.stringField(tick, base + ".kind", expected.kind, actual.kind);
    diff.doubleField(tick, base + ".pos.x", expected.posX, actual.posX);
    diff.doubleField(tick, base + ".pos.y", expected.posY, actual.posY);
    diff.doubleField(tick, base + ".pos.z", expected.posZ, actual.posZ);
    diff.doubleField(tick, base + ".yaw", expected.yaw, actual.yaw);
    diff.doubleField(tick, base + ".pitch", expected.pitch, actual.pitch);
    diff.doubleField(tick, base + ".hp", expected.hp, actual.hp);
    diff.intField(tick, base + ".flags", static_cast<int64_t>(expected.flags), static_cast<int64_t>(actual.flags));
    if (diff.has) return;
  }
  diff.intField(tick, "events.count", static_cast<int64_t>(frame.events.size()), static_cast<int64_t>(events.size()));
  for (std::size_t i = 0u; i < frame.events.size() && i < events.size(); ++i) {
    const ac::test::FixtureEvent& expected = frame.events[i];
    const ac::test::FixtureEvent& actual = events[i];
    const std::string base = "events[" + std::to_string(i) + "]";
    diff.intField(tick, base + ".tick", static_cast<int64_t>(expected.tick), static_cast<int64_t>(actual.tick));
    diff.stringField(tick, base + ".type", expected.type, actual.type);
    diff.intField(tick, base + ".flags", static_cast<int64_t>(expected.flags), static_cast<int64_t>(actual.flags));
    diff.intField(tick, base + ".subjectId", static_cast<int64_t>(expected.subjectId),
                  static_cast<int64_t>(actual.subjectId));
    diff.intField(tick, base + ".targetId", static_cast<int64_t>(expected.targetId),
                  static_cast<int64_t>(actual.targetId));
    diff.doubleField(tick, base + ".x", expected.x, actual.x);
    diff.doubleField(tick, base + ".y", expected.y, actual.y);
    diff.doubleField(tick, base + ".z", expected.z, actual.z);
    diff.doubleField(tick, base + ".value", expected.value, actual.value);
    // `kind`（S03 §5.4 的羊种类枚举 / S08）：JSON 与投影文本里只有带种类的类型才有该字段，其余两侧同为 0。
    diff.intField(tick, base + ".kind", static_cast<int64_t>(expected.kind), static_cast<int64_t>(actual.kind));
    if (diff.has) return;
  }
  diff.intField(tick, "rngState.ai", static_cast<int64_t>(frame.rng.ai), static_cast<int64_t>(rng.ai));
  diff.intField(tick, "rngState.spawn", static_cast<int64_t>(frame.rng.spawn), static_cast<int64_t>(rng.spawn));
  diff.intField(tick, "rngState.fx", static_cast<int64_t>(frame.rng.fx), static_cast<int64_t>(rng.fx));
}

// 快照字段组（snapshot-roundtrip-240t）：两侧各自「编码→解码」，
// 编码字节块（15 字节实体记录，升序 id）与解码后投影文本各算一个 FNV-1a 64，两个哈希都要对上。
void compareSnapshot(uint32_t tick, const ac::test::FixtureSnapshot& entry, const World& world,
                     ac::test::DiffSink& diff) {
  // fixture 记录的 records 数 = v1 snapshotWorld 的实体数（activeIds 顺序、无 id 复用 = 升序）。
  for (std::size_t i = 1u; i < world.activeCount; ++i) {
    if (world.activeIds[i] <= world.activeIds[i - 1u]) {
      diff.stringField(tick, "snapshot.records.order", "ascending activeIds", "id reuse");
      return;
    }
  }
  ac::net::EntityRecord records[ac::net::kMaxEntityRecordsPerFrame];
  const std::size_t count = ac::replication::projectWorld(world, records, ac::net::kMaxEntityRecordsPerFrame);
  diff.intField(tick, "snapshot.records", static_cast<int64_t>(entry.records), static_cast<int64_t>(count));
  if (diff.has) return;

  uint8_t bytes[ac::net::kMaxSnapshotBytes];
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<uint8_t>(ac::net::PacketType::kSnapshot);
  ac::net::SnapshotFrame frame{};
  frame.tick = tick;
  frame.serverTimeMs = world.timeMs;
  frame.lastAckedSeq = 0u;
  frame.baseline = nullptr;
  frame.records = records;
  frame.recordCount = count;
  frame.events = nullptr;
  frame.eventCount = 0u;
  const ac::net::EncodeResult encoded = ac::net::encodeSnapshot(header, frame, bytes, sizeof bytes);
  if (!encoded.isOk) {
    diff.stringField(tick, "snapshot.encode", "ok", "encode failed");
    return;
  }
  // 记录块起点 = 报文头 22 字节（到 baselineTick 为止）+ 1 字节记录数；exporter 那边直接按 15 字节
  // 拼记录再哈希，所以这里必须跳过那个计数字节才是同一段字节（报文格式本身不动）。
  const uint8_t* block = bytes + ac::net::kSnapshotHeadBytes + 1u;
  const uint64_t encodeHash =
      ac::test::fnv1a64(block, count * ac::test::kSnapshotRecordBytes, ac::test::kFnvOffsetBasis);
  diff.stringField(tick, "snapshot.encodeHash", ac::test::hash64Hex(entry.encodeHash),
                   ac::test::hash64Hex(encodeHash));

  // 解码：再走一次真实解码器，然后按 15 字节字段投影（与 v1 readSnapshotRecord 同字段同序）。
  const ac::net::DecodeResult<ac::net::SnapshotView> decoded = ac::net::decodeSnapshot(bytes, encoded.bytes, nullptr);
  if (!decoded.isOk) {
    diff.stringField(tick, "snapshot.decode", "ok", "decode failed");
    return;
  }
  std::vector<ac::test::SnapshotRecordFields> fields;
  fields.reserve(decoded.value.records.size());
  for (const ac::net::EntityRecord& record : decoded.value.records) {
    ac::test::SnapshotRecordFields row;
    row.id = record.id;
    row.kind = record.kind();
    row.flags = record.flags();
    row.xCm = record.xCm;
    row.yCm = record.yCm;
    row.zCm = record.zCm;
    row.yawUnits = record.yawUnits;
    row.pitchUnits = record.pitchUnits;
    row.hpRatioUnits = record.hpRatioUnits;
    row.state = record.state;
    fields.push_back(row);
  }
  const uint64_t decodeHash =
      ac::test::fnv1a64Text(ac::test::snapshotDecodeText(fields), ac::test::kFnvOffsetBasis);
  diff.stringField(tick, "snapshot.decodeHash", ac::test::hash64Hex(entry.decodeHash),
                   ac::test::hash64Hex(decodeHash));
}

// 诊断出口（只影响输出，不参与比较）：AC_FIXTURE_DUMP=1,269,270 时把这些 tick 的全量投影按 %.17g 打出来，
// 用来和 tools/export-fixtures.mjs --trace 的同一 tick 逐字段对表。
bool dumpWanted(uint32_t tick);

// §5.4 比较顺序：configHash -> 初态 -> 逐 tick（关键帧字段 -> 投影计数 -> 哈希链 -> 快照组）。
// 返回已跑完的 tick 数（configHash / 初态不符时返回 0：不跑 tick）。
uint32_t replayFixture(const ac::test::Fixture& fixture, ac::test::DiffSink& diff) {
  diff.stringField(0u, "configHash", fixture.configHash, ac::test::hashHex(ac::test::configHash()));
  if (diff.has) return 0u;

  std::unique_ptr<World> world = createFixtureWorld(fixture, diff);
  if (diff.has) return 0u;

  const std::vector<uint16_t> initialPlayers = ascendingPlayerIds(*world);
  const int32_t playerCount = static_cast<int32_t>(initialPlayers.size());
  ac::waves::DirectorState director = ac::waves::createDirectorState();
  if (fixture.startWave != 0u) ac::waves::planWave(director, static_cast<int32_t>(fixture.startWave), playerCount);

  std::vector<ac::test::FixtureCommand> commands;
  std::vector<ac::test::FixtureEntity> entities;
  std::vector<ac::test::FixtureEvent> events;
  uint64_t chain = ac::test::kFnvOffsetBasis;
  uint32_t compared = 0u;

  for (uint32_t tick = 1u; tick <= fixture.ticks; ++tick) {
    if (!fixture.commandAt(tick, commands)) {
      diff.stringField(tick, "script", "covering run", "gap");
      return compared;
    }
    const std::vector<uint16_t> players = ascendingPlayerIds(*world);
    if (commands.size() > players.size()) {
      diff.stringField(tick, "commands.count", "<= command slots",
                       std::to_string(commands.size()) + " > " + std::to_string(players.size()));
      return compared;
    }
    // 命令槽位 = 升序玩家位置（v1 applyCommands 的 slot 语义）；缺命令的槽位给 nullptr -> 速度归零。
    std::vector<Command> slots;
    slots.reserve(commands.size());
    for (std::size_t i = 0u; i < commands.size(); ++i) {
      const ac::test::FixtureCommand& source = commands[i];
      diff.intField(tick, "commands[" + std::to_string(i) + "].id", static_cast<int64_t>(players[i]),
                    static_cast<int64_t>(source.id));
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
    if (diff.has) return compared;
    ac::sim::stepWorld(*world, slots.empty() ? nullptr : slots.data(), static_cast<uint32_t>(slots.size()),
                       ac::test::kFixtureDtMs);
    // 外部每 tick 驱动波次导演（README §2；DirectorState 在 stepWorld 之外，与 S09/S10 的 B2 一致）。
    if (fixture.startWave != 0u) {
      uint16_t playerIds[ac::sim::kMaxEntities];
      const uint32_t count = ac::sim::collectPlayerIds(*world, playerIds);
      ac::waves::updateDirector(*world, director, playerCount, world->rng.spawn, playerIds, count);
    }
    ++compared;

    // 内部不变量（非 fixture 字段）：阶段 0 的 tick / timeMs 推进。
    diff.intField(tick, "world.tick", static_cast<int64_t>(tick), static_cast<int64_t>(world->tick));
    diff.intField(tick, "world.timeMs",
                  static_cast<int64_t>(tick) * static_cast<int64_t>(ac::config::kStepDtMs),
                  static_cast<int64_t>(world->timeMs));

    projectEntities(*world, entities);
    projectEvents(*world, events);
    if (dumpWanted(tick)) {
      std::printf("[dump] %s t%u", fixture.name.c_str(), tick);
      for (const ac::test::FixtureEntity& entity : entities) {
        std::printf(" %u:%s:%.17g,%.17g:hp%.17g:yaw%.17g:f%u", entity.id, entity.kind.c_str(), entity.posX,
                    entity.posZ, entity.hp, entity.yaw, entity.flags);
      }
      for (const ac::test::FixtureEvent& event : events) {
        std::printf(" EVT %s(%u->%u,%.17g)", event.type.c_str(), event.subjectId, event.targetId, event.value);
      }
      std::printf(" rng=%u,%u,%u", world->rng.ai.a, world->rng.spawn.a, world->rng.fx.a);
      std::printf("\n");
      if (std::getenv("AC_FIXTURE_PTEXT") != nullptr) {
        const ac::test::FixtureRng dumped{world->rng.ai.a, world->rng.spawn.a, world->rng.fx.a};
        std::string text = ac::test::projectionText(tick, commands, entities, events, dumped);
        for (char& c : text) {
          if (c == '\n') c = '|';
        }
        std::printf("[ptext] t%u|%s\n", tick, text.c_str());
      }
    }
    const ac::test::FixtureRng rng{world->rng.ai.a, world->rng.spawn.a, world->rng.fx.a};

    // 关键帧先比：它给出「首个不一致字段」，比链哈希更好定位（同 tick 两者不一致时以字段为准）。
    for (const ac::test::FixtureKeyframe& frame : fixture.keyframes) {
      if (frame.tick != tick) continue;
      compareKeyframe(tick, frame, entities, events, rng, diff);
      break;
    }
    if (diff.has) return compared;

    // 逐帧哈希链：h_i = fnv1a64(当帧全量投影文本, h_{i-1})。
    chain = ac::test::fnv1a64Text(ac::test::projectionText(tick, commands, entities, events, rng), chain);
    diff.stringField(tick, "hashChain", ac::test::hash64Hex(fixture.hashChain[tick - 1u]),
                     ac::test::hash64Hex(chain));
    if (diff.has) return compared;

    for (const ac::test::FixtureSnapshot& entry : fixture.snapshot) {
      if (entry.tick != tick) continue;
      compareSnapshot(tick, entry, *world, diff);
      break;
    }
    if (diff.has) return compared;
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
  AC_CHECK_EQ(compared, fixture.ticks);
  const ac::test::FixtureKeyframe& last = fixture.keyframes.back();
  std::printf("fixture %s ticks=%u entities=%zu events=%zu chain=%s configHash=%s\n", name, compared,
              last.entities.size(), last.events.size(), ac::test::hash64Hex(fixture.hashChain.back()).c_str(),
              fixture.configHash.c_str());
}

void expectDiff(const char* name, ac::test::Fixture& fixture, uint32_t expectTick, const std::string& expectField,
                uint32_t expectCompared) {
  ac::test::DiffSink diff;
  diff.fixture = fixture.name;
  const uint32_t compared = replayFixture(fixture, diff);
  AC_CHECK(diff.has);
  if (!diff.has) return;
  AC_CHECK_EQ(compared, expectCompared);
  AC_CHECK_EQ(diff.tick, expectTick);
  AC_CHECK_EQ(diff.field, expectField);
  std::printf("fixture %s -> %s (comparedTicks=%u)\n", name, diff.report().c_str(), compared);
}

bool loadOrFail(const char* name, ac::test::Fixture& fixture) {
  std::string error;
  if (!ac::test::loadFixtureByName(name, fixture, error)) {
    AC_FAIL(error.c_str());
    return false;
  }
  return true;
}

// 诊断出口（只影响输出，不参与比较）：AC_FIXTURE_DUMP=1,269,270 时把这些 tick 的全量投影按 %.17g 打出来，
// 用来和 tools/export-fixtures.mjs --trace 的同一 tick 逐字段对表。
bool dumpWanted(uint32_t tick) {
  static bool parsed = false;
  static bool all = false;
  static std::vector<uint32_t> ticks;
  if (!parsed) {
    parsed = true;
    const char* raw = std::getenv("AC_FIXTURE_DUMP");
    if (raw != nullptr && std::strcmp(raw, "*") == 0) {
      all = true;
    } else if (raw != nullptr) {
      std::string text(raw);
      std::size_t start = 0u;
      while (start <= text.size()) {
        const std::size_t comma = text.find(',', start);
        const std::string part = text.substr(start, comma == std::string::npos ? comma : comma - start);
        if (!part.empty()) ticks.push_back(static_cast<uint32_t>(std::strtoul(part.c_str(), nullptr, 10)));
        if (comma == std::string::npos) break;
        start = comma + 1u;
      }
    }
  }
  if (all) return true;
  for (const uint32_t wanted : ticks) {
    if (wanted == tick) return true;
  }
  return false;
}

}  // namespace

AC_TEST(fixture_still_60t) { runFixture("still-60t"); }
AC_TEST(fixture_line_move_240t) { runFixture("straight-line-240t"); }
AC_TEST(fixture_barn_collision_400t) { runFixture("barn-collision-400t"); }
AC_TEST(fixture_fence_bounds_400t) { runFixture("fence-bounds-400t"); }
AC_TEST(fixture_rifle_burst_hit_120t) { runFixture("rifle-burst-hit-120t"); }
AC_TEST(fixture_shotgun_spread_60t) { runFixture("shotgun-spread-60t"); }
AC_TEST(fixture_downed_revive_140t) { runFixture("downed-revive-140t"); }
AC_TEST(fixture_sheep_grunt_600t) { runFixture("sheep-grunt-ai-600t"); }
AC_TEST(fixture_sheep_ram_charge_300t) { runFixture("sheep-ram-charge-300t"); }
AC_TEST(fixture_sheep_elite_bolt_300t) { runFixture("sheep-elite-bolt-300t"); }
AC_TEST(fixture_sheep_king_phases_900t) { runFixture("sheep-king-phases-900t"); }
AC_TEST(fixture_wave_director_1to5_1200t) { runFixture("wave-director-1to5-1200t"); }
AC_TEST(fixture_snapshot_roundtrip_240t) { runFixture("snapshot-roundtrip-240t"); }
AC_TEST(fixture_stream_ownership_600t) { runFixture("rng-streams-600t"); }

// §5.5 的算法自检：FNV-1a 64 的公开测试向量（与导出脚本 assertFnvSelfTest 同一组）。
AC_TEST(fixture_fnv_self_test) { AC_CHECK(ac::test::selfTestFnv()); }

// §6 的自检：手工改关键帧的一位数字必须让比较在**那一个 tick**、那一个字段上 报 DIFF 失败。
AC_TEST(fixture_tampered_projection_reports_diff) {
  ac::test::Fixture fixture;
  if (!loadOrFail("fence-bounds-400t", fixture)) return;
  ac::test::FixtureKeyframe& last = fixture.keyframes.back();
  last.entities[0].posZ = std::nextafter(last.entities[0].posZ, 1.0e9);
  expectDiff("tampered-keyframe", fixture, fixture.ticks, "entities[0].pos.z", fixture.ticks);
}

// §6 的自检：改哈希链的一位必须报到那一个 tick 的 hashChain 上（该 tick 不是关键帧）。
AC_TEST(fixture_tampered_digest_tick_diff) {
  ac::test::Fixture fixture;
  if (!loadOrFail("fence-bounds-400t", fixture)) return;
  const uint32_t tick = 250u;
  fixture.hashChain[tick - 1u] ^= 0x1ull;
  expectDiff("tampered-chain", fixture, tick, "hashChain", tick);
}

// §6 的自检：改命令脚本的一位必须报错——脚本是载荷，不是装饰。
AC_TEST(fixture_tampered_script_tick_diff) {
  ac::test::Fixture fixture;
  if (!loadOrFail("still-60t", fixture)) return;
  ac::test::FixtureRun& run = fixture.script.back();
  run.commands[0].moveX = run.commands[0].moveX == 0.0 ? 1.0 : 0.0;
  expectDiff("tampered-script", fixture, run.from, "hashChain", run.from);
}

// §6 的自检：改 configHash 必须在跑 tick 之前失败。
AC_TEST(fixture_tampered_hash_fails_before_ticks) {
  ac::test::Fixture fixture;
  if (!loadOrFail("still-60t", fixture)) return;
  fixture.configHash = "00000000";
  expectDiff("tampered-config-hash", fixture, 0u, "configHash", 0u);
}

// 可选键自检（B 部分 B2 收尾）：`events[].kind` 只对**带种类的类型**出现（S03 §5.4 / S08）。现网 14 份向量的
// 关键帧 tick 都没落在击杀 tick 上（`docs/evidence/fixtures/README.md` §7.2 的 B2 收尾有登记），所以这里用一份
// 最小的 v2 向量把两条分支都钉住：带 `kind` 的读得出来、不带的取默认 0，且投影文本只对带种类的类型追加 `,kind`。
AC_TEST(fixture_event_kind_optional_key) {
  // 键序按 README §7.2（整数不带小数点）；1 tick 的最小合法向量：脚本无缝覆盖 1..1、hashChain 长度 = ticks。
  static const char kJson[] = R"json({
  "name": "kind-optional",
  "version": 2,
  "seed": 7,
  "dtMs": 50,
  "configHash": "19a978ea",
  "ticks": 1,
  "setup": {
    "players": [
    ],
    "sheep": [
    ]
  },
  "director": { "startWave": 0 },
  "script": [
    {
      "from": 1,
      "to": 1,
      "commands": [
      ]
    }
  ],
  "keyframes": [
    {
      "tick": 1,
      "entities": [
      ],
      "events": [
        { "tick": 1, "type": "sheepKilled", "flags": 0, "subjectId": 1, "targetId": 5, "x": 1, "y": 2, "z": 3, "value": 4, "kind": 3 },
        { "tick": 1, "type": "playerHit", "flags": 0, "subjectId": 1, "targetId": 5, "x": 1, "y": 2, "z": 3, "value": 4 }
      ],
      "rngState": { "ai": 1, "spawn": 2, "fx": 3 }
    }
  ],
  "snapshot": [
  ],
  "hashChain": [
    "0000000000000000"
  ]
}
)json";
  ac::test::TempDir dir("fixture_kind");
  if (!dir.isReady()) {
    AC_FAIL("temp dir not ready");
    return;
  }
  // 路径统一用正斜杠：`loadFixtureFile` 的 `baseName` 只按 '/' 取末段（Windows 反斜杠会让 name 校验假失败）。
  const std::string path = dir.path().generic_string() + "/kind-optional.json";
  {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
      AC_FAIL("temp fixture open failed");
      return;
    }
    const std::size_t length = sizeof(kJson) - 1u;
    const bool isOk = std::fwrite(kJson, 1u, length, file) == length;
    std::fclose(file);
    if (!isOk) {
      AC_FAIL("temp fixture write failed");
      return;
    }
  }
  ac::test::Fixture fixture;
  std::string error;
  if (!ac::test::loadFixtureFile(path, fixture, error)) {
    AC_FAIL(error.c_str());
    return;
  }
  AC_CHECK_EQ(fixture.keyframes.size(), 1u);
  if (fixture.keyframes.size() != 1u) return;
  const ac::test::FixtureKeyframe& frame = fixture.keyframes[0];
  AC_CHECK_EQ(frame.events.size(), 2u);
  if (frame.events.size() != 2u) return;
  // ① 带种类的类型：`kind` 是可选的**末位**键，出现就必须读出来（3 = king）。
  AC_CHECK(frame.events[0].type == std::string("sheepKilled"));
  AC_CHECK_EQ(static_cast<int>(frame.events[0].kind), 3);
  AC_CHECK(ac::test::eventCarriesKind(frame.events[0].type));
  // ② 其余类型：不写 `kind`，取默认 0，且投影不追加该字段。
  AC_CHECK(frame.events[1].type == std::string("playerHit"));
  AC_CHECK_EQ(static_cast<int>(frame.events[1].kind), 0);
  AC_CHECK(!ac::test::eventCarriesKind(frame.events[1].type));
  const std::string text =
      ac::test::projectionText(1u, std::vector<ac::test::FixtureCommand>{}, frame.entities, frame.events, frame.rng);
  AC_CHECK(text.find("evt=1,sheepKilled,0,1,5,1,2,3,4,3\n") != std::string::npos);
  AC_CHECK(text.find("evt=1,playerHit,0,1,5,1,2,3,4\n") != std::string::npos);
}
