#include "tiny_test.hpp"
#include "room/room.hpp"
#include "expedition/room_run.hpp"
#include "sim/entity_table.hpp"
#include "sim/world.hpp"

namespace {
struct RoomScenario {
  std::unique_ptr<ac::room::Room> owned = std::make_unique<ac::room::Room>();
  ac::room::Session host{};
  ac::net::ExpeditionState state{};
  ac::room::RoomDeps deps{};
  RoomScenario() {
    owned->world = ac::sim::createWorld(42u);
    for (std::size_t i = owned->world->activeCount; i > 0u; --i)
      ac::sim::despawnEntity(*owned->world, owned->world->activeIds[i - 1u]);
    (void)ac::room::roomJoin(*owned, host, 0u);
    deps.user = this;
    deps.sendExpeditionState = [](void* user, const ac::room::Room&, ac::room::Session&,
                                 const ac::net::ExpeditionState& state) {
      static_cast<RoomScenario*>(user)->state = state;
    };
  }
  bool request(uint32_t id, uint8_t op, uint8_t choice = 0u, uint32_t offer = 0u) {
    ac::net::ExpeditionRequest request{};
    request.requestId = id; request.op = op; request.choiceId = choice;
    request.offerId = offer; request.runId = op == 1u || op == 6u ? 0u : owned->run.runId;
    return ac::room::roomExpeditionRequest(*owned, host, request, deps);
  }
  void start() {
    (void)request(1u, 1u); host.ready = true;
    (void)ac::room::tryStartMatch(*owned, host);
    for (uint32_t i = 0u; i < 30u; ++i) ac::room::roomTick(*owned, deps, i * 50u);
  }
};
}
AC_TEST(expedition_room_host_enables_mode_and_loading_enters_safe_reward) {
  RoomScenario scenario;
  auto& room = *scenario.owned;
  AC_CHECK(scenario.request(1u, 1u));
  AC_CHECK(room.expeditionEnabled);
  scenario.host.ready = true;
  AC_CHECK(ac::room::tryStartMatch(room, scenario.host) == ac::room::StartOutcome::kOk);
  for (uint32_t i = 0u; i < 29u; ++i) ac::room::roomTick(room, scenario.deps, i * 50u);
  AC_CHECK(room.phase == ac::room::MatchPhase::kLoading);
  ac::room::roomTick(room, scenario.deps, 1500u);
  AC_CHECK(room.phase == ac::room::MatchPhase::kIntermission);
  AC_CHECK(room.run.stage == ac::expedition::Stage::kReward);
  AC_CHECK(scenario.state.choices[0] == 1u);
  AC_CHECK(scenario.state.choices[1] == 4u);
  AC_CHECK(scenario.state.choices[2] == 0u);
  AC_CHECK(room.director.totalSpawns == 0);
}
AC_TEST(expedition_room_start_preserves_existing_ammo_crates) {
  RoomScenario scenario;
  AC_CHECK(scenario.owned->ammoCrateCount == 0u);
  scenario.start();
  auto& room = *scenario.owned;
  uint32_t pickups = 0u;
  for (std::size_t i = 0u; i < room.world->activeCount; ++i)
    if (room.world->entities[room.world->activeIds[i] - 1u].kind == ac::sim::EntityKind::kPickup) ++pickups;
  AC_CHECK(room.ammoCrateCount == 4u);
  AC_CHECK(pickups == 4u);
  const uint16_t firstCrate = room.ammoCrateIds[0];
  AC_CHECK(scenario.request(1u, 2u, 1u, scenario.state.offerId));
  ac::room::roomTick(room, scenario.deps, 1550u);
  auto* player = ac::room::playerEntityAt(room, scenario.host.pid);
  AC_CHECK(player != nullptr);
  player->pos = {10.0, 0.0, 10.0};
  player->weapon.reserveAmmo = 0;
  for (auto& magazine : player->weapon.magInSlot) magazine = 0;
  ac::sim::Command interact{};
  interact.buttons = ac::config::kButtonInteract;
  AC_CHECK(ac::room::roomApplyCommand(room, scenario.host, interact));
  for (uint32_t i = 0u; i < 30u; ++i) ac::room::roomTick(room, scenario.deps, 1600u + i * 50u);
  AC_CHECK(player->weapon.reserveAmmo == 120);
  AC_CHECK(player->weapon.magInSlot[0] > 0);
  AC_CHECK(room.ammoCrateCount == 4u);
  player->pos = {12.0, 0.0, 0.0};
  room.encounter.progressMs = 179950u;
  ac::room::roomTick(room, scenario.deps, 3100u);
  AC_CHECK(room.phase == ac::room::MatchPhase::kIntermission);
  AC_CHECK(room.ammoCrateCount == 4u);
  AC_CHECK(room.ammoCrateIds[0] == firstCrate);
  ac::room::endMatch(room, 3150u, 1);
  AC_CHECK(scenario.request(2u, 7u));
  AC_CHECK(room.ammoCrateCount == 0u);
  for (uint32_t i = 0u; i < 30u; ++i) ac::room::roomTick(room, scenario.deps, 3200u + i * 50u);
  AC_CHECK(room.ammoCrateCount == 4u);
  pickups = 0u;
  for (std::size_t i = 0u; i < room.world->activeCount; ++i)
    if (room.world->entities[room.world->activeIds[i] - 1u].kind == ac::sim::EntityKind::kPickup) ++pickups;
  AC_CHECK(pickups == 4u);
  ac::room::endMatch(room, 4800u, 1);
  ac::room::resetMatchForRestart(room);
  AC_CHECK(room.ammoCrateCount == 0u);
  AC_CHECK(scenario.request(1u, 6u));
  scenario.host.ready = true;
  AC_CHECK(ac::room::tryStartMatch(room, scenario.host) == ac::room::StartOutcome::kOk);
  for (uint32_t i = 0u; i < 30u; ++i) ac::room::roomTick(room, scenario.deps, 4850u + i * 50u);
  AC_CHECK(!room.expeditionEnabled);
  AC_CHECK(room.ammoCrateCount == 4u);
  pickups = 0u;
  for (std::size_t i = 0u; i < room.world->activeCount; ++i)
    if (room.world->entities[room.world->activeIds[i] - 1u].kind == ac::sim::EntityKind::kPickup) ++pickups;
  AC_CHECK(pickups == 4u);
}
AC_TEST(expedition_room_two_players_refill_with_fire_reload_and_missing_commands) {
  RoomScenario scenario;
  auto& room = *scenario.owned;
  ac::room::Session peer{};
  AC_CHECK(ac::room::roomJoin(room, peer, 0u) == ac::room::JoinOutcome::kOk);
  peer.ready = true;
  scenario.start();
  AC_CHECK(scenario.request(1u, 2u, 1u, room.run.players[0].offerId));
  ac::net::ExpeditionRequest choose{};
  choose.runId = room.run.runId; choose.requestId = 1u; choose.op = 2u;
  choose.choiceId = 4u; choose.offerId = room.run.players[1].offerId;
  AC_CHECK(ac::room::roomExpeditionRequest(room, peer, choose, scenario.deps));
  ac::room::roomTick(room, scenario.deps, 1550u);
  auto* first = ac::room::playerEntityAt(room, scenario.host.pid);
  auto* second = ac::room::playerEntityAt(room, peer.pid);
  AC_CHECK(first != nullptr && second != nullptr && first != second);
  first->pos = {10.0, 0.0, 10.0}; second->pos = {-10.0, 0.0, -10.0};
  first->weapon.reserveAmmo = 0; second->weapon.reserveAmmo = 0;
  for (auto& magazine : first->weapon.magInSlot) magazine = 0;
  for (auto& magazine : second->weapon.magInSlot) magazine = 0;
  ac::sim::Command held{};
  held.buttons = ac::config::kButtonInteract | ac::config::kButtonReload | ac::config::kButtonFire;
  AC_CHECK(ac::room::roomApplyCommand(room, scenario.host, held));
  AC_CHECK(ac::room::roomApplyCommand(room, peer, held));
  // No replacement command simulates dropped/stale-rejected inputs: Room retains last accepted intent.
  for (uint32_t i = 0u; i < 30u; ++i) ac::room::roomTick(room, scenario.deps, 1600u + i * 50u);
  AC_CHECK(first->interactHeld && second->interactHeld);
  AC_CHECK(first->weapon.reserveAmmo == 120);
  AC_CHECK(second->weapon.reserveAmmo == 120);
}
AC_TEST(expedition_room_supply_reaches_entity_and_pressure_is_finite) {
  RoomScenario scenario;
  scenario.start();
  auto& room = *scenario.owned;
  auto* entity = ac::room::playerEntityAt(room, scenario.host.pid);
  AC_CHECK(entity != nullptr);
  entity->hp = 30.0; entity->weapon.reserveAmmo = 2;
  AC_CHECK(scenario.request(1u, 3u, 0u, scenario.state.offerId));
  AC_CHECK(entity->hp == 55.0);
  AC_CHECK(entity->weapon.reserveAmmo == 32);
  AC_CHECK(scenario.request(1u, 3u, 0u, scenario.state.offerId));
  AC_CHECK(entity->weapon.reserveAmmo == 32);
  ac::room::roomTick(room, scenario.deps, 1550u);
  AC_CHECK(room.world->stats.aliveSheep == 8u);
  for (std::size_t i = room.world->activeCount; i > 0u; --i) {
    const auto id = room.world->activeIds[i - 1u];
    if (room.world->entities[id - 1u].kind == ac::sim::EntityKind::kSheep)
      ac::sim::despawnEntity(*room.world, id);
  }
  ac::sim::updateWorldStats(*room.world);
  for (uint32_t i = 0u; i < 100u; ++i) ac::room::roomTick(room, scenario.deps, 1600u + i * 50u);
  AC_CHECK(room.world->stats.aliveSheep == 0u);
  AC_CHECK(room.run.stage == ac::expedition::Stage::kCombat);
  AC_CHECK(room.phase == ac::room::MatchPhase::kPlaying);
  AC_CHECK(entity->upgrade.points == 0u);
}
AC_TEST(expedition_room_node_success_clears_threats_preserves_build_and_ends_third_node) {
  RoomScenario scenario;
  scenario.start();
  auto& room = *scenario.owned;
  AC_CHECK(scenario.request(1u, 2u, 1u, scenario.state.offerId));
  ac::room::roomTick(room, scenario.deps, 1550u);
  auto* entity = ac::room::playerEntityAt(room, scenario.host.pid);
  AC_CHECK(entity != nullptr);
  entity->pos = {12.0, 0.0, 0.0};
  entity->build.pulseReadyAtMs = 10000u;
  room.encounter.progressMs = 179950u;
  ac::room::roomTick(room, scenario.deps, 1600u);
  AC_CHECK(room.run.node == 2u);
  AC_CHECK(room.phase == ac::room::MatchPhase::kIntermission);
  AC_CHECK(room.world->stats.aliveSheep == 0u);
  AC_CHECK(entity->build.selectedMask == 1u);
  AC_CHECK(entity->build.pulseReadyAtMs == 10000u);
  AC_CHECK(scenario.state.offerCount == 3u);
  AC_CHECK(scenario.request(2u, 3u, 0u, scenario.state.offerId));
  ac::room::roomTick(room, scenario.deps, 1650u);
  room.encounter.progressMs = 179950u;
  ac::room::roomTick(room, scenario.deps, 1700u);
  AC_CHECK(room.run.node == 3u);
  AC_CHECK(scenario.request(3u, 3u, 0u, scenario.state.offerId));
  ac::room::roomTick(room, scenario.deps, 1750u);
  room.encounter.progressMs = 179950u;
  ac::room::roomTick(room, scenario.deps, 1800u);
  AC_CHECK(room.phase == ac::room::MatchPhase::kEnded);
  AC_CHECK(room.run.result == ac::expedition::Result::kSuccess);
  AC_CHECK(room.match.hasResult);
  const uint32_t completedRunId = room.run.runId;
  AC_CHECK(scenario.request(4u, 7u));
  AC_CHECK(room.phase == ac::room::MatchPhase::kLoading);
  AC_CHECK(entity->build.selectedMask == 0u);
  AC_CHECK(entity->build.pulseReadyAtMs == 0u);
  ac::net::ExpeditionRequest restartRetry{};
  restartRetry.runId = completedRunId;
  restartRetry.requestId = 4u; restartRetry.op = 7u;
  for (uint32_t i = 0u; i < 30u; ++i) ac::room::roomTick(room, scenario.deps, 1850u + i * 50u);
  const uint32_t nextRunId = room.run.runId;
  AC_CHECK(ac::room::roomExpeditionRequest(room, scenario.host, restartRetry, scenario.deps));
  AC_CHECK(room.run.runId == nextRunId);
  AC_CHECK(room.phase == ac::room::MatchPhase::kIntermission);
}
AC_TEST(expedition_room_objective_failure_timeout_and_all_downed_end_match) {
  for (uint8_t failure = 0u; failure < 3u; ++failure) {
    RoomScenario scenario;
    scenario.start();
    auto& room = *scenario.owned;
    AC_CHECK(scenario.request(1u, 3u, 0u, scenario.state.offerId));
    ac::room::roomTick(room, scenario.deps, 1550u);
    auto* entity = ac::room::playerEntityAt(room, scenario.host.pid);
    AC_CHECK(entity != nullptr);
    if (failure == 0u) {
      room.encounter.hpMilli = 1u;
      auto sheep = ac::sim::spawnEntity(*room.world, ac::sim::EntityKind::kSheep, {12.0, 0.0, 0.0});
      AC_CHECK(sheep.isOk);
      room.world->entities[sheep.id - 1u].hp = 100.0;
    } else if (failure == 1u) room.encounter.remainingMs = 50u;
    else entity->downed.downed = true;
    ac::room::roomTick(room, scenario.deps, 1600u);
    AC_CHECK(room.phase == ac::room::MatchPhase::kEnded);
    AC_CHECK(room.match.hasResult);
    AC_CHECK(room.match.winnerTeam == 1u);
    AC_CHECK(scenario.state.result == static_cast<uint8_t>(failure + 2u));
  }
}
AC_TEST(expedition_room_reconnect_projection_restores_offer_build_ack_and_clock) {
  RoomScenario scenario;
  scenario.start();
  auto& room = *scenario.owned;
  const uint32_t offer = scenario.state.offerId;
  AC_CHECK(scenario.request(1u, 4u, 0u, offer));
  const auto before = scenario.state;
  AC_CHECK(ac::room::roomDisconnect(room, scenario.host, 1500u));
  ac::room::Session incoming{};
  AC_CHECK(ac::room::roomReconnect(room, scenario.host, incoming));
  AC_CHECK(ac::room::sendExpeditionState(room, incoming, scenario.deps));
  AC_CHECK(scenario.state.runId == before.runId);
  AC_CHECK(scenario.state.offerId == before.offerId);
  AC_CHECK(scenario.state.choices == before.choices);
  AC_CHECK(scenario.state.rewardDeadlineMs == before.rewardDeadlineMs);
  AC_CHECK(scenario.state.lastRequestId == 1u);
  AC_CHECK(scenario.state.lastResult == 1u);
  AC_CHECK(scenario.state.rerollsLeft == 0u);
}
AC_TEST(expedition_room_pending_tool_rejects_second_request_without_losing_first) {
  RoomScenario scenario;
  scenario.start();
  auto& room = *scenario.owned;
  AC_CHECK(scenario.request(1u, 3u, 0u, scenario.state.offerId));
  ac::room::roomTick(room, scenario.deps, 1550u);
  auto* entity = ac::room::playerEntityAt(room, scenario.host.pid);
  AC_CHECK(entity != nullptr);
  entity->hp = 40.0;
  AC_CHECK(scenario.request(2u, 5u, 1u));
  AC_CHECK(!scenario.request(3u, 5u, 1u));
  AC_CHECK(scenario.state.lastRequestId == 3u);
  AC_CHECK(scenario.state.lastResult == 8u);
  ac::room::roomTick(room, scenario.deps, 1600u);
  AC_CHECK(entity->hp == 65.0);
  AC_CHECK(entity->build.triggerCount == 1u);
  AC_CHECK(!scenario.request(3u, 5u, 1u));
  AC_CHECK(scenario.state.lastResult == 8u);
  AC_CHECK(scenario.request(2u, 5u, 1u));
  AC_CHECK(scenario.state.lastRequestId == 2u);
  AC_CHECK(scenario.state.lastResult == 1u);
  AC_CHECK(entity->build.triggerCount == 1u);
}
AC_TEST(expedition_room_pending_failed_tool_retains_failure_after_newer_rejection) {
  RoomScenario scenario;
  scenario.start();
  auto& room = *scenario.owned;
  AC_CHECK(scenario.request(1u, 3u, 0u, scenario.state.offerId));
  ac::room::roomTick(room, scenario.deps, 1550u);
  auto* entity = ac::room::playerEntityAt(room, scenario.host.pid);
  AC_CHECK(entity != nullptr);
  entity->build.pulseReadyAtMs = 20000u;
  AC_CHECK(scenario.request(2u, 5u, 1u));
  AC_CHECK(!scenario.request(3u, 5u, 1u));
  ac::room::roomTick(room, scenario.deps, 1600u);
  AC_CHECK(!scenario.request(2u, 5u, 1u));
  AC_CHECK(scenario.state.lastRequestId == 2u);
  AC_CHECK(scenario.state.lastResult == 8u);
  AC_CHECK(entity->build.triggerCount == 0u);
}
AC_TEST(expedition_room_permanent_leave_does_not_leak_receipts_to_reused_pid) {
  RoomScenario scenario;
  auto& room = *scenario.owned;
  AC_CHECK(scenario.request(1u, 1u));
  AC_CHECK(scenario.request(2u, 6u));
  const uint32_t oldPid = scenario.host.pid;
  AC_CHECK(ac::room::roomLeave(room, scenario.host, 100u));
  ac::room::Session replacement{};
  AC_CHECK(ac::room::roomJoin(room, replacement, 150u) == ac::room::JoinOutcome::kOk);
  AC_CHECK(replacement.pid == oldPid);
  ac::net::ExpeditionRequest request{};
  request.op = 1u; request.requestId = 1u;
  AC_CHECK(ac::room::roomExpeditionRequest(room, replacement, request, scenario.deps));
  AC_CHECK(scenario.state.lastRequestId == 1u);
  AC_CHECK(scenario.state.lastResult == 1u);
}
AC_TEST(expedition_room_old_run_failure_cannot_poison_current_request_watermark) {
  RoomScenario scenario;
  scenario.start();
  auto& room = *scenario.owned;
  ac::net::ExpeditionRequest stale{};
  stale.requestId = 100u; stale.op = 3u;
  stale.runId = room.run.runId + 10u;
  stale.offerId = scenario.state.offerId;
  AC_CHECK(!ac::room::roomExpeditionRequest(room, scenario.host, stale, scenario.deps));
  AC_CHECK(scenario.state.lastResult == 3u);
  AC_CHECK(scenario.request(1u, 3u, 0u, scenario.state.offerId));
  AC_CHECK(scenario.state.lastResult == 1u);
}
AC_TEST(expedition_room_tool_is_deferred_and_failure_is_idempotent) {
  RoomScenario scenario;
  scenario.start();
  auto& room = *scenario.owned;
  AC_CHECK(scenario.request(1u, 2u, 1u, scenario.state.offerId));
  ac::room::roomTick(room, scenario.deps, 1550u);
  auto* entity = ac::room::playerEntityAt(room, scenario.host.pid);
  AC_CHECK(entity != nullptr);
  entity->hp = 40.0;
  AC_CHECK(scenario.request(2u, 5u, 1u));
  AC_CHECK(entity->hp == 40.0);
  AC_CHECK(scenario.state.lastRequestId == 1u);
  AC_CHECK(scenario.request(2u, 5u, 1u));
  ac::room::roomTick(room, scenario.deps, 1600u);
  AC_CHECK(entity->hp == 65.0);
  AC_CHECK(entity->build.triggerCount == 1u);
  AC_CHECK(scenario.state.lastRequestId == 2u);
  AC_CHECK(scenario.state.lastResult == 1u);
  AC_CHECK(scenario.request(3u, 5u, 1u));
  ac::room::roomTick(room, scenario.deps, 1650u);
  AC_CHECK(scenario.state.lastRequestId == 3u);
  AC_CHECK(scenario.state.lastResult == 8u);
  AC_CHECK(!scenario.request(3u, 5u, 1u));
  AC_CHECK(entity->build.triggerCount == 1u);
  AC_CHECK(!scenario.request(2u, 3u));
  AC_CHECK(scenario.state.lastResult == 7u);
  AC_CHECK(!scenario.request(3u, 5u, 1u));
  AC_CHECK(scenario.state.lastResult == 8u);
}
