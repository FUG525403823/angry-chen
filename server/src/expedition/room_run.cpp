#include "expedition/room_run.hpp"
#include <algorithm>
#include "expedition/build.hpp"
#include "room/room.hpp"
#include "sim/entity_table.hpp"
#include "sim/arena.hpp"
#include "waves/director.hpp"

namespace ac::room {
namespace {
void clearThreats(Room& room) noexcept {
  if (!room.world) return;
  for (std::size_t i = room.world->activeCount; i > 0u; --i) {
    const uint16_t id = room.world->activeIds[i - 1u];
    const auto kind = room.world->entities[id - 1u].kind;
    if (kind == ac::sim::EntityKind::kSheep || kind == ac::sim::EntityKind::kProjectile)
      ac::sim::despawnEntity(*room.world, id);
  }
  room.world->stats.aliveSheep = 0;
}
Room::ExpeditionReceipt* receiptFor(Room& room, uint16_t pid) noexcept {
  for (auto& receipt : room.expeditionReceipts) if (receipt.pid == pid) return &receipt;
  for (auto& receipt : room.expeditionReceipts) if (receipt.pid == 0u) {
    receipt.pid = pid;
    return &receipt;
  }
  return nullptr;
}
bool identical(const ac::net::ExpeditionRequest& a, const ac::net::ExpeditionRequest& b) noexcept {
  return a.runId == b.runId && a.offerId == b.offerId && a.requestId == b.requestId &&
         a.op == b.op && a.choiceId == b.choiceId && a.reserved == b.reserved;
}
void synchronizePlayers(Room& room) noexcept {
  for (uint8_t i = 0u; i < room.run.playerCount; ++i) {
    auto& player = room.run.players[i];
    auto* session = sessionByPid(room, player.pid);
    player.connected = session != nullptr && isConnected(*session);
    auto* entity = playerEntityAt(room, player.pid);
    if (!entity) continue;
    entity->build.enabled = true;
    entity->build.selectedMask = player.cardMask;
    if (player.supplyPending) {
      if (!entity->downed.downed && entity->hp > 0.0) entity->hp = std::min(entity->maxHp, entity->hp + 25.0);
      entity->weapon.reserveAmmo += 30;
      player.supplyPending = false;
    }
  }
}
void spawnPressure(Room& room) noexcept {
  uint16_t ids[kMaxPlayersPerRoom]{};
  uint32_t count = 0u;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    auto* session = room.sessions[i];
    if (session && isConnected(*session)) ids[count++] = static_cast<uint16_t>(session->pid);
  }
  const uint32_t budget = (4u + 4u * room.run.node) * activePlayerCount(room);
  int32_t points[8]{};
  const int32_t pointCount = ac::waves::selectSpawnPointIndices(*room.world, ids, count,
      room.world->rng.spawn, points, 8);
  if (pointCount == 0) return;
  for (uint32_t i = 0u; i < budget; ++i) {
    const auto& point = ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(points[i % static_cast<uint32_t>(pointCount)])];
    if (!ac::waves::spawnSheepAt(*room.world, ac::config::SheepKind::kGrunt, point.x, point.z)) break;
  }
}
}

bool sendExpeditionState(Room& room, Session& session, const RoomDeps& deps) noexcept {
  if (!deps.sendExpeditionState || sessionByPid(room, session.pid) != &session || !isConnected(session)) return false;
  ac::net::ExpeditionState state{};
  state.mode = room.expeditionEnabled ? 1u : 0u;
  const auto& run = room.run;
  state.runId = run.runId; state.revision = run.revision; state.clockMs = run.clockMs;
  state.stage = static_cast<uint8_t>(run.stage); state.node = run.node;
  state.result = static_cast<uint8_t>(run.result); state.objectiveHp = run.objectiveHp;
  state.progressMs = run.progressMs; state.combatLeftMs = run.combatLeftMs;
  state.rewardDeadlineMs = run.rewardDeadlineMs; state.localPid = static_cast<uint16_t>(session.pid);
  state.playerCount = room.sessionCount;
  for (uint8_t i = 0u; i < run.playerCount; ++i) {
    const auto& player = run.players[i];
    if (player.pid != session.pid) continue;
    if (run.stage == ac::expedition::Stage::kReward) {
      state.offerId = player.offerId; state.offerCount = 3u;
      for (uint8_t j = 0u; j < 3u; ++j) state.choices[j] = player.choices[j];
    }
    state.resolved = player.resolved ? 1u : 0u; state.rerollsLeft = player.rerollsLeft;
  }
  auto* receipt = receiptFor(room, static_cast<uint16_t>(session.pid));
  if (receipt) { state.lastRequestId = receipt->ackRequestId; state.lastResult = receipt->ackResult; }
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    const auto* member = room.sessions[i];
    if (!member) continue;
    auto& player = state.players[i]; player.pid = static_cast<uint16_t>(member->pid);
    const auto* entity = playerEntityAt(room, member->pid);
    if (!entity) continue;
    player.cardMask = entity->build.selectedMask;
    const uint32_t worldClock = static_cast<uint32_t>(room.world->timeMs);
    player.toolReadyAtMs = run.clockMs + (entity->build.pulseReadyAtMs > worldClock ? entity->build.pulseReadyAtMs - worldClock : 0u);
    player.triggerCount = entity->build.triggerCount;
  }
  deps.sendExpeditionState(deps.user, room, session, state);
  return true;
}
bool broadcastExpeditionState(Room& room, const RoomDeps& deps) noexcept {
  bool sent = false;
  for (uint8_t i = 0u; i < room.sessionCount; ++i)
    if (room.sessions[i]) sent = sendExpeditionState(room, *room.sessions[i], deps) || sent;
  room.expeditionSentRevision = room.run.revision;
  return sent;
}

bool roomExpeditionRequest(Room& room, Session& session, const ac::net::ExpeditionRequest& request,
                           const RoomDeps& deps) noexcept {
  if (sessionByPid(room, session.pid) != &session || !isConnected(session)) return false;
  auto* receipt = receiptFor(room, static_cast<uint16_t>(session.pid));
  if (!receipt) return false;
  // Accepted restart invalidates its old run immediately; its exact retry remains an acknowledgement,
  // not permission to restart the new run a second time.
  if (request.op == 7u && request.requestId != 0u && receipt->result == 1u &&
      identical(request, receipt->request)) {
    receipt->ackRequestId = request.requestId; receipt->ackResult = 1u;
    sendExpeditionState(room, session, deps); return true;
  }
  const bool lobbyConfiguration = request.op == 1u || request.op == 6u;
  const uint32_t expectedRunId = lobbyConfiguration ? 0u : room.run.runId;
  if (request.runId != expectedRunId) {
    // A foreign run can receive a failure projection but must not mutate this run's cache/watermark.
    receipt->ackRequestId = request.requestId; receipt->ackResult = 3u;
    ++room.run.revision; sendExpeditionState(room, session, deps); return false;
  }
  if (request.requestId != 0u && receipt->toolRequest.requestId == request.requestId &&
      identical(request, receipt->toolRequest) && request.runId == room.run.runId) {
    if (!receipt->queued) {
      receipt->ackRequestId = request.requestId; receipt->ackResult = receipt->toolResult;
    }
    sendExpeditionState(room, session, deps);
    return receipt->queued || receipt->toolResult == 1u;
  }
  if (request.requestId == receipt->request.requestId && request.requestId != 0u && identical(request, receipt->request)) {
    const bool pendingReceipt = receipt->queued && identical(receipt->request, receipt->toolRequest);
    receipt->ackRequestId = pendingReceipt ? receipt->ackRequestId : receipt->request.requestId;
    receipt->ackResult = pendingReceipt ? receipt->ackResult : receipt->result;
    sendExpeditionState(room, session, deps); return receipt->result == 1u;
  }
  const uint32_t scopeRunId = room.run.runId;
  if (receipt->watermarkRunId != scopeRunId) {
    receipt->watermarkRunId = scopeRunId; receipt->highRequestId = 0u;
  }
  if (receipt->queued) {
    // One outstanding tool per player. Cache a newer rejection without replacing the queued action.
    const bool newer = request.requestId > receipt->highRequestId;
    if (newer) {
      receipt->highRequestId = request.requestId;
      receipt->request = request; receipt->result = 8u;
    }
    receipt->ackRequestId = request.requestId; receipt->ackResult = newer ? 8u : 7u;
    ++room.run.revision; sendExpeditionState(room, session, deps); return false;
  }
  if (request.requestId == 0u || request.requestId <= receipt->highRequestId) {
    receipt->ackRequestId = request.requestId; receipt->ackResult = 7u;
    ++room.run.revision; sendExpeditionState(room, session, deps); return false;
  }
  receipt->highRequestId = request.requestId;
  uint8_t result = 1u;
  if (request.reserved != 0u || request.op < 1u || request.op > 7u) result = 6u;
  else if (request.op == 1u || request.op == 6u || request.op == 7u) {
    if (session.pid != room.match.hostId) result = 9u;
    else if (request.choiceId != 0u || request.offerId != 0u) result = 6u;
    else if (request.op != 7u && request.runId != 0u) result = 3u;
    else if (request.op == 7u && request.runId != room.run.runId) result = 3u;
    else if ((request.op == 7u && room.phase != MatchPhase::kEnded) ||
             (request.op != 7u && room.phase != MatchPhase::kLobby)) result = 4u;
    else if (request.op == 7u) {
      resetMatchForRestart(room);
      for (uint8_t i = 0u; i < room.sessionCount; ++i) if (room.sessions[i]) room.sessions[i]->ready = true;
      (void)tryStartMatch(room, session);
    } else { room.expeditionEnabled = request.op == 1u; ++room.run.revision; }
  } else if (!room.expeditionEnabled) result = 2u;
  else if (request.runId != room.run.runId) result = 3u;
  else if (request.op == 5u) {
    if (room.phase != MatchPhase::kPlaying) result = 4u;
    else if (request.offerId != 0u) result = 5u;
    else if (request.choiceId != 1u) result = 6u;
    else if (room.expeditionToolCount >= 16u) result = 8u;
    else {
      room.expeditionToolQueue[room.expeditionToolCount] = static_cast<uint16_t>(session.pid);
      room.expeditionToolRequestIds[room.expeditionToolCount++] = request.requestId;
      receipt->queued = true;
      receipt->toolRequest = request; receipt->toolResult = 0u;
    }
  } else {
    result = ac::expedition::request(room.run, static_cast<uint16_t>(session.pid), request.runId,
        request.offerId, request.requestId, request.op, request.choiceId);
    synchronizePlayers(room);
  }
  // A restart clears receipt storage; reacquire it before writing the acknowledgement.
  receipt = receiptFor(room, static_cast<uint16_t>(session.pid));
  if (receipt) {
    receipt->request = request; receipt->result = result;
    if (!receipt->queued) { receipt->ackRequestId = request.requestId; receipt->ackResult = result; }
  }
  ++room.run.revision;
  broadcastExpeditionState(room, deps);
  return result == 1u;
}
void resetRoomExpedition(Room& room) noexcept {
  room.run = {}; room.encounter = {}; room.pressureTimerMs = 0u;
  room.expeditionToolCount = 0u; room.expeditionTimerMs = 0u;
  room.expeditionTriggerSignature = 0u; room.expeditionSentRevision = 0u;
  for (auto& receipt : room.expeditionReceipts) receipt = {};
  if (room.world) for (std::size_t i = 0u; i < room.world->activeCount; ++i)
    ac::expedition::resetBuild(room.world->entities[room.world->activeIds[i] - 1u].build);
}
void beginRoomExpedition(Room& room) noexcept {
  uint16_t ids[kMaxPlayersPerRoom]{};
  for (uint8_t i = 0u; i < room.sessionCount; ++i) ids[i] = static_cast<uint16_t>(room.sessions[i]->pid);
  ac::expedition::beginRun(room.run, room.nextRunId++, room.seed, ids, room.sessionCount);
  clearThreats(room); synchronizePlayers(room);
  spawnAmmoCrates(room);  // Keep the existing map supply for the whole run, not per encounter.
  (void)applyMatchTransition(room, MatchPhase::kIntermission);
  room.intermissionMs = 30000;
}
void tickRoomExpedition(Room& room, uint32_t dtMs) noexcept {
  if (!room.run.enabled) return;
  synchronizePlayers(room);
  const auto previous = room.run.stage;
  ac::expedition::tickRun(room.run, dtMs);
  synchronizePlayers(room);
  if (previous == ac::expedition::Stage::kReward && room.run.stage == ac::expedition::Stage::kCombat) {
    ac::expedition::beginEncounter(room.encounter);
    room.pressureTimerMs = 0u;
    (void)applyMatchTransition(room, MatchPhase::kPlaying);
    room.intermissionMs = 0;
    spawnPressure(room);
  } else if (room.run.stage == ac::expedition::Stage::kReward)
    room.intermissionMs = room.run.rewardDeadlineMs > room.run.clockMs ? room.run.rewardDeadlineMs - room.run.clockMs : 0u;
}
void executeExpeditionTools(Room& room) noexcept {
  for (uint8_t i = 0u; i < room.expeditionToolCount; ++i) {
    const uint16_t pid = room.expeditionToolQueue[i];
    auto* entity = playerEntityAt(room, pid);
    const bool accepted = room.phase == MatchPhase::kPlaying && entity &&
        ac::expedition::pulse(*room.world, *entity, static_cast<uint32_t>(room.world->timeMs));
    auto* receipt = receiptFor(room, pid);
    if (receipt) {
      receipt->queued = false;
      if (receipt->toolRequest.requestId == room.expeditionToolRequestIds[i]) {
        receipt->toolResult = accepted ? 1u : 8u;
        receipt->ackRequestId = receipt->toolRequest.requestId; receipt->ackResult = receipt->toolResult;
        if (identical(receipt->request, receipt->toolRequest)) receipt->result = receipt->toolResult;
      }
    }
    ++room.run.revision;
  }
  room.expeditionToolCount = 0u;
}
void stepRoomEncounter(Room& room, uint64_t nowMs, uint32_t dtMs) noexcept {
  uint32_t invaders = 0u, living = 0u;
  bool defended = false;
  for (std::size_t i = 0u; i < room.world->activeCount; ++i) {
    const auto& entity = room.world->entities[room.world->activeIds[i] - 1u];
    const double dx = entity.pos.x - 12.0;
    const bool inside = dx * dx + entity.pos.z * entity.pos.z <= 36.0;
    if (entity.kind == ac::sim::EntityKind::kSheep && entity.hp > 0.0 && inside) ++invaders;
    if (entity.kind == ac::sim::EntityKind::kPlayer && !entity.idle && !entity.downed.downed && entity.hp > 0.0) {
      ++living; if (inside) defended = true;
    }
  }
  ac::expedition::stepEncounter(room.encounter, dtMs, defended, invaders);
  room.run.objectiveHp = static_cast<uint16_t>(room.encounter.hpMilli / 1000u);
  room.run.progressMs = room.encounter.progressMs; room.run.combatLeftMs = room.encounter.remainingMs;
  ac::expedition::Result result = static_cast<ac::expedition::Result>(room.encounter.result);
  if (result == ac::expedition::Result::kNone && living == 0u) result = ac::expedition::Result::kAllDowned;
  if (result != ac::expedition::Result::kNone) {
    ac::expedition::finishEncounter(room.run, result);
    clearThreats(room);
    if (room.run.stage == ac::expedition::Stage::kEnded) endMatch(room, nowMs, result == ac::expedition::Result::kSuccess ? 0 : 1);
    else { (void)applyMatchTransition(room, MatchPhase::kIntermission); room.intermissionMs = 30000; }
    return;
  }
  room.pressureTimerMs += dtMs;
  if (room.pressureTimerMs >= 25000u) { room.pressureTimerMs -= 25000u; spawnPressure(room); }
}
}  // namespace ac::room
