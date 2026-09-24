// S10 §6：房间、会话与对局流程的断言（--filter=match / --filter=matchstate）。
// 只用内存注册表与内存适配器驱动全流程，不监听任何真实端口。
#include "tiny_test.hpp"

#include "allocation_probe.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "config/combat.hpp"
#include "config/player.hpp"
#include "config/weapons.hpp"
#include "config/waves.hpp"
#include "core/quantize.hpp"
#include "net/codec.hpp"
#include "net/memory_transport.hpp"
#include "room/match_controller.hpp"
#include "room/phase.hpp"
#include "room/room.hpp"
#include "room/rooms.hpp"
#include "room/session.hpp"
#include "room/stats.hpp"
#include "sim/entity_table.hpp"
#include "sim/world.hpp"
#include "waves/director.hpp"

namespace {

namespace room = ac::room;
namespace net = ac::net;
namespace sim = ac::sim;

using net::DecodeResult;
using net::EncodeResult;
using net::MatchState;
using net::MatchStatePlayer;
using net::PacketHeader;
using net::PacketType;
using net::ReliableExt;
using room::JoinOutcome;
using room::MatchPhase;
using room::Session;
using room::StartOutcome;

constexpr uint32_t kRegistryStartMs = 0x51A7u;

PacketHeader makeHeader(PacketType type, uint16_t flags, uint16_t session, uint16_t seq) {
  return PacketHeader{net::kProtocolVersion, static_cast<uint8_t>(type), flags, session, seq};
}

// 一名假客户端 + 房间注册表 + 注入点计数。sessions 先 reserve，指针不会被搬走。
struct Harness {
  room::RoomRegistry registry{};
  std::vector<Session> sessions{};
  room::Room* room = nullptr;
  uint64_t nowMs = 100000u;
  uint32_t unicastCalls = 0u;
  uint32_t replicateCalls = 0u;
  room::RoomDeps deps{};

  static void onMatchState(void* user, const room::Room&, Session&, const MatchState&) {
    static_cast<Harness*>(user)->unicastCalls += 1u;
  }
  static void onReplicate(void* user, room::Room&) {
    static_cast<Harness*>(user)->replicateCalls += 1u;
  }

  explicit Harness(uint32_t capacity = 4u) {
    room::initRoomRegistry(registry, kRegistryStartMs);
    sessions.reserve(capacity);
    for (uint32_t i = 0u; i < capacity; ++i) {
      sessions.push_back(room::createSession(i + 1u, nowMs));
    }
    deps.user = this;
    deps.sendMatchState = &Harness::onMatchState;
    deps.replicate = &Harness::onReplicate;
  }

  JoinOutcome createAndJoin(uint32_t index, const char* name) {
    AC_CHECK(room::setSessionName(sessions[index], name));
    const JoinOutcome outcome = room::join(registry, "0000", sessions[index], nowMs);
    if (outcome == JoinOutcome::kOk) room = room::findRoom(registry, sessions[index].roomCode);
    return outcome;
  }

  JoinOutcome joinByCode(const char* code, uint32_t index, const char* name) {
    AC_CHECK(room::setSessionName(sessions[index], name));
    const JoinOutcome outcome = room::join(registry, code, sessions[index], nowMs);
    if (outcome == JoinOutcome::kOk) room = room::findRoom(registry, sessions[index].roomCode);
    return outcome;
  }

  void advance(uint32_t ticks) {
    for (uint32_t i = 0u; i < ticks; ++i) {
      nowMs += 50u;
      AC_CHECK(room::updateRoom(*room, deps, nowMs));
    }
  }

  void stepOnce() {
    nowMs += 50u;
    room::updateRoom(*room, deps, nowMs);
  }

  void setReady(uint32_t index, bool ready, uint8_t weapon = 0u) {
    room::roomSetReady(*room, sessions[index], ready, weapon, deps);
  }

  void startMatch(uint32_t hostIndex = 0u, uint32_t playerCount = 1u) {
    for (uint32_t i = 0u; i < playerCount; ++i) setReady(i, true);
    const StartOutcome outcome = room::tryStartMatch(*room, sessions[hostIndex]);
    AC_CHECK(outcome == StartOutcome::kOk);
    advance(30u);  // §5.2：1500ms = 30 tick
    AC_CHECK(room->phase == MatchPhase::kPlaying);
  }

  sim::Entity* entity(uint32_t pid) const { return room::playerEntityAt(*room, pid); }
};

// 把对局推到「第 10 波已清空」的唯一现场：波次 = 10、导演刚组队完、场上无羊。
void forceFinalWave(room::Room& room) {
  room.wave = ac::config::kWaveMax;
  room.director = ac::waves::createDirectorState();
  room.director.wave = ac::config::kWaveMax;
  sim::World& world = *room.world;
  for (std::size_t i = world.activeCount; i > 0u; --i) {
    const uint16_t id = world.activeIds[i - 1u];
    const sim::EntityKind kind = world.entities[id - 1u].kind;
    if (kind == sim::EntityKind::kSheep || kind == sim::EntityKind::kProjectile) {
      sim::despawnEntity(world, id);
    }
  }
  world.stats.aliveSheep = 0u;
}

}  // namespace

// ---------------------------------------------------------------- §5.1 阶段机

AC_TEST(match_phase_names_codes_and_validity) {
  AC_CHECK_EQ(static_cast<int32_t>(room::kMatchPhaseCount), 5);
  AC_CHECK_EQ(static_cast<int32_t>(MatchPhase::kLobby), 0);
  AC_CHECK_EQ(static_cast<int32_t>(MatchPhase::kLoading), 1);
  AC_CHECK_EQ(static_cast<int32_t>(MatchPhase::kPlaying), 2);
  AC_CHECK_EQ(static_cast<int32_t>(MatchPhase::kIntermission), 3);
  AC_CHECK_EQ(static_cast<int32_t>(MatchPhase::kEnded), 4);
  AC_CHECK(room::matchPhaseName(MatchPhase::kLobby) == "lobby");
  AC_CHECK(room::matchPhaseName(MatchPhase::kLoading) == "loading");
  AC_CHECK(room::matchPhaseName(MatchPhase::kPlaying) == "playing");
  AC_CHECK(room::matchPhaseName(MatchPhase::kIntermission) == "intermission");
  AC_CHECK(room::matchPhaseName(MatchPhase::kEnded) == "ended");
  AC_CHECK(room::isValidMatchPhase(0) && room::isValidMatchPhase(4));
  AC_CHECK(!room::isValidMatchPhase(5) && !room::isValidMatchPhase(-1));
  AC_CHECK_EQ(static_cast<int32_t>(room::matchPhaseFromCode(9)), 0);
}

AC_TEST(match_phase_transition_table_is_frozen) {
  int32_t legal = 0;
  int32_t illegal = 0;
  for (int32_t from = 0; from < room::kMatchPhaseCount; ++from) {
    for (int32_t to = 0; to < room::kMatchPhaseCount; ++to) {
      const bool allowed = room::canMatchTransition(room::matchPhaseFromCode(from),
                                                   room::matchPhaseFromCode(to));
      if (allowed) {
        legal += 1;
      } else {
        illegal += 1;
      }
    }
  }
  AC_CHECK_EQ(legal, 7);
  AC_CHECK_EQ(illegal, 18);
  AC_CHECK(room::canMatchTransition(MatchPhase::kLobby, MatchPhase::kLoading));
  AC_CHECK(room::canMatchTransition(MatchPhase::kLoading, MatchPhase::kPlaying));
  AC_CHECK(room::canMatchTransition(MatchPhase::kPlaying, MatchPhase::kIntermission));
  AC_CHECK(room::canMatchTransition(MatchPhase::kPlaying, MatchPhase::kEnded));
  AC_CHECK(room::canMatchTransition(MatchPhase::kIntermission, MatchPhase::kPlaying));
  AC_CHECK(room::canMatchTransition(MatchPhase::kIntermission, MatchPhase::kEnded));
  AC_CHECK(room::canMatchTransition(MatchPhase::kEnded, MatchPhase::kLobby));
  AC_CHECK(!room::canMatchTransition(MatchPhase::kLobby, MatchPhase::kPlaying));
  AC_CHECK(!room::canMatchTransition(MatchPhase::kLobby, MatchPhase::kEnded));
  AC_CHECK(!room::canMatchTransition(MatchPhase::kEnded, MatchPhase::kPlaying));
}

AC_TEST(match_transition_denied_records_warning_without_changing_state) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  AC_CHECK(room.phase == MatchPhase::kLobby);
  AC_CHECK(!room::canMatchTransition(MatchPhase::kLobby, MatchPhase::kLobby));  // 表里同态非法
  AC_CHECK(!room::applyMatchTransition(room, MatchPhase::kPlaying));
  AC_CHECK(room.phase == MatchPhase::kLobby);
  AC_CHECK_EQ(room.match.counters.deniedTransitions, 1u);
  AC_CHECK(room::applyMatchTransition(room, MatchPhase::kLobby));  // 已达成：不记警告
  AC_CHECK_EQ(room.match.counters.deniedTransitions, 1u);
  AC_CHECK(room::applyMatchTransition(room, MatchPhase::kLoading));
  AC_CHECK(room.phase == MatchPhase::kLoading);
  AC_CHECK_EQ(room.match.counters.deniedTransitions, 1u);
  AC_CHECK(room::applyMatchTransition(room, MatchPhase::kLoading));
  AC_CHECK_EQ(room.match.counters.deniedTransitions, 1u);
}

AC_TEST(match_ended_to_lobby_resets_records_wave_and_director) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch();
  room::Room& room = *harness.room;
  room.wave = 4;
  room.director.wave = 4;
  room.director.planned = 9;
  room.match.records[0].stats.kills = 7u;
  room.match.winnerTeam = 1u;
  room.match.endedAtMs = room.match.startedAtMs + 1234u;
  AC_CHECK(room::applyMatchTransition(room, MatchPhase::kEnded));
  room::resetMatchForRestart(room);
  AC_CHECK(room.phase == MatchPhase::kLobby);
  AC_CHECK_EQ(room.wave, 0);
  AC_CHECK_EQ(room.director.wave, 0);
  AC_CHECK_EQ(room.director.planned, 0);
  AC_CHECK_EQ(room.match.winnerTeam, 0u);
  AC_CHECK_EQ(room.match.startedAtMs, 0u);
  AC_CHECK_EQ(room.match.recordCount, 1u);
  AC_CHECK_EQ(room.match.records[0].stats.kills, 0u);
  AC_CHECK_EQ(harness.entity(1u)->hp, harness.entity(1u)->maxHp);
}

// ---------------------------------------------------------------- §5.3 房间码与建房

AC_TEST(match_room_code_alphabet_uniqueness_and_retry) {
  room::RoomRegistry registry;
  room::initRoomRegistry(registry, kRegistryStartMs);
  std::vector<std::string> alive{};
  std::vector<std::string> seen{};
  uint32_t failures = 0u;
  for (int32_t i = 0; i < 200; ++i) {
    room::Room* const room = room::createRoom(registry, 1000u);
    if (room == nullptr) {  // 容量到顶：回收一间再继续（§5.3 的 64 房上限）
      AC_CHECK_EQ(registry.count, room::kMaxRooms);
      failures += 1u;
      AC_CHECK_EQ(registry.createFailureCount, failures);
      room::destroyRoom(registry, *registry.rooms[0].get());
      alive.clear();
      continue;
    }
    const std::string code(room->code);
    AC_CHECK_EQ(code.size(), room::kRoomCodeLength);
    AC_CHECK(code != room::kNewRoomCode);
    for (const char value : code) {
      AC_CHECK(std::strchr(room::kRoomCodeAlphabet, value) != nullptr);
    }
    for (const std::string& other : alive) AC_CHECK(other != code);
    alive.push_back(code);
    seen.push_back(code);
    if (registry.count >= room::kMaxRooms) {
      AC_CHECK(room::createRoom(registry, 1000u) == nullptr);
      failures += 1u;
      AC_CHECK_EQ(registry.createFailureCount, failures);
      room::destroyRoom(registry, *room);
      alive.clear();
    }
  }
  AC_CHECK_EQ(seen.size(), 200u);
  AC_CHECK(registry.count <= room::kMaxRooms);
}

AC_TEST(match_room_code_reserved_0000_creates_new_room) {
  Harness harness(2u);
  AC_CHECK(harness.joinByCode("0000", 0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK_EQ(harness.registry.count, 1);
  AC_CHECK(harness.room != nullptr);
  AC_CHECK(std::strcmp(harness.sessions[0].roomCode, room::kNewRoomCode) != 0);
  AC_CHECK_EQ(harness.room->sessionCount, 1u);
}

AC_TEST(match_room_code_normalization_and_format_validation) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  char lower[5] = {};
  for (std::size_t i = 0u; i < room::kRoomCodeLength; ++i) {
    const char value = harness.room->code[i];
    lower[i] = value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
  }
  AC_CHECK(harness.joinByCode(lower, 1u, "beta") == JoinOutcome::kOk);
  AC_CHECK_EQ(harness.room->sessionCount, 2u);
  Harness other(1u);
  AC_CHECK(other.joinByCode("ABC", 0u, "gamma") == JoinOutcome::kRoomNotFound);
  AC_CHECK(other.joinByCode("AB0D", 0u, "gamma") == JoinOutcome::kRoomNotFound);
  AC_CHECK(other.joinByCode("ABCD", 0u, "gamma") == JoinOutcome::kRoomNotFound);
  AC_CHECK_EQ(other.registry.count, 0);
}

AC_TEST(match_room_full_after_four_players) {
  Harness harness(5u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  const char* names[3] = {"beta", "gamma", "delta"};
  for (uint32_t i = 1u; i < 4u; ++i) {
    AC_CHECK(harness.joinByCode(harness.room->code, i, names[i - 1u]) == JoinOutcome::kOk);
  }
  AC_CHECK_EQ(harness.room->sessionCount, 4u);
  AC_CHECK_EQ(static_cast<int32_t>(harness.room->capacity), room::kMaxPlayersPerRoom);
  AC_CHECK(harness.joinByCode(harness.room->code, 4u, "epsilon") == JoinOutcome::kRoomFull);
  AC_CHECK_EQ(harness.room->sessionCount, 4u);
  AC_CHECK(harness.sessions[4].pid == 0u);
  AC_CHECK_EQ(room::joinOutcomeName(JoinOutcome::kRoomFull), std::string("room-full"));
}

// ---------------------------------------------------------------- §5.4 会话

AC_TEST(match_join_assigns_pids_spawn_points_and_host) {
  Harness harness(4u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  const char* names[3] = {"beta", "gamma", "delta"};
  for (uint32_t i = 1u; i < 4u; ++i) {
    AC_CHECK(harness.joinByCode(harness.room->code, i, names[i - 1u]) == JoinOutcome::kOk);
  }
  for (uint32_t i = 0u; i < 4u; ++i) {
    AC_CHECK_EQ(harness.sessions[i].pid, i + 1u);
    AC_CHECK(harness.sessions[i].token == 0u);
    AC_CHECK(!harness.sessions[i].ready);
    AC_CHECK_EQ(std::strcmp(harness.sessions[i].roomCode, harness.room->code), 0);
    AC_CHECK(harness.entity(i + 1u) != nullptr);
  }
  AC_CHECK_EQ(harness.room->match.hostId, 1u);
  AC_CHECK_EQ(harness.room->match.recordCount, 4u);
  AC_CHECK(harness.entity(1u)->pos.x != harness.entity(2u)->pos.x ||
           harness.entity(1u)->pos.z != harness.entity(2u)->pos.z);
  AC_CHECK(harness.entity(1u)->hp == harness.entity(1u)->maxHp);
}

AC_TEST(match_join_rejected_only_while_playing) {
  Harness harness(5u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch(0u, 1u);
  AC_CHECK(harness.room->phase == MatchPhase::kPlaying);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kMatchInProgress);
  AC_CHECK(harness.sessions[1].pid == 0u);
  AC_CHECK_EQ(harness.room->sessionCount, 1u);
  room::Room& room = *harness.room;
  AC_CHECK(room::applyMatchTransition(room, MatchPhase::kIntermission));
  AC_CHECK(harness.joinByCode(room.code, 1u, "beta") == JoinOutcome::kOk);
  AC_CHECK_EQ(room.sessionCount, 2u);
}

AC_TEST(match_start_requires_host_and_all_ready) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  AC_CHECK(room::tryStartMatch(room, harness.sessions[1]) == StartOutcome::kNotHost);
  harness.setReady(0u, true);
  AC_CHECK(room::tryStartMatch(room, harness.sessions[0]) == StartOutcome::kNotReady);
  harness.setReady(1u, true);
  AC_CHECK(room::tryStartMatch(room, harness.sessions[0]) == StartOutcome::kOk);
  AC_CHECK(room.phase == MatchPhase::kLoading);
  AC_CHECK_EQ(room.match.loadingMs, room::kLoadingMs);
  AC_CHECK(room::tryStartMatch(room, harness.sessions[0]) == StartOutcome::kWrongPhase);
  AC_CHECK_EQ(room::startOutcomeName(StartOutcome::kNotReady), std::string("not-ready"));
}

AC_TEST(match_nickname_sanitizer_rules) {
  Session session = room::createSession(1u, 0u);
  AC_CHECK(room::setSessionName(session, "alpha_1"));
  AC_CHECK_EQ(session.nameBytes, 7u);
  AC_CHECK(room::setSessionName(session, "\xE9\x99\x88sir"));
  AC_CHECK_EQ(session.nameBytes, 6u);
  AC_CHECK(!room::setSessionName(session, "<b>alpha</b>"));  // 剔除后剩 balpha/b，/ 不在允许集
  AC_CHECK(room::setSessionName(session, "a<b>c"));
  AC_CHECK_EQ(std::strcmp(session.name, "abc"), 0);
  AC_CHECK(!room::setSessionName(session, ""));
  AC_CHECK(!room::setSessionName(session, "   "));
  AC_CHECK(!room::setSessionName(session, "alpha beta"));
  AC_CHECK(!room::setSessionName(session, "abcdefghijklm"));
  AC_CHECK(room::setSessionName(session, "abcdefghijkl"));
  AC_CHECK_EQ(session.nameBytes, 12u);
  AC_CHECK(!room::setSessionName(session, "\x01\x02"));
  AC_CHECK(room::setSessionName(session, "  alpha  "));
  AC_CHECK_EQ(std::strcmp(session.name, "alpha"), 0);
}

// ---------------------------------------------------------------- §5.2/§5.7 流程

AC_TEST(match_loading_runs_thirty_ticks_then_playing_wave_one) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.setReady(0u, true);
  AC_CHECK(room::tryStartMatch(*harness.room, harness.sessions[0]) == StartOutcome::kOk);
  room::Room& room = *harness.room;
  harness.advance(29u);
  AC_CHECK(room.phase == MatchPhase::kLoading);
  AC_CHECK_EQ(room.match.loadingMs, 50);
  AC_CHECK_EQ(room.wave, 0);
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kPlaying);
  AC_CHECK_EQ(room.wave, 1);
  AC_CHECK_EQ(room.match.startedAtMs, harness.nowMs);
  AC_CHECK_EQ(room.match.counters.matchesStarted, 1u);
  AC_CHECK_EQ(room.match.counters.ticks, 30u);
  AC_CHECK_EQ(room.world->tick, 1u);
}

AC_TEST(match_intermission_runs_four_hundred_ticks_then_wave_two) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch();
  room::Room& room = *harness.room;
  harness.setReady(0u, false);  // 否则 5000ms 后会被全员准备跳过
  AC_CHECK(room::handleWaveCleared(room, harness.nowMs));
  AC_CHECK(room.phase == MatchPhase::kIntermission);
  AC_CHECK_EQ(room.intermissionMs, room::kIntermissionMs);
  harness.advance(399u);
  AC_CHECK(room.phase == MatchPhase::kIntermission);
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kPlaying);
  AC_CHECK_EQ(room.wave, 2);
}

AC_TEST(match_intermission_skip_needs_min_time_and_all_ready) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  harness.startMatch(0u, 2u);
  room::Room& room = *harness.room;
  AC_CHECK(room::handleWaveCleared(room, harness.nowMs));
  AC_CHECK(room.phase == MatchPhase::kIntermission);
  harness.advance(99u);
  AC_CHECK(room.phase == MatchPhase::kIntermission);
  AC_CHECK(room.intermissionMs > 0);
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kPlaying);
  AC_CHECK_EQ(room.intermissionMs, 0);
  AC_CHECK_EQ(room.wave, 2);
}

AC_TEST(match_intermission_skip_blocked_when_anyone_not_ready) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  harness.startMatch(0u, 2u);
  room::Room& room = *harness.room;
  AC_CHECK(room::handleWaveCleared(room, harness.nowMs));
  harness.setReady(1u, false);
  harness.advance(100u);
  AC_CHECK(room.phase == MatchPhase::kIntermission);
  AC_CHECK_EQ(room.intermissionMs, room::kIntermissionMs - 5000);
  harness.setReady(1u, true);
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kPlaying);
  AC_CHECK_EQ(room.wave, 2);
}

AC_TEST(match_enter_playing_refills_everything) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.setReady(0u, true);
  AC_CHECK(room::tryStartMatch(*harness.room, harness.sessions[0]) == StartOutcome::kOk);
  sim::Entity* const entity = harness.entity(1u);
  AC_CHECK(entity != nullptr);
  entity->hp = 1.0;  // 进 playing 前把全部补给项弄脏
  entity->armor = 0.0;
  for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) entity->weapon.magInSlot[slot] = 0;
  entity->weapon.reserveAmmo = 0;
  entity->weapon.reloadEndsAtMs = 999.0;
  entity->weapon.spreadDeg = 3.5;
  entity->downed.downed = true;
  entity->rage.value = 0.8;
  harness.advance(30u);
  AC_CHECK(harness.room->phase == MatchPhase::kPlaying);
  AC_CHECK_EQ(entity->hp, entity->maxHp);
  AC_CHECK_EQ(entity->armor, ac::config::kMaxArmor);
  for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
    AC_CHECK_EQ(entity->weapon.magInSlot[slot], ac::config::kWeapons[slot].mag);
  }
  AC_CHECK_EQ(entity->weapon.reserveAmmo, ac::config::kReserveAmmoInitial);
  AC_CHECK_EQ(entity->weapon.reloadEndsAtMs, 0.0);
  AC_CHECK_EQ(entity->weapon.spreadDeg, 0.0);
  AC_CHECK(!entity->downed.downed);
  AC_CHECK_EQ(entity->rage.value, 0.0);
}

AC_TEST(match_tick_accumulator_uses_fifty_ms_steps) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  const uint32_t before = room.match.counters.ticks;
  harness.nowMs += 100u;
  AC_CHECK(room::updateRoom(room, harness.deps, harness.nowMs));
  AC_CHECK_EQ(room.match.counters.ticks, before + 2u);
  harness.nowMs += 40u;
  AC_CHECK(!room::updateRoom(room, harness.deps, harness.nowMs));
  AC_CHECK_EQ(room.match.counters.ticks, before + 2u);
  harness.nowMs += 10u;
  AC_CHECK(room::updateRoom(room, harness.deps, harness.nowMs));
  AC_CHECK_EQ(room.match.counters.ticks, before + 3u);
}

AC_TEST(match_commands_are_stepped_only_in_playing) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  sim::Command command{};
  command.moveX = 1.0;
  AC_CHECK(room::roomApplyCommand(room, harness.sessions[0], command));
  const double beforeX = harness.entity(1u)->pos.x;
  harness.advance(5u);
  AC_CHECK_EQ(harness.entity(1u)->pos.x, beforeX);
  AC_CHECK_EQ(room.world->tick, 0u);
  harness.startMatch();
  const uint32_t ticksAfterStart = room.world->tick;
  AC_CHECK(ticksAfterStart >= 1u);
  harness.advance(5u);
  AC_CHECK_EQ(room.world->tick, ticksAfterStart + 5u);
}

// ---------------------------------------------------------------- §5.5 宽限与重连

AC_TEST(match_disconnect_keeps_slot_inside_grace_period) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  harness.sessions[1].token = 0xABCD1234u;
  AC_CHECK(room::roomDisconnect(room, harness.sessions[1], harness.nowMs));
  AC_CHECK(!room::isConnected(harness.sessions[1]));
  AC_CHECK(harness.entity(2u)->idle);
  AC_CHECK_EQ(harness.entity(2u)->vel.x, 0.0);
  AC_CHECK_EQ(room.sessionCount, 2u);
  harness.advance(599u);
  AC_CHECK_EQ(room.sessionCount, 2u);
  AC_CHECK_EQ(room.match.counters.graceTimeouts, 0u);
}

AC_TEST(match_grace_timeout_releases_slot_and_blocks_reconnect) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  harness.sessions[1].token = 0x11112222u;
  const uint32_t pid = harness.sessions[1].pid;
  room::roomDisconnect(room, harness.sessions[1], harness.nowMs);
  harness.advance(601u);
  AC_CHECK_EQ(room.sessionCount, 1u);
  AC_CHECK_EQ(room.match.counters.graceTimeouts, 1u);
  AC_CHECK(harness.sessions[1].pid == 0u);
  AC_CHECK_EQ(harness.sessions[1].roomCode[0], '\0');
  AC_CHECK(room::playerEntityAt(room, pid) == nullptr);
  Session returning = room::createSession(9u, harness.nowMs);
  AC_CHECK(!room::reconnect(harness.registry, 0x11112222u, returning));
  AC_CHECK(returning.pid == 0u);
}

AC_TEST(match_reconnect_matches_token_only_and_compensates) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  harness.sessions[1].token = 0x55556666u;
  const uint32_t pid = harness.sessions[1].pid;
  sim::Entity* const entity = harness.entity(pid);
  entity->hp = entity->maxHp * 0.25;
  entity->downed.downed = true;
  room::roomDisconnect(room, harness.sessions[1], harness.nowMs);
  harness.advance(10u);
  Session impostor = room::createSession(7u, harness.nowMs);
  AC_CHECK(room::setSessionName(impostor, "beta"));
  impostor.token = 0x99999999u;
  AC_CHECK(!room::reconnect(harness.registry, impostor.token, impostor));
  Session returning = room::createSession(8u, harness.nowMs);
  AC_CHECK(room::setSessionName(returning, "zeta"));
  returning.token = 0x55556666u;
  AC_CHECK(room::reconnect(harness.registry, returning.token, returning));
  AC_CHECK_EQ(returning.pid, pid);
  AC_CHECK_EQ(std::strcmp(returning.name, "beta"), 0);
  AC_CHECK_EQ(returning.token, 0x55556666u);
  AC_CHECK_EQ(room.sessionCount, 2u);
  AC_CHECK(room::sessionByPid(room, pid) == &returning);
  AC_CHECK(harness.sessions[1].pid == 0u);
  AC_CHECK(!entity->idle);
  AC_CHECK_EQ(entity->vel.x, 0.0);
  AC_CHECK(!entity->downed.downed);
  AC_CHECK_EQ(entity->hp, entity->maxHp * room::kReconnectMinHpRatio);
  AC_CHECK_EQ(room.match.counters.graceReconnects, 1u);
}

AC_TEST(match_leave_marks_left_mid_match_and_frees_slot) {
  Harness harness(3u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 2u, "gamma") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  AC_CHECK(room::roomLeave(room, harness.sessions[2], harness.nowMs));
  AC_CHECK_EQ(room.sessionCount, 2u);
  AC_CHECK(harness.sessions[2].pid == 0u);
  AC_CHECK_EQ(std::strcmp(room.match.records[2].name, "gamma"), 0);
  AC_CHECK(!room.match.records[2].leftMidMatch);
  harness.startMatch(0u, 2u);
  AC_CHECK(room::roomLeave(room, harness.sessions[1], harness.nowMs));
  AC_CHECK_EQ(room.sessionCount, 1u);
  AC_CHECK(room.match.records[1].leftMidMatch);
  AC_CHECK_EQ(room.match.recordCount, 3u);
}

// ---------------------------------------------------------------- §5.7 结束判定与结算

AC_TEST(match_all_downed_ends_with_sheep_win) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  harness.startMatch(0u, 2u);
  room::Room& room = *harness.room;
  for (uint32_t pid = 1u; pid <= 2u; ++pid) harness.entity(pid)->downed.downed = true;
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kEnded);
  AC_CHECK_EQ(room.match.winnerTeam, 1u);
  AC_CHECK(room.match.hasResult);
  AC_CHECK_EQ(room.match.counters.matchesEnded, 1u);
}

AC_TEST(match_wave_ten_clear_ends_with_player_win) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch();
  room::Room& room = *harness.room;
  forceFinalWave(room);
  AC_CHECK_EQ(room.director.isFinished, false);
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kEnded);
  AC_CHECK(room.director.isFinished);
  AC_CHECK_EQ(room.match.winnerTeam, 0u);
  AC_CHECK_EQ(room.match.lastResult.waveReached, ac::config::kWaveMax);
  AC_CHECK_EQ(room.match.lastResult.winnerTeam, 0u);
}

AC_TEST(match_empty_room_ends_match_with_sheep_win) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch();
  room::Room& room = *harness.room;
  AC_CHECK(room::roomLeave(room, harness.sessions[0], harness.nowMs));
  AC_CHECK_EQ(room.sessionCount, 0u);
  harness.stepOnce();
  AC_CHECK(room.phase == MatchPhase::kEnded);
  AC_CHECK_EQ(room.match.winnerTeam, 1u);
  AC_CHECK_EQ(room.match.counters.matchesEnded, 1u);
}

AC_TEST(match_match_ended_event_is_updated_not_duplicated) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch();
  room::Room& room = *harness.room;
  forceFinalWave(room);
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kEnded);
  uint32_t matchEndedCount = 0u;
  const sim::Event* found = nullptr;
  for (std::size_t i = 0u; i < room.world->eventCount; ++i) {
    const sim::Event& event = room.world->events[i];
    if (event.type != sim::kEventMatchEnded) continue;
    matchEndedCount += 1u;
    found = &event;
  }
  AC_CHECK_EQ(matchEndedCount, 1u);
  AC_CHECK(found != nullptr);
  if (found != nullptr) {
    AC_CHECK_EQ(found->subjectId, 0u);
    AC_CHECK_EQ(static_cast<int32_t>(found->value), ac::config::kWaveMax);
  }
}

AC_TEST(match_settlement_record_fields_and_match_id) {
  Harness harness(3u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 2u, "gamma") == JoinOutcome::kOk);
  harness.startMatch(0u, 3u);
  room::Room& room = *harness.room;
  forceFinalWave(room);
  harness.advance(1u);
  const room::MatchResultRecord record = room::buildMatchResult(room);
  char expected[64] = {};  // 独立拼接，不复用被测的 writeMatchId
  std::snprintf(expected, sizeof(expected), "%s-%llu", room.code,
                static_cast<unsigned long long>(room.match.startedAtMs));
  AC_CHECK_EQ(std::strcmp(record.matchId, expected), 0);
  AC_CHECK_EQ(record.matchId[4], '-');
  AC_CHECK_EQ(record.startedAtMs, room.match.startedAtMs);
  AC_CHECK_EQ(record.durationMs, room.match.endedAtMs - room.match.startedAtMs);
  AC_CHECK_EQ(record.waveReached, ac::config::kWaveMax);
  AC_CHECK_EQ(record.winnerTeam, 0u);
  AC_CHECK_EQ(record.playerCount, 3u);
  AC_CHECK_EQ(std::strcmp(record.players[0].name, "alpha"), 0);
  AC_CHECK_EQ(record.players[0].nameBytes, 5u);
  AC_CHECK(record.players[2].kills == 0u && record.players[2].headshots == 0u);
}

AC_TEST(match_stats_map_tick_events_to_players) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  harness.startMatch(0u, 2u);
  room::Room& room = *harness.room;
  room.world->eventCount = 0u;  // 只统计本 tick 的事件
  sim::pushEvent(*room.world, sim::kEventPlayerHit, 0u, 1u, 2u, 0.0, 0.0, 0.0, 25.0);
  sim::pushEvent(*room.world, sim::kEventSheepKilled, ac::config::kHitFlagHeadshot, 1u, 0u, 0.0, 0.0,
                 0.0, 0.0);
  sim::pushEvent(*room.world, sim::kEventSheepKilled, 0u, 1u, 0u, 0.0, 0.0, 0.0, 0.0);
  sim::pushEvent(*room.world, sim::kEventPlayerDowned, 0u, 1u, 2u, 0.0, 0.0, 0.0, 0.0);
  sim::pushEvent(*room.world, sim::kEventReviveDone, 0u, 1u, 2u, 0.0, 0.0, 0.0, 0.0);
  room::accumulateMatchEvents(room);
  room::PlayerRecord* const alpha = room::playerRecordFor(room, 1u);
  room::PlayerRecord* const beta = room::playerRecordFor(room, 2u);
  AC_CHECK(alpha != nullptr && beta != nullptr);
  if (alpha != nullptr && beta != nullptr) {
    AC_CHECK_EQ(alpha->stats.hits, 1u);
    AC_CHECK_EQ(alpha->stats.kills, 2u);
    AC_CHECK_EQ(alpha->stats.headshots, 1u);
    AC_CHECK_EQ(alpha->stats.revives, 1u);
    AC_CHECK_EQ(alpha->stats.downs, 0u);
    AC_CHECK_EQ(beta->stats.downs, 1u);
    AC_CHECK_EQ(beta->stats.kills, 0u);
    room.world->eventCount = 0u;  // 跨 tick 不重放
    room::accumulateMatchEvents(room);
    AC_CHECK_EQ(alpha->stats.kills, 2u);
    AC_CHECK_EQ(alpha->stats.hits, 1u);
  }
}

AC_TEST(match_shot_deltas_count_shots_fired) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  harness.startMatch(0u, 2u);
  room::Room& room = *harness.room;
  sim::Entity* const entity = harness.entity(1u);
  room::captureShotBaseline(room);
  entity->weapon.magInSlot[entity->weapon.activeSlot] -= 3;
  room::applyShotDeltas(room);
  room::PlayerRecord* record = room::playerRecordFor(room, 1u);
  AC_CHECK(record != nullptr && record->stats.shotsFired == 3u);
  room::captureShotBaseline(room);
  entity->weapon.activeSlot = static_cast<uint8_t>((entity->weapon.activeSlot + 1u) % 3u);
  entity->weapon.magInSlot[entity->weapon.activeSlot] = 1;
  room::applyShotDeltas(room);
  record = room::playerRecordFor(room, 1u);
  AC_CHECK(record != nullptr && record->stats.shotsFired == 3u);
}

AC_TEST(match_alive_and_downed_time_only_accumulate_in_match_phases) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  room::accumulateMatchTime(room, 50u);
  AC_CHECK_EQ(room.match.records[0].stats.aliveMs, 0u);
  harness.startMatch(0u, 2u);
  AC_CHECK_EQ(room.match.records[0].stats.aliveMs, 50u);
  harness.entity(2u)->downed.downed = true;
  harness.advance(2u);
  AC_CHECK_EQ(room.match.records[0].stats.aliveMs, 150u);
  AC_CHECK_EQ(room.match.records[1].stats.downedMs, 100u);
  AC_CHECK_EQ(room.match.records[1].stats.aliveMs, 50u);
  room::accumulateMatchTime(room, 50u);
  AC_CHECK_EQ(room.match.records[0].stats.aliveMs, 200u);
}

AC_TEST(match_idle_room_reclaimed_after_sixty_seconds) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  const uint64_t emptyAtMs = harness.nowMs;
  room::roomDisconnect(*harness.room, harness.sessions[0], harness.nowMs);
  room::roomDisconnect(*harness.room, harness.sessions[1], harness.nowMs);
  AC_CHECK(room::roomIsIdle(*harness.room));
  harness.nowMs += 100u;
  AC_CHECK(!room::updateRoom(*harness.room, harness.deps, harness.nowMs));
  AC_CHECK_EQ(harness.room->emptySinceMs, static_cast<int64_t>(emptyAtMs));
  harness.nowMs += static_cast<uint64_t>(room::kEmptyRoomReclaimMs) - 200u;
  AC_CHECK_EQ(room::reclaimIdle(harness.registry, harness.nowMs), 0);
  AC_CHECK_EQ(harness.registry.count, 1);
  harness.nowMs += 200u;
  AC_CHECK_EQ(room::reclaimIdle(harness.registry, harness.nowMs), 1);
  AC_CHECK_EQ(harness.registry.count, 0);
  AC_CHECK_EQ(harness.registry.reclaimedCount, 1u);
  AC_CHECK(harness.sessions[0].pid == 0u);
  AC_CHECK_EQ(harness.sessions[0].roomCode[0], '\0');
  AC_CHECK_EQ(harness.sessions[0].disconnectedAtMs, -1);
  AC_CHECK(!room::reconnect(harness.registry, 0x1234u, harness.sessions[0]));
}

AC_TEST(match_connected_room_is_never_reclaimed) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(!room::roomIsIdle(*harness.room));
  AC_CHECK(harness.room->emptySinceMs < 0);
  harness.nowMs += static_cast<uint64_t>(room::kEmptyRoomReclaimMs) * 2u;
  AC_CHECK_EQ(room::reclaimIdle(harness.registry, harness.nowMs), 0);
  AC_CHECK_EQ(harness.registry.count, 1);
}

// ---------------------------------------------------------------- §5.7-5/§5.8 广播与单播

AC_TEST(match_replication_hook_runs_once_per_tick) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch();
  room::Room& room = *harness.room;
  const uint32_t before = harness.replicateCalls;
  harness.advance(10u);
  AC_CHECK_EQ(harness.replicateCalls, before + 10u);
  AC_CHECK_EQ(room.match.counters.ticks, 40u);
}

AC_TEST(matchstate_periodic_unicast_every_twenty_ticks) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  harness.startMatch(0u, 2u);
  room::Room& room = *harness.room;
  const uint32_t base = room.unicastCount;
  const uint32_t calls = harness.unicastCalls;
  AC_CHECK_EQ(room.matchStateTimerMs, 500);  // 30 tick = 1500ms，已发过一次 1000ms 节拍
  harness.advance(9u);
  AC_CHECK_EQ(room.matchStateTimerMs, 950);
  AC_CHECK_EQ(room.unicastCount, base);
  harness.advance(1u);  // 累加器到 1000ms：减 1000 并单播一份
  AC_CHECK_EQ(room.unicastCount, base + 2u);
  AC_CHECK_EQ(harness.unicastCalls, calls + 2u);
  AC_CHECK_EQ(room.matchStateTimerMs, 0);
  harness.advance(20u);
  AC_CHECK_EQ(room.unicastCount, base + 4u);
}

AC_TEST(matchstate_immediate_resend_on_phase_change) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.setReady(0u, true);
  AC_CHECK(room::tryStartMatch(*harness.room, harness.sessions[0]) == StartOutcome::kOk);
  room::Room& room = *harness.room;
  const uint32_t base = room.unicastCount;
  harness.advance(1u);
  AC_CHECK_EQ(room.unicastCount, base + 1u);
  AC_CHECK_EQ(room.immediateCount, 1u);
  harness.advance(29u);
  AC_CHECK(room.phase == MatchPhase::kPlaying);
  AC_CHECK(room.unicastCount > base + 1u);
  AC_CHECK(room.immediateCount >= 2u);
}

AC_TEST(matchstate_immediate_resend_on_ready_change) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  const uint32_t base = room.unicastCount;
  harness.setReady(0u, true, 1u);
  AC_CHECK_EQ(room.unicastCount, base + 1u);
  AC_CHECK_EQ(harness.sessions[0].weapon, 1u);
  harness.setReady(0u, true, 1u);
  AC_CHECK_EQ(room.unicastCount, base + 1u);
}

AC_TEST(matchstate_immediate_resend_on_join_and_leave) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  harness.stepOnce();
  const uint32_t base = room.unicastCount;
  AC_CHECK(harness.joinByCode(room.code, 1u, "beta") == JoinOutcome::kOk);
  harness.nowMs += 10u;
  AC_CHECK(!room::updateRoom(room, harness.deps, harness.nowMs));
  AC_CHECK_EQ(room.unicastCount, base + 2u);
  const uint32_t afterJoin = room.unicastCount;
  AC_CHECK(room::roomLeave(room, harness.sessions[1], harness.nowMs));
  harness.nowMs += 10u;
  room::updateRoom(room, harness.deps, harness.nowMs);
  AC_CHECK_EQ(room.unicastCount, afterJoin + 1u);
}

AC_TEST(matchstate_two_player_payload_roundtrip) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "a") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "abcdefghijkl") == JoinOutcome::kOk);
  room::Room& room = *harness.room;
  harness.setReady(0u, true, 2u);
  harness.setReady(1u, true, 1u);
  harness.stepOnce();  // 让选枪落到实体上（v1：weaponApplied 后按实体槽位上报）
  harness.entity(2u)->downed.downed = true;
  const std::size_t count = room::buildMatchState(room);
  AC_CHECK_EQ(count, 2u);
  AC_CHECK_EQ(room.matchState.players.size(), 2u);
  AC_CHECK_EQ(room.matchState.phase, static_cast<uint8_t>(MatchPhase::kLobby));
  AC_CHECK_EQ(room.matchState.wave, 0u);
  uint8_t buffer[net::kMatchStateMaxBytes + 32u] = {};
  const PacketHeader header = makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u);
  const ReliableExt ext = ReliableExt{1u, 0u, 0u};
  const EncodeResult encoded =
      net::encodeMatchState(header, ext, room.matchState, buffer, sizeof(buffer));
  AC_CHECK(encoded.isOk);
  AC_CHECK(encoded.bytes <= net::kCommonHeaderBytes + net::kReliableExtBytes + net::kMatchStateMaxBytes);
  const DecodeResult<MatchState> decoded = net::decodeMatchState(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  if (decoded.isOk) {
    AC_CHECK_EQ(decoded.value.phase, static_cast<uint8_t>(MatchPhase::kLobby));
    AC_CHECK_EQ(decoded.value.wave, 0u);
    AC_CHECK_EQ(decoded.value.intermissionMs, 0u);
    AC_CHECK_EQ(decoded.value.players.size(), 2u);
    if (decoded.value.players.size() == 2u) {
      const MatchStatePlayer& first = decoded.value.players[0];
      const MatchStatePlayer& second = decoded.value.players[1];
      AC_CHECK_EQ(first.pid, 1u);
      AC_CHECK_EQ(first.name.size(), 1u);
      AC_CHECK_EQ(first.name, std::string("a"));
      AC_CHECK_EQ(first.ready, 1u);
      AC_CHECK_EQ(first.weapon, 2u);
      AC_CHECK_EQ(first.mag, static_cast<uint8_t>(ac::config::kWeapons[2].mag));
      AC_CHECK_EQ(first.reserve, static_cast<uint16_t>(ac::config::kReserveAmmoInitial));
      AC_CHECK_EQ(first.kills, 0u);
      AC_CHECK_EQ(first.downed, 0u);
      AC_CHECK_EQ(second.pid, 2u);
      AC_CHECK_EQ(second.name.size(), 12u);
      AC_CHECK_EQ(second.name, std::string("abcdefghijkl"));
      AC_CHECK_EQ(second.downed, 1u);
      AC_CHECK_EQ(second.weapon, 1u);
      AC_CHECK_EQ(second.mag, static_cast<uint8_t>(ac::config::kWeapons[1].mag));
    }
  }
}

AC_TEST(matchstate_quantized_fields_follow_hud_rules) {
  Harness harness(2u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  AC_CHECK(harness.joinByCode(harness.room->code, 1u, "beta") == JoinOutcome::kOk);
  harness.startMatch(0u, 2u);
  room::Room& room = *harness.room;
  sim::Entity* const entity = harness.entity(1u);
  entity->hp = entity->maxHp * 0.5;
  entity->downed.downed = true;
  entity->downed.reviveProgressMs = 500.0;
  AC_CHECK_EQ(room::buildMatchState(room), 2u);
  const MatchStatePlayer& first = room.matchState.players[0];
  AC_CHECK_EQ(first.hpRatio, ac::quantizeRatio(0.5));
  AC_CHECK_EQ(first.downed, 1u);
  AC_CHECK(first.reviveRatio255 > 0u);
  AC_CHECK_EQ(room.matchState.intermissionMs, 0u);
}

AC_TEST(matchstate_loading_and_intermission_report_timers) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.setReady(0u, true);
  AC_CHECK(room::tryStartMatch(*harness.room, harness.sessions[0]) == StartOutcome::kOk);
  room::Room& room = *harness.room;
  AC_CHECK_EQ(room::buildMatchState(room), 1u);
  AC_CHECK_EQ(room.matchState.intermissionMs, static_cast<uint16_t>(room::kLoadingMs));
  harness.advance(10u);
  AC_CHECK_EQ(room::buildMatchState(room), 1u);
  AC_CHECK_EQ(room.matchState.intermissionMs, static_cast<uint16_t>(room::kLoadingMs - 500));
  harness.advance(20u);
  AC_CHECK(room.phase == MatchPhase::kPlaying);
  harness.advance(1u);
  AC_CHECK_EQ(room.matchState.intermissionMs, 0u);
  AC_CHECK(room::handleWaveCleared(room, harness.nowMs));
  harness.advance(1u);
  AC_CHECK(room.phase == MatchPhase::kIntermission);
  AC_CHECK_EQ(room::buildMatchState(room), 1u);
  AC_CHECK_EQ(room.matchState.intermissionMs, static_cast<uint16_t>(room::kIntermissionMs - 50));
}

AC_TEST(matchstate_immediate_resend_when_player_downed) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  harness.startMatch();
  room::Room& room = *harness.room;
  const uint32_t base = room.immediateCount;
  harness.entity(1u)->downed.downed = true;
  harness.advance(1u);
  AC_CHECK_EQ(room.immediateCount, base + 1u);
  AC_CHECK_EQ(room.matchState.players[0].downed, 1u);
  harness.entity(1u)->downed.downed = false;
  harness.advance(1u);
  AC_CHECK_EQ(room.immediateCount, base + 2u);
  AC_CHECK_EQ(room.matchState.players[0].downed, 0u);
}

AC_TEST(matchstate_unicast_over_memory_transport) {
  Harness harness;
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  net::MemoryTransport transport(0x5EEDu);
  const net::EndpointId server{1u};
  const net::EndpointId client{2u};
  struct Sink {
    net::MemoryTransport* transport;
    net::EndpointId server;
    net::EndpointId client;
    uint32_t delivered;
    uint32_t replicated;
  } sink{&transport, server, client, 0u, 0u};
  harness.deps.user = &sink;
  harness.deps.replicate = [](void* user, room::Room&) {
    static_cast<Sink*>(user)->replicated += 1u;
  };
  harness.deps.sendMatchState = [](void* user, const room::Room&, Session&,
                                   const MatchState& state) {
    Sink* const target = static_cast<Sink*>(user);
    uint8_t buffer[net::kMatchStateMaxBytes + 32u] = {};
    const PacketHeader header = makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u);
    const ReliableExt ext = ReliableExt{1u, 0u, 0u};
    const EncodeResult encoded = net::encodeMatchState(header, ext, state, buffer, sizeof(buffer));
    if (!encoded.isOk) return;
    target->transport->send(target->server, target->client,
                            std::span<const uint8_t>(buffer, encoded.bytes));
    target->delivered += 1u;
  };
  harness.advance(20u);
  AC_CHECK(sink.delivered >= 1u);  // 进入 lobby 的首个 tick 立即补发一次 + 1000ms 节拍
  AC_CHECK_EQ(sink.replicated, 20u);
  AC_CHECK_EQ(transport.sentCount(), static_cast<std::size_t>(sink.delivered));
  MatchState received{};
  transport.pump(static_cast<uint32_t>(harness.nowMs),
                 [&received](net::EndpointId, std::span<const std::uint8_t> bytes) {
                   const DecodeResult<MatchState> decoded =
                       net::decodeMatchState(bytes.data(), bytes.size());
                   if (decoded.isOk) received = decoded.value;
                 });
  AC_CHECK_EQ(received.players.size(), 1u);
  AC_CHECK_EQ(received.phase, static_cast<uint8_t>(MatchPhase::kLobby));
  if (!received.players.empty()) {
    AC_CHECK_EQ(received.players[0].pid, 1u);
    AC_CHECK_EQ(received.players[0].name, std::string("alpha"));
  }
}

// ---------------------------------------------------------------- DoD：全流程零分配

AC_TEST(match_full_match_runs_three_thousand_ticks_without_allocating) {
  Harness harness(4u);
  AC_CHECK(harness.createAndJoin(0u, "alpha") == JoinOutcome::kOk);
  const char* names[3] = {"beta", "gamma", "delta"};
  for (uint32_t i = 1u; i < 4u; ++i) {
    AC_CHECK(harness.joinByCode(harness.room->code, i, names[i - 1u]) == JoinOutcome::kOk);
  }
  harness.startMatch(0u, 4u);
  room::Room& room = *harness.room;
  AC_CHECK(room.phase == MatchPhase::kPlaying);
  uint32_t waveMax = 0u;
  room::MatchPhase finalPhase = MatchPhase::kLobby;
  std::size_t allocations = 0u;
  {
    ac::test::AllocationScope scope;
    for (uint32_t i = 0u; i < 3000u; ++i) {
      for (uint32_t pid = 1u; pid <= 4u; ++pid) {
        sim::Entity* const entity = harness.entity(pid);
        if (entity != nullptr) entity->downed.downed = false;
      }
      harness.stepOnce();
      if (room.wave > static_cast<int32_t>(waveMax)) waveMax = static_cast<uint32_t>(room.wave);
    }
    allocations = scope.since();
    finalPhase = room.phase;
  }
  AC_CHECK_EQ(allocations, 0u);
  AC_CHECK(room.match.counters.ticks >= 3000u);
  AC_CHECK(waveMax >= 1u);
  AC_CHECK_EQ(room.matchState.players.size(), 4u);
  AC_CHECK(harness.unicastCalls > 0u);
  AC_CHECK(finalPhase == MatchPhase::kPlaying || finalPhase == MatchPhase::kIntermission ||
           finalPhase == MatchPhase::kEnded);
}
