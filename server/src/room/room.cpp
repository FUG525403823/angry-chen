#include "room/room.hpp"

#include "room/event_map.hpp"

#include <cstring>

#include "combat/downed.hpp"
#include "combat/rage.hpp"
#include "combat/resolve.hpp"
#include "combat/weapon.hpp"
#include "config/player.hpp"
#include "config/upgrades.hpp"
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
// S16：返回「本 tick 是否清波进入波间」——roomTick 据此立即广播 MatchState（升级点/ready 即时可见）。
bool driveDirector(Room& room, uint64_t nowMs) noexcept {
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
    return true;
  }
  checkMatchEnd(room, nowMs);
  return false;
}

double hpRatioOf(const ac::sim::Entity& entity) noexcept {
  return entity.maxHp > 0.0 ? entity.hp / entity.maxHp : 0.0;
}

// §5.7-5 + S03 §5.4：本 tick 的事件条目（sim::Event → net::EventEntry）。
// 新事件先追加进**未确认队列**，本代帧取队首 ≤ kMaxEventsPerFrame 条（**不出队**）：队列只在复制侧
// 确认「帧真的发出去了」（confirmFrameEvents）后才按重传窗口出队。被跳过的 tick 因此不清队列。
// 队列也满时事件才真丢（下一 tick 的 stepWorld 会清事件缓冲），记进 eventOverflowCount。
void stageFrameEvents(Room& room) noexcept {
  ac::sim::World& world = *room.world;
  const uint16_t before = room.pendingEventCount;
  const std::size_t appended = projectRoomEvents(world, room.pendingEvents, before,
                                                 Room::kPendingEventCapacity, room.nextEventId);
  for (std::size_t i = before; i < static_cast<std::size_t>(before) + appended; ++i) {
    room.pendingEventPasses[i] = Room::kEventSendPasses;
  }
  room.pendingEventCount = static_cast<uint16_t>(before + appended);
  if (world.eventCursor < world.eventCount) {
    room.eventOverflowCount += static_cast<uint32_t>(world.eventCount - world.eventCursor);
    world.eventCursor = world.eventCount;  // 丢就要一次计清，不留到下一 tick 重复计
  }
  std::size_t frameCount = room.pendingEventCount;
  if (frameCount > ac::net::kMaxEventsPerFrame) frameCount = ac::net::kMaxEventsPerFrame;
  for (std::size_t i = 0u; i < frameCount; ++i) room.eventEntries[i] = room.pendingEvents[i];
  room.eventEntryCount = static_cast<uint8_t>(frameCount);
  room.eventFrameGeneration += 1u;  // 本代帧的世代戳（确认侧按它去重）
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

// S16：loading→playing 时生成弹药补给箱（kind=pickup，id 在玩家之后，pid==EntityId 不变）。
void spawnAmmoCrates(Room& room) noexcept {
  if (room.world == nullptr) return;
  ac::sim::World& world = *room.world;
  room.ammoCrateCount = 0u;
  for (int32_t i = 0; i < ac::config::kAmmoCrateCount; ++i) {
    const ac::Vec3 pos = ac::config::kAmmoCratePositions[static_cast<std::size_t>(i)];
    const ac::sim::SpawnResult spawned = ac::sim::spawnEntity(world, ac::sim::EntityKind::kPickup, pos);
    if (!spawned.isOk) continue;  // 实体池满：跳过，不影响开局
    room.ammoCrateIds[room.ammoCrateCount] = spawned.id;
    room.ammoCrateCount += 1u;
  }
}

// S16：resetMatchForRestart 回收补给箱（按实际生成数）。
void despawnAmmoCrates(Room& room) noexcept {
  if (room.world == nullptr) return;
  for (uint8_t i = 0u; i < room.ammoCrateCount; ++i) {
    ac::sim::despawnEntity(*room.world, room.ammoCrateIds[i]);
  }
  room.ammoCrateCount = 0u;
}

// §5.3 可靠事件通道的确认侧（S03 §5.3「超出部分走 EventChannel 可靠通道补发」的房间侧落地）：
// 复制侧把本代帧**真的发出去**之后调用一次，sentCount = 该帧带出的条目数。被跳过的 tick（档位降档、
// 背压、编码失败、慢客户端）不调用 ⇒ 条目留在未确认队列里，下一帧继续下发（客户端按 eventId 去重）。
// 同一代只确认一次：同 tick 多客户端各发一帧时不重复扣重传窗口。
void confirmFrameEvents(Room& room, uint32_t frameGeneration, std::size_t sentCount) noexcept {
  if (frameGeneration == 0u || room.eventConfirmedGeneration == frameGeneration) return;
  const std::size_t limit =
      sentCount < static_cast<std::size_t>(room.eventEntryCount)
          ? sentCount
          : static_cast<std::size_t>(room.eventEntryCount);
  room.eventConfirmedGeneration = frameGeneration;
  for (std::size_t i = 0u; i < limit; ++i) {
    if (room.pendingEventPasses[i] > 0u) room.pendingEventPasses[i] -= 1u;
  }
  // 队首用满重传窗口的条目出队（FIFO：后面的条目与窗口计数一起前移）。
  std::size_t delivered = 0u;
  while (delivered < room.pendingEventCount && room.pendingEventPasses[delivered] == 0u) {
    ++delivered;
  }
  if (delivered == 0u) return;
  const std::size_t remaining = static_cast<std::size_t>(room.pendingEventCount) - delivered;
  for (std::size_t i = 0u; i < remaining; ++i) {
    room.pendingEvents[i] = room.pendingEvents[i + delivered];
    room.pendingEventPasses[i] = room.pendingEventPasses[i + delivered];
  }
  room.pendingEventCount = static_cast<uint16_t>(remaining);
}

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
  if (&incoming != &existing) {
    incoming.pid = existing.pid;
    std::memcpy(incoming.name, existing.name, kNameBufferBytes);
    incoming.nameBytes = existing.nameBytes;
    incoming.token = existing.token;
    incoming.ready = existing.ready;
    incoming.weapon = existing.weapon;
    incoming.weaponApplied = existing.weaponApplied;
    incoming.kills = existing.kills;
    incoming.joinedAtMs = existing.joinedAtMs;
    existing.pid = 0u;
    existing.roomCode[0] = '\0';
    existing.disconnectedAtMs = -1;
  }
  std::memcpy(incoming.roomCode, room.code, sizeof(incoming.roomCode));
  incoming.disconnectedAtMs = -1;
  incoming.command = ac::sim::Command{};
  room.sessions[index] = &incoming;
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
    // ADR-009「kills 字段来源」：HUD 读数是真实击杀累加器 PlayerStats::kills（noteKill 累加），
    // 不是 v1 遗留、永不累加的 Session::kills（后者只在加入时置 0、重连时照抄）。
    const PlayerRecord* const record = playerRecordFor(room, session->pid);
    const uint32_t kills = record != nullptr ? record->stats.kills : session->kills;
    player.kills = static_cast<uint16_t>(kills > 0xffffu ? 0xffffu : kills);
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
    // S16：波次升级（点在实体上，断线重连不丢）。
    player.upgradePoints = alive ? entity->upgrade.points : 0u;
    player.upgradeDamage = alive ? entity->upgrade.damageLevel : 0u;
    player.upgradeSpeed = alive ? entity->upgrade.speedLevel : 0u;
    player.upgradeReload = alive ? entity->upgrade.reloadLevel : 0u;
    player.upgradeReserve = alive ? entity->upgrade.reserveLevel : 0u;
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
  // 实体峰值（S13 §5 报告的 peak.entities 来源）：本 tick 世界里的活动实体数（玩家/羊/投射物/拾取物）。
  if (room.world->activeCount > room.match.counters.peakEntities) {
    room.match.counters.peakEntities = room.world->activeCount;
  }
  // §5.7-2：阶段推进（loading / intermission 计时 + 全员准备跳过）。
  updateMatch(room, nowMs, ac::config::kStepDtMs);
  applyWeaponSelections(room);
  buildCommands(room);
  captureShotBaseline(room);
  bool waveCleared = false;
  if (room.phase == MatchPhase::kPlaying) {
    // S06 冻结的 stepWorld 是四参数（不带 CombatContext）⇒ S08 的回滚上下文此刻仍整体关闭；
    // world.poseHistory 由 stepWorld 自己每 tick 记录，接上上下文的活儿留给后续相位。
    ac::sim::stepWorld(*room.world, room.commands, room.commandCount, ac::config::kStepDtMs);
    accumulateMatchEvents(room);
    applyShotDeltas(room);
    accumulateMatchTime(room, ac::config::kStepDtMs);
    waveCleared = driveDirector(room, nowMs);
  } else if (room.phase == MatchPhase::kIntermission) {
    accumulateMatchTime(room, ac::config::kStepDtMs);
  }
  // S16：清波进入波间的那一拍立即广播 MatchState（升级点与 ready 重置即时可见，不等 1Hz 节拍）。
  if (waveCleared) broadcastMatchState(room, deps);
  // §5.7-5：快照与事件广播（S12 的复制流水线）——先把本 tick 的事件条目舞台化，再交给复制回调。
  stageFrameEvents(room);
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
