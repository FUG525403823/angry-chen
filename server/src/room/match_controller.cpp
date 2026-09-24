#include "room/match_controller.hpp"

#include <cstring>

#include "combat/downed.hpp"
#include "combat/rage.hpp"
#include "combat/resolve.hpp"
#include "combat/weapon.hpp"
#include "config/combat.hpp"
#include "config/player.hpp"
#include "config/weapons.hpp"
#include "config/waves.hpp"
#include "room/room.hpp"
#include "sim/entity_table.hpp"
#include "sim/world.hpp"

namespace ac::room {
namespace {

// §5.8 立即补发判据的位域布局（见 matchStateSignature）。
constexpr uint32_t kPhaseBits = 3u;
constexpr uint32_t kCountBits = 3u;
constexpr uint32_t kMaskBits = 4u;
constexpr uint32_t kFingerprintBits = 4u;
constexpr uint32_t kWaveBits = 4u;

}  // namespace

// v1 的 room.world.entities[pid - 1]：pid 就是玩家实体的 EntityId。
ac::sim::Entity* playerEntityAt(Room& room, uint32_t pid) noexcept {
  if (room.world == nullptr || pid == 0u || pid > ac::sim::kMaxEntities) return nullptr;
  ac::sim::Entity& entity = room.world->entities[pid - 1u];
  if (!entity.active || entity.kind != ac::sim::EntityKind::kPlayer) return nullptr;
  return &entity;
}

const ac::sim::Entity* playerEntityAt(const Room& room, uint32_t pid) noexcept {
  return playerEntityAt(const_cast<Room&>(room), pid);
}

const char* startOutcomeName(StartOutcome outcome) noexcept {
  switch (outcome) {
    case StartOutcome::kOk:
      return "ok";
    case StartOutcome::kNotHost:
      return "not-host";
    case StartOutcome::kNotReady:
      return "not-ready";
    case StartOutcome::kWrongPhase:
      return "wrong-phase";
  }
  return "unknown";
}

bool applyMatchTransition(Room& room, MatchPhase next) noexcept {
  if (room.phase == next) return true;
  if (!canMatchTransition(room.phase, next)) {
    room.match.counters.deniedTransitions += 1u;
    return false;
  }
  room.phase = next;
  return true;
}

bool areAllPlayersReady(const Room& room) noexcept {
  uint32_t ready = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    if (!session->ready) return false;
    ready += 1u;
  }
  return ready >= 1u;
}

StartOutcome tryStartMatch(Room& room, Session& session) noexcept {
  if (session.pid == 0u || session.pid != room.match.hostId) return StartOutcome::kNotHost;
  if (room.phase == MatchPhase::kEnded) resetMatchForRestart(room);
  if (room.phase != MatchPhase::kLobby) return StartOutcome::kWrongPhase;
  if (!areAllPlayersReady(room)) return StartOutcome::kNotReady;
  if (!applyMatchTransition(room, MatchPhase::kLoading)) return StartOutcome::kWrongPhase;
  room.match.loadingMs = kLoadingMs;
  room.match.counters = MatchCounters{};
  room.match.counters.matchesStarted = 1u;
  return StartOutcome::kOk;
}

void resetMatchForRestart(Room& room) noexcept {
  if (!applyMatchTransition(room, MatchPhase::kLobby)) return;
  for (uint8_t i = 0u; i < kMaxPlayersPerRoom; ++i) room.match.records[i] = PlayerRecord{};
  room.match.recordCount = 0u;
  room.match.winnerTeam = 0u;
  room.match.startedAtMs = 0u;
  room.match.endedAtMs = 0u;
  room.match.loadingMs = 0;
  room.match.hasResult = false;
  room.match.lastResult = MatchResultRecord{};
  room.wave = 0;
  room.intermissionMs = 0;
  room.matchStateTimerMs = 0;
  room.director = ac::waves::createDirectorState();
  // 回收羊与投射物（倒序遍历 activeIds，避免下标失效）。
  if (room.world != nullptr) {
    ac::sim::World& world = *room.world;
    for (std::size_t i = world.activeCount; i > 0u; --i) {
      const uint16_t id = world.activeIds[i - 1u];
      const ac::sim::Entity& entity = world.entities[id - 1u];
      if (entity.kind == ac::sim::EntityKind::kSheep || entity.kind == ac::sim::EntityKind::kProjectile) {
        ac::sim::despawnEntity(world, id);
      }
    }
  }
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    joinMatchRecord(room, session->pid, session->name, session->nameBytes);
  }
  refillPlayers(room);
}

bool updateMatch(Room& room, uint64_t nowMs, uint32_t elapsedMs) noexcept {
  if (room.phase == MatchPhase::kLoading) {
    room.match.loadingMs -= static_cast<int64_t>(elapsedMs);
    if (room.match.loadingMs <= 0) {
      room.match.loadingMs = 0;
      if (!applyMatchTransition(room, MatchPhase::kPlaying)) return false;
      room.wave = 1;
      room.match.startedAtMs = nowMs;
      refillPlayers(room);
      return true;
    }
    return false;
  }
  if (room.phase == MatchPhase::kIntermission) {
    room.intermissionMs -= static_cast<int64_t>(elapsedMs);
    const int64_t elapsed = kIntermissionMs - room.intermissionMs;
    if (elapsed >= kIntermissionSkipMinMs && areAllPlayersReady(room)) room.intermissionMs = 0;
    if (room.intermissionMs <= 0) {
      room.intermissionMs = 0;
      if (!applyMatchTransition(room, MatchPhase::kPlaying)) return false;
      room.wave += 1;
      refillPlayers(room);
      return true;
    }
  }
  return false;
}

bool handleWaveCleared(Room& room, uint64_t nowMs) noexcept {
  if (room.phase != MatchPhase::kPlaying || room.world == nullptr) return false;
  ac::sim::reviveDownedForWaveClear(*room.world);
  if (room.wave >= ac::config::kWaveMax) {
    endMatch(room, nowMs, 0);
    return true;
  }
  if (!applyMatchTransition(room, MatchPhase::kIntermission)) return false;
  room.intermissionMs = kIntermissionMs;
  return true;
}

bool checkMatchEnd(Room& room, uint64_t nowMs) noexcept {
  if (room.phase != MatchPhase::kPlaying) return false;
  uint32_t active = 0u;
  bool allDowned = true;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    if (!isConnected(*session)) continue;
    active += 1u;
    const ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    if (entity == nullptr) continue;
    if (!entity->downed.downed) allDowned = false;
  }
  if (active == 0u) {
    if (room.sessionCount == 0u) {
      endMatch(room, nowMs, 1);
      return true;
    }
    return false;
  }
  if (allDowned) {
    endMatch(room, nowMs, 1);
    return true;
  }
  return false;
}

void endMatch(Room& room, uint64_t nowMs, int winnerTeam) noexcept {
  if (room.phase == MatchPhase::kEnded) return;
  if (!applyMatchTransition(room, MatchPhase::kEnded)) return;
  room.match.winnerTeam = static_cast<uint8_t>(winnerTeam == 1 ? 1u : 0u);
  room.match.endedAtMs = nowMs;
  // §5.6 的 matchEnded 事件：已有则就地更新，否则追加。
  // 计划文本的 flags = durationMs 在 v2 不可表达（Event.flags 是 u8）：时长只留在
  // room.match.startedAtMs / endedAtMs 上，由 S12 组装事件载荷时读取。
  if (room.world != nullptr) {
    ac::sim::World& world = *room.world;
    bool found = false;
    for (std::size_t i = world.eventCount; i > 0u; --i) {
      ac::sim::Event& event = world.events[i - 1u];
      if (event.type != ac::sim::kEventMatchEnded) continue;
      event.subjectId = room.match.winnerTeam;
      event.value = static_cast<double>(room.wave);
      found = true;
      break;
    }
    if (!found) {
      ac::sim::pushEvent(world, ac::sim::kEventMatchEnded, 0u, room.match.winnerTeam, 0u, 0.0, 0.0,
                         0.0, static_cast<double>(room.wave));
    }
  }
  room.match.counters.matchesEnded += 1u;
  room.match.lastResult = buildMatchResult(room);
  room.match.hasResult = true;
}

MatchResultRecord buildMatchResult(const Room& room) noexcept {
  const MatchRuntime& runtime = room.match;
  MatchResultRecord record{};
  writeMatchId(record.matchId, sizeof(record.matchId), room.code, runtime.startedAtMs);
  record.startedAtMs = runtime.startedAtMs;
  record.durationMs =
      runtime.endedAtMs > runtime.startedAtMs ? runtime.endedAtMs - runtime.startedAtMs : 0u;
  record.waveReached = room.wave;
  record.winnerTeam = runtime.winnerTeam;
  record.playerCount = runtime.recordCount;
  for (uint8_t i = 0u; i < runtime.recordCount && i < kMaxPlayersPerRoom; ++i) {
    const PlayerRecord& source = runtime.records[i];
    PlayerResult& target = record.players[i];
    std::memcpy(target.name, source.name, kNameBufferBytes);
    target.nameBytes = source.nameBytes;
    target.kills = source.stats.kills;
    target.headshots = source.stats.headshots;
    target.shotsFired = source.stats.shotsFired;
    target.hits = source.stats.hits;
    target.revives = source.stats.revives;
    target.downs = source.stats.downs;
    target.aliveMs = source.stats.aliveMs;
    target.leftMidMatch = source.leftMidMatch;
  }
  return record;
}

void refillPlayers(Room& room) noexcept {
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    if (entity == nullptr) continue;
    entity->hp = entity->maxHp;
    entity->armor = ac::config::kMaxArmor;
    for (int32_t slot = 0; slot < ac::config::kWeaponSlotCount; ++slot) {
      entity->weapon.magInSlot[slot] = ac::config::kWeapons[slot].mag;
    }
    entity->weapon.reserveAmmo = ac::config::kReserveAmmoInitial;
    entity->weapon.reloadEndsAtMs = 0.0;
    entity->weapon.spreadDeg = 0.0;  // §5.2 明写「散布清零」（v1 的全体补给漏了这一项）
    ac::combat::resetDownedState(entity->downed);
    ac::combat::resetRageState(entity->rage);
  }
}

void joinMatchRecord(Room& room, uint32_t pid, const char* name, uint8_t nameBytes) noexcept {
  if (pid == 0u) return;
  PlayerRecord* existing = playerRecordFor(room, pid);
  if (existing == nullptr) {
    if (room.match.recordCount >= kMaxPlayersPerRoom) return;
    existing = &room.match.records[room.match.recordCount];
    *existing = PlayerRecord{};
    existing->pid = pid;
    room.match.recordCount += 1u;
  }
  const std::size_t length = nameBytes < ac::net::kNameMaxBytes ? nameBytes : ac::net::kNameMaxBytes;
  std::memcpy(existing->name, name, length);
  existing->name[length] = '\0';
  existing->nameBytes = static_cast<uint8_t>(length);
}

PlayerRecord* playerRecordFor(Room& room, uint32_t pid) noexcept {
  if (pid == 0u) return nullptr;
  if (room.world == nullptr || pid > ac::sim::kMaxEntities) return nullptr;
  if (room.world->entities[pid - 1u].kind != ac::sim::EntityKind::kPlayer) return nullptr;
  for (uint8_t i = 0u; i < room.match.recordCount; ++i) {
    if (room.match.records[i].pid == pid) return &room.match.records[i];
  }
  return nullptr;
}

void captureShotBaseline(Room& room) noexcept {
  for (uint8_t i = 0u; i < kMaxPlayersPerRoom; ++i) {
    room.match.shotMagBefore[i] = -1;
    room.match.shotSlotBefore[i] = -1;
  }
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    const ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    if (entity == nullptr) continue;
    const uint8_t slot = entity->weapon.activeSlot;
    if (slot >= ac::config::kWeaponSlotCount) continue;
    room.match.shotMagBefore[i] = entity->weapon.magInSlot[slot];
    room.match.shotSlotBefore[i] = static_cast<int32_t>(slot);
  }
}

void applyShotDeltas(Room& room) noexcept {
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    const int32_t before = room.match.shotMagBefore[i];
    const int32_t slotBefore = room.match.shotSlotBefore[i];
    if (before < 0) continue;
    const ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    if (entity == nullptr) continue;
    if (slotBefore != static_cast<int32_t>(entity->weapon.activeSlot)) continue;
    const int32_t after = entity->weapon.magInSlot[entity->weapon.activeSlot];
    if (after >= before) continue;
    PlayerRecord* const record = playerRecordFor(room, session->pid);
    if (record == nullptr) continue;
    for (int32_t fired = before - after; fired > 0; --fired) noteShot(record->stats);
  }
}

void accumulateMatchEvents(Room& room) noexcept {
  if (room.world == nullptr) return;
  const ac::sim::World& world = *room.world;
  for (std::size_t i = 0u; i < world.eventCount; ++i) {
    const ac::sim::Event& event = world.events[i];
    switch (event.type) {
      case ac::sim::kEventPlayerHit: {
        PlayerRecord* const record = playerRecordFor(room, event.subjectId);
        if (record != nullptr) noteHit(record->stats);
        break;
      }
      case ac::sim::kEventSheepKilled: {
        PlayerRecord* const record = playerRecordFor(room, event.subjectId);
        if (record != nullptr) {
          noteKill(record->stats, (event.flags & ac::config::kHitFlagHeadshot) != 0u);
        }
        break;
      }
      case ac::sim::kEventPlayerDowned: {
        const uint32_t victim = event.targetId > 0u ? event.targetId : event.subjectId;
        PlayerRecord* const record = playerRecordFor(room, victim);
        if (record != nullptr) noteDown(record->stats);
        break;
      }
      case ac::sim::kEventReviveDone: {
        PlayerRecord* const record = playerRecordFor(room, event.subjectId);
        if (record != nullptr) noteRevive(record->stats);
        break;
      }
      default:
        break;
    }
  }
}

void accumulateMatchTime(Room& room, uint32_t dtMs) noexcept {
  if (room.phase != MatchPhase::kPlaying && room.phase != MatchPhase::kIntermission) return;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    PlayerRecord* const record = playerRecordFor(room, session->pid);
    if (record == nullptr) continue;
    const ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    if (entity == nullptr) continue;
    if (entity->downed.downed) {
      accumulateDowned(record->stats, dtMs);
    } else {
      accumulateAlive(record->stats, dtMs);
    }
  }
}

uint32_t activePlayerCount(const Room& room) noexcept {
  uint32_t count = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    if (!isConnected(*session)) continue;
    count += 1u;
  }
  return count > 0u ? count : 1u;
}

uint32_t matchStateSignature(const Room& room) noexcept {
  uint32_t signature = static_cast<uint32_t>(room.phase) & ((1u << kPhaseBits) - 1u);
  signature |= (static_cast<uint32_t>(room.sessionCount) & ((1u << kCountBits) - 1u)) << kPhaseBits;
  uint32_t readyMask = 0u;
  uint32_t downedMask = 0u;
  uint32_t fingerprint = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr) continue;
    if (session->ready) readyMask |= (1u << i);
    fingerprint = (fingerprint * 37u + session->pid) & ((1u << kFingerprintBits) - 1u);
    const ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    if (entity != nullptr && entity->downed.downed) downedMask |= (1u << i);
  }
  const uint32_t maskMax = (1u << kMaskBits) - 1u;
  signature |= (readyMask & maskMax) << (kPhaseBits + kCountBits);
  signature |= (downedMask & maskMax) << (kPhaseBits + kCountBits + kMaskBits);
  signature |= fingerprint << (kPhaseBits + kCountBits + 2u * kMaskBits);
  signature |= (static_cast<uint32_t>(room.wave) & ((1u << kWaveBits) - 1u))
               << (kPhaseBits + kCountBits + 2u * kMaskBits + kFingerprintBits);
  return signature;
}

}  // namespace ac::room
