#include "room/room.hpp"

#include <cstring>

#include "combat/downed.hpp"
#include "combat/rage.hpp"
#include "combat/resolve.hpp"
#include "combat/weapon.hpp"
#include "config/player.hpp"
#include "config/weapons.hpp"
#include "core/quantize.hpp"
#include "room/rooms.hpp"
#include "sim/arena.hpp"
#include "sim/entity_table.hpp"

namespace ac::room {
namespace {

// §5.8：客户端的「主机」= pid 最小者；服务端的 hostId 与它保持一致（v1 只在首次加入时赋值，
// 主机会话离开后无人能开局 —— 这里改为每次成员变化后重算最小 pid）。
void refreshHostId(Room& room) noexcept {
  uint32_t best = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session == nullptr || session->pid == 0u) continue;
    if (best == 0u || session->pid < best) best = session->pid;
  }
  room.match.hostId = best;
}

// §5.5：无会话或全部会话都已断线 = 空闲；空闲起点只记一次。
void refreshEmptySince(Room& room, uint64_t nowMs) noexcept {
  if (roomIsIdle(room)) {
    if (room.emptySinceMs < 0) room.emptySinceMs = static_cast<int64_t>(nowMs);
  } else {
    room.emptySinceMs = -1;
  }
}

// §5.5：宽限 30 秒到期仍未回来 → 移除会话、释放名额、计入 graceTimeouts。
void expireGraceSessions(Room& room, uint64_t nowMs) noexcept {
  for (int32_t i = static_cast<int32_t>(room.sessionCount) - 1; i >= 0; --i) {
    Session* const session = room.sessions[i];
    if (session == nullptr || isConnected(*session)) continue;
    const uint64_t disconnectedAt = static_cast<uint64_t>(session->disconnectedAtMs);
    if (nowMs < disconnectedAt || nowMs - disconnectedAt < static_cast<uint64_t>(kGracePeriodMs)) {
      continue;
    }
    room.match.counters.graceTimeouts += 1u;
    removeMember(room, *session, nowMs, true);
  }
}

// v1 runTick：把会话选中的枪落在实体上（每次只落一次），并同步开火时钟。
void applyWeaponSelections(Room& room) noexcept {
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    Session* const session = room.sessions[i];
    if (session == nullptr || session->weaponApplied) continue;
    ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    if (entity == nullptr) continue;
    const uint8_t slot = session->weapon == 1u ? 1u : (session->weapon == 2u ? 2u : 0u);
    entity->weapon.activeSlot = slot;
    entity->weapon.reloadEndsAtMs = 0.0;
    entity->weapon.nextFireAllowedAtMs = static_cast<double>(room.world->timeMs);
    session->weaponApplied = true;
  }
}

// v1 collectDirectorPlayerIds：已连接、有 pid、实体活动且非 idle 的玩家，升序。
uint32_t collectDirectorPlayerIds(Room& room, uint16_t* out, uint32_t capacity) noexcept {
  uint32_t count = 0u;
  const ac::sim::World& world = *room.world;
  for (std::size_t i = 0u; i < world.activeCount && count < capacity; ++i) {
    const uint16_t id = world.activeIds[i];
    const ac::sim::Entity& entity = world.entities[id - 1u];
    if (!entity.active || entity.kind != ac::sim::EntityKind::kPlayer || entity.idle) continue;
    const Session* const session = sessionByPid(room, id);
    if (session == nullptr || !isConnected(*session)) continue;
    out[count++] = id;
  }
  return count;
}

// §5.7-3：组队 → 每 tick 生成/清波判定；清波走 handleWaveCleared，否则查结束判定。
void driveDirector(Room& room, uint64_t nowMs) noexcept {
  ac::sim::World& world = *room.world;
  if (room.director.wave != room.wave && !room.director.isFinished) {
    ac::waves::planWave(room.director, room.wave, static_cast<int32_t>(activePlayerCount(room)));
  }
  uint16_t ids[kMaxPlayersPerRoom] = {};
  const uint32_t idCount =
      collectDirectorPlayerIds(room, ids, static_cast<uint32_t>(kMaxPlayersPerRoom));
  const ac::waves::DirectorTick tick =
      ac::waves::updateDirector(world, room.director, static_cast<int32_t>(activePlayerCount(room)),
                                world.rng.spawn, ids, idCount);
  if (tick.isWaveClear) {
    handleWaveCleared(room, nowMs);
    return;
  }
  checkMatchEnd(room, nowMs);
}

double hpRatioOf(const ac::sim::Entity& entity) noexcept {
  return entity.maxHp > 0.0 ? entity.hp / entity.maxHp : 0.0;
}

uint8_t clampToU8(double value) noexcept {
  if (!(value > 0.0)) return 0u;
  return value >= 255.0 ? 255u : static_cast<uint8_t>(value);
}

uint16_t clampToU16(double value) noexcept {
  if (!(value > 0.0)) return 0u;
  return value >= 65535.0 ? 65535u : static_cast<uint16_t>(value);
}

// §5.8：以 10ms / 100ms 为单位向下取整（计划文本；v1 用的是四舍五入）。
uint8_t floorUnits(double value, double unitsPerValue) noexcept {
  const double floor = value > 0.0 ? static_cast<double>(static_cast<int64_t>(value)) : 0.0;
  return clampToU8(floor / unitsPerValue);
}

uint8_t quantizePercent(double ratio) noexcept {
  if (!(ratio > 0.0)) return 0u;
  return ratio >= 1.0 ? 100u : static_cast<uint8_t>(ratio * 100.0 + 0.5);
}

}  // namespace

const char* joinOutcomeName(JoinOutcome outcome) noexcept {
  switch (outcome) {
    case JoinOutcome::kOk:
      return "ok";
    case JoinOutcome::kRoomNotFound:
      return "room-not-found";
    case JoinOutcome::kRoomFull:
      return "room-full";
    case JoinOutcome::kMatchInProgress:
      return "match-in-progress";
  }
  return "unknown";
}

bool roomIsIdle(const Room& room) noexcept {
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session != nullptr && session->pid > 0u && isConnected(*session)) return false;
  }
  return true;
}

uint8_t connectedSessionCount(const Room& room) noexcept {
  uint8_t count = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const Session* const session = room.sessions[i];
    if (session != nullptr && session->pid > 0u && isConnected(*session)) count += 1u;
  }
  return count;
}

Session* sessionByPid(Room& room, uint32_t pid) noexcept {
  if (pid == 0u) return nullptr;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    Session* const session = room.sessions[i];
    if (session != nullptr && session->pid == pid) return session;
  }
  return nullptr;
}

JoinOutcome roomJoin(Room& room, Session& session, uint64_t nowMs) noexcept {
  // §5.4：playing 阶段不接受新身份（带令牌的宽限重连走 roomReconnect）。
  if (room.phase == MatchPhase::kPlaying) return JoinOutcome::kMatchInProgress;
  // §5.8 要求 MatchState 的 name 为 1–12 字节；净化在握手层（S12）完成，这里兜底，
  // 保证「任何时刻组装的 MatchState 都可编码」这条不变量不被上层漏判破坏。
  if (session.nameBytes < ac::net::kNameMinBytes) {
    (void)setSessionName(session, "player");
  }
  if (room.world == nullptr || room.sessionCount >= room.capacity) return JoinOutcome::kRoomFull;
  const std::size_t spawnIndex = static_cast<std::size_t>(room.sessionCount) % ac::sim::arena::kPlayerSpawns.size();
  const ac::Vec3 spawn = ac::sim::arena::kPlayerSpawns[spawnIndex];
  const ac::sim::SpawnResult spawned =
      ac::sim::spawnEntity(*room.world, ac::sim::EntityKind::kPlayer, spawn);
  if (!spawned.isOk) return JoinOutcome::kRoomFull;
  session.pid = spawned.id;
  session.token = 0u;  // O08：新玩家一律丢弃连线带来的令牌
  std::memcpy(session.roomCode, room.code, sizeof(session.roomCode));
  session.ready = false;
  session.weapon = 0u;
  session.weaponApplied = false;
  session.kills = 0u;
  session.disconnectedAtMs = -1;
  session.joinedAtMs = nowMs;
  session.command = ac::sim::Command{};
  room.sessions[room.sessionCount] = &session;
  room.sessionCount += 1u;
  refreshHostId(room);
  joinMatchRecord(room, session.pid, session.name, session.nameBytes);
  room.emptySinceMs = -1;
  return JoinOutcome::kOk;
}

bool roomReconnect(Room& room, Session& existing, Session& incoming) noexcept {
  int32_t index = -1;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    if (room.sessions[i] == &existing) index = static_cast<int32_t>(i);
  }
  if (index < 0) return false;
  // §5.5：只按令牌匹配（调用方已完成），昵称不参与身份判定：沿用旧会话的昵称与令牌。
  incoming.pid = existing.pid;
  std::memcpy(incoming.name, existing.name, kNameBufferBytes);
  incoming.nameBytes = existing.nameBytes;
  incoming.token = existing.token;
  incoming.ready = existing.ready;
  incoming.weapon = existing.weapon;
  incoming.weaponApplied = existing.weaponApplied;
  incoming.kills = existing.kills;
  std::memcpy(incoming.roomCode, room.code, sizeof(incoming.roomCode));
  incoming.disconnectedAtMs = -1;
  incoming.joinedAtMs = existing.joinedAtMs;
  incoming.command = ac::sim::Command{};
  room.sessions[index] = &incoming;
  existing.pid = 0u;
  existing.roomCode[0] = '\0';
  existing.disconnectedAtMs = -1;
  // §5.5 重连补偿：解除 idle、水平速度清零、倒地解除、生命下限 maxHp * 0.5。
  ac::sim::Entity* const entity = playerEntityAt(room, incoming.pid);
  if (entity != nullptr) {
    entity->idle = false;
    entity->vel.x = 0.0;
    entity->vel.z = 0.0;
    ac::combat::resetDownedState(entity->downed);
    const double minimum = entity->maxHp * kReconnectMinHpRatio;
    if (entity->hp < minimum) entity->hp = minimum;
  }
  room.match.counters.graceReconnects += 1u;
  refreshHostId(room);
  return true;
}

bool roomDisconnect(Room& room, Session& session, uint64_t nowMs) noexcept {
  if (!isConnected(session)) return false;
  session.disconnectedAtMs = static_cast<int64_t>(nowMs);
  session.command = ac::sim::Command{};
  ac::sim::Entity* const entity = playerEntityAt(room, session.pid);
  if (entity != nullptr) {
    entity->idle = true;
    entity->vel.x = 0.0;
    entity->vel.z = 0.0;
  }
  refreshEmptySince(room, nowMs);
  return true;
}

bool roomLeave(Room& room, Session& session, uint64_t nowMs) noexcept {
  return removeMember(room, session, nowMs, true);
}

bool removeMember(Room& room, Session& session, uint64_t nowMs, bool leftMidMatch) noexcept {
  int32_t index = -1;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    if (room.sessions[i] == &session) index = static_cast<int32_t>(i);
  }
  if (index < 0) return false;
  const uint32_t pid = session.pid;
  const bool wasMidMatch = room.phase == MatchPhase::kPlaying || room.phase == MatchPhase::kIntermission;
  PlayerRecord* const record = playerRecordFor(room, pid);
  if (record != nullptr && leftMidMatch && wasMidMatch) record->leftMidMatch = true;
  if (room.world != nullptr && pid > 0u && pid <= ac::sim::kMaxEntities) {
    ac::sim::despawnEntity(*room.world, static_cast<uint16_t>(pid));
  }
  for (int32_t i = index; i + 1 < static_cast<int32_t>(room.sessionCount); ++i) {
    room.sessions[i] = room.sessions[i + 1];
  }
  room.sessionCount -= 1u;
  room.sessions[room.sessionCount] = nullptr;
  clearSessionRoom(session);
  refreshHostId(room);
  refreshEmptySince(room, nowMs);
  return true;
}

bool roomSetReady(Room& room, Session& session, bool ready, uint8_t weapon,
                  const RoomDeps& deps) noexcept {
  Session* const member = sessionByPid(room, session.pid);
  if (member == nullptr || member != &session) return false;
  const bool changed = member->ready != ready || (weapon <= 2u && member->weapon != weapon);
  member->ready = ready;
  if (weapon <= 2u) {
    member->weapon = weapon;
    member->weaponApplied = false;
  }
  if (changed) broadcastMatchState(room, deps);
  return changed;
}

bool roomApplyCommand(Room& room, Session& session, const ac::sim::Command& command) noexcept {
  Session* const member = sessionByPid(room, session.pid);
  if (member == nullptr || member != &session) return false;
  member->command = command;
  return true;
}

void buildCommands(Room& room) noexcept {
  room.commandCount = 0u;
  if (room.world == nullptr) return;
  const ac::sim::World& world = *room.world;
  for (std::size_t i = 0u; i < world.activeCount; ++i) {
    if (room.commandCount >= kMaxPlayersPerRoom) break;
    const uint16_t id = world.activeIds[i];
    const ac::sim::Entity& entity = world.entities[id - 1u];
    if (!entity.active || entity.kind != ac::sim::EntityKind::kPlayer) continue;
    ac::sim::Command& command = room.commands[room.commandCount];
    Session* const session = sessionByPid(room, id);
    if (session != nullptr) {
      command = session->command;
    } else {
      command = ac::sim::Command{};
      command.yaw = entity.yaw;
    }
    room.commandCount += 1u;
  }
}

std::size_t buildMatchState(Room& room) noexcept {
  ac::net::MatchState& state = room.matchState;
  state.phase = static_cast<uint8_t>(room.phase);
  state.wave = clampToU8(static_cast<double>(room.wave));
  if (room.phase == MatchPhase::kLoading) {
    state.intermissionMs = clampToU16(static_cast<double>(room.match.loadingMs));
  } else if (room.intermissionMs > 0) {
    state.intermissionMs = clampToU16(static_cast<double>(room.intermissionMs));
  } else {
    state.intermissionMs = 0u;
  }
  const double nowMs = room.world != nullptr ? static_cast<double>(room.world->timeMs) : 0.0;
  // 复用同一份 players（createRoom 里 reserve(4)），resize 到 <= 4 不会重新分配。
  if (state.players.size() != room.sessionCount) state.players.resize(room.sessionCount);
  std::size_t count = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    Session* const session = room.sessions[i];
    if (session == nullptr) continue;
    if (count >= state.players.size()) break;
    ac::net::MatchStatePlayer& player = state.players[count];
    const ac::sim::Entity* const entity = playerEntityAt(room, session->pid);
    const bool alive = entity != nullptr;
    if (alive && session->weaponApplied) session->weapon = entity->weapon.activeSlot;
    player.pid = static_cast<uint16_t>(session->pid & 0xffffu);
    player.name.assign(session->name, session->nameBytes);
    player.ready = session->ready ? 1u : 0u;
    player.weapon = alive && session->weaponApplied ? entity->weapon.activeSlot : session->weapon;
    player.hpRatio = alive ? ac::quantizeRatio(hpRatioOf(*entity)) : 0u;
    player.kills = static_cast<uint16_t>(session->kills > 0xffffu ? 0xffffu : session->kills);
    player.mag = alive ? clampToU8(static_cast<double>(entity->weapon.magInSlot[entity->weapon.activeSlot])) : 0u;
    player.reserve = alive ? clampToU16(static_cast<double>(entity->weapon.reserveAmmo)) : 0u;
    player.reloadLeft10Ms =
        alive ? floorUnits(ac::combat::reloadRemainingMs(entity->weapon, nowMs), 10.0) : 0u;
    player.rage = alive ? quantizePercent(ac::combat::rageRatio(entity->rage)) : 0u;
    player.rageLeft100Ms =
        alive ? floorUnits(ac::combat::rageSecondsLeft(entity->rage, nowMs) * 1000.0, 100.0) : 0u;
    player.downed = alive && entity->downed.downed ? 1u : 0u;
    player.reviveRatio255 =
        alive ? ac::quantizeRatio(ac::combat::reviveRatio(entity->downed)) : 0u;
    count += 1u;
  }
  if (state.players.size() > count) state.players.resize(count);
  return count;
}

bool broadcastMatchState(Room& room, const RoomDeps& deps) noexcept {
  room.stateSignature = matchStateSignature(room);
  room.hasStateSignature = true;
  if (room.sessionCount == 0u) return false;
  const std::size_t count = buildMatchState(room);
  if (count == 0u) return false;
  uint32_t sent = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    Session* const session = room.sessions[i];
    if (session == nullptr || !isConnected(*session)) continue;
    sent += 1u;
    if (deps.sendMatchState != nullptr) {
      deps.sendMatchState(deps.user, room, *session, room.matchState);
    }
  }
  room.unicastCount += sent;
  return sent > 0u;
}

bool roomTick(Room& room, const RoomDeps& deps, uint64_t nowMs) noexcept {
  if (room.world == nullptr) return false;
  room.match.counters.ticks += 1u;
  const uint8_t connected = connectedSessionCount(room);
  if (connected > room.match.counters.peakPlayers) room.match.counters.peakPlayers = connected;
  // §5.7-2：阶段推进（loading / intermission 计时 + 全员准备跳过）。
  updateMatch(room, nowMs, ac::config::kStepDtMs);
  applyWeaponSelections(room);
  buildCommands(room);
  captureShotBaseline(room);
  if (room.phase == MatchPhase::kPlaying) {
    // S06 冻结的 stepWorld 是四参数（不带 CombatContext）⇒ S08 的回滚上下文此刻仍整体关闭；
    // world.poseHistory 由 stepWorld 自己每 tick 记录，接上上下文的活儿留给后续相位。
    ac::sim::stepWorld(*room.world, room.commands, room.commandCount, ac::config::kStepDtMs);
    accumulateMatchEvents(room);
    applyShotDeltas(room);
    accumulateMatchTime(room, ac::config::kStepDtMs);
    driveDirector(room, nowMs);
  } else if (room.phase == MatchPhase::kIntermission) {
    accumulateMatchTime(room, ac::config::kStepDtMs);
  }
  // §5.7-5：快照与事件广播（S12 的复制流水线）。
  if (deps.replicate != nullptr) deps.replicate(deps.user, room);
  // §5.7-6：MatchState 节拍（每 tick 加 dtMs，>= 1000ms 减 1000 并单播一份）。
  room.matchStateTimerMs += static_cast<int64_t>(ac::config::kStepDtMs);
  while (room.matchStateTimerMs >= kMatchStateIntervalMs) {
    room.matchStateTimerMs -= kMatchStateIntervalMs;
    broadcastMatchState(room, deps);
  }
  return true;
}

bool updateRoom(Room& room, const RoomDeps& deps, uint64_t nowMs) noexcept {
  int64_t elapsed = static_cast<int64_t>(nowMs) - static_cast<int64_t>(room.lastUpdateMs);
  if (elapsed < 0) elapsed = 0;
  room.lastUpdateMs = nowMs;
  expireGraceSessions(room, nowMs);
  refreshEmptySince(room, nowMs);
  if (room.sessionCount == 0u) {
    if (room.phase == MatchPhase::kPlaying || room.phase == MatchPhase::kIntermission) {
      endMatch(room, nowMs, 1);
      broadcastMatchState(room, deps);
    }
    return false;
  }
  if (connectedSessionCount(room) == 0u) return false;
  room.accumulatorMs += elapsed;
  const int64_t stepMs = static_cast<int64_t>(ac::config::kStepDtMs);
  uint32_t steps = 0u;
  bool ticked = false;
  while (room.accumulatorMs >= stepMs) {
    if (steps >= kTickCatchUpLimit) {
      const int64_t skipped = room.accumulatorMs / stepMs;
      room.accumulatorMs -= skipped * stepMs;
      room.match.counters.skipped += static_cast<uint32_t>(skipped);
      break;
    }
    room.accumulatorMs -= stepMs;
    roomTick(room, deps, nowMs);
    ticked = true;
    steps += 1u;
  }
  // §5.8：签名变化（阶段 / 波次 / 成员 / 准备 / 倒地 / 复活）→ 立即补发一次。
  if (room.sessionCount > 0u) {
    const uint32_t signature = matchStateSignature(room);
    if (!room.hasStateSignature || signature != room.stateSignature) {
      broadcastMatchState(room, deps);
      room.immediateCount += 1u;
    }
  }
  return ticked;
}

}  // namespace ac::room
