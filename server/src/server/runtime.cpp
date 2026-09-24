#include "server/runtime.hpp"

#include <chrono>
#include <span>

#include "config/waves.hpp"
#include "core/log.hpp"
#include "core/quantize.hpp"
#include "core/scheduler.hpp"
#include "core/version.hpp"
#include "net/codec.hpp"
#include "replication/delta.hpp"
#include "waves/director.hpp"

namespace ac::server {
namespace {

std::uint32_t wallMs32(std::uint64_t nowMs) noexcept { return static_cast<std::uint32_t>(nowMs); }

std::uint32_t millisBetween(std::chrono::steady_clock::time_point from,
                            std::chrono::steady_clock::time_point to) noexcept {
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
  if (micros <= 0) return 0u;
  return static_cast<std::uint32_t>((micros + 500) / 1000);
}

// §5.3 的反量化：轴 /127，角度按 u16 单位；量化表在 core/quantize.hpp，这里不另设系数。
ac::sim::Command toSimCommand(const ac::net::CommandPayload& payload) noexcept {
  ac::sim::Command command{};
  command.moveX = ac::dequantizeAxis(payload.moveX);
  command.moveY = ac::dequantizeAxis(payload.moveY);
  command.yaw = ac::dequantizeAngle(payload.yaw);
  command.pitch = ac::dequantizeAngle(payload.pitch);
  command.buttons = payload.buttons;
  command.switchTo = payload.switchTo;
  command.seq = payload.seq;
  command.clientTick = payload.clientTick;
  return command;
}

ac::net::PacketHeader makeHeader(ac::net::PacketType type, std::uint16_t session,
                                 std::uint16_t seq) noexcept {
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<std::uint8_t>(type);
  header.flags = ac::net::requiredFlags(type);
  header.session = session;
  header.seq = seq;
  return header;
}

}  // namespace

Runtime::Runtime() = default;

Runtime::~Runtime() { stop(); }

bool Runtime::start(const RuntimeConfig& config, std::string* error) {
  config_ = config;
  if (!ac::net::ensureWinsock()) {
    if (error != nullptr) *error = "winsock init failed";
    return false;
  }
  dataDir_ = ac::persist::dataDirFromEnv();
  store_ = ac::persist::openMatchStore(dataDir_, error);
  if (store_ == nullptr) return false;

  if (!udp_.bind(config_.udpPort)) {
    if (error != nullptr) *error = "udp bind failed on port " + std::to_string(config_.udpPort);
    return false;
  }
  udpPort_ = udp_.boundPort();

  startMs_ = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count());
  ac::room::initRoomRegistry(registry_, wallMs32(startMs_));
  registry_.worldSeed = config_.seed;
  room_ = ac::room::createRoom(registry_, startMs_);
  if (room_ == nullptr) {
    udp_.close();
    if (error != nullptr) *error = "createRoom failed";
    return false;
  }
  deps_.user = this;
  deps_.replicate = &Runtime::replicateThunk;
  deps_.sendMatchState = &Runtime::matchStateThunk;

  if (config_.isHttpEnabled) {
    ac::http::HttpListenerDeps httpDeps{};
    httpDeps.user = this;
    httpDeps.fill = &Runtime::httpFillThunk;
    if (!http_.start(config_.httpPort, &httpState_, httpDeps, error)) {
      udp_.close();
      ac::room::destroyRoom(registry_, *room_);
      room_ = nullptr;
      return false;
    }
    httpPort_ = http_.boundPort();
  }

  ac::core::startScheduler(scheduler_, startMs_, 0u);
  process_.protocol = ac::version::kProtocol;
  process_.tickMs = ac::version::kTickMs;
  isRunning_ = true;
  ensureMatchRunning(startMs_);
  return true;
}

void Runtime::stop() {
  if (!isRunning_ && room_ == nullptr) return;
  isRunning_ = false;
  http_.stop();
  udp_.close();
  if (room_ != nullptr) {
    ac::room::destroyRoom(registry_, *room_);
    room_ = nullptr;
  }
  for (Client& client : clients_) client = Client{};
}

std::size_t Runtime::roomCount() const noexcept {
  return room_ == nullptr ? 0u : static_cast<std::size_t>(registry_.count);
}

std::size_t Runtime::clientCount() const noexcept {
  std::size_t count = 0u;
  for (const Client& client : clients_) {
    if (client.isUsed) ++count;
  }
  return count;
}

Runtime::Client* Runtime::findClientByTransport(std::uint16_t transportId) noexcept {
  for (Client& client : clients_) {
    if (client.isUsed && client.transportId == transportId) return &client;
  }
  return nullptr;
}

Runtime::Client* Runtime::claimClient(std::uint16_t transportId, const ac::net::Endpoint& endpoint,
                                      std::uint64_t nowMs) noexcept {
  if (Client* existing = findClientByTransport(transportId); existing != nullptr) {
    existing->endpoint = endpoint;
    return existing;
  }
  for (Client& client : clients_) {
    if (client.isUsed) continue;
    client = Client{};
    client.isUsed = true;
    client.endpoint = endpoint;
    client.transportId = transportId;
    client.session = ac::room::createSession(transportId, nowMs);
    return &client;
  }
  return nullptr;
}

void Runtime::releaseClient(Client& client) noexcept {
  if (room_ != nullptr && ac::room::isInRoom(client.session)) {
    // 用当前轮询时刻：startMs_ 是进程启动时刻，传给 roomLeave 会把空房时刻记成过去（评审 #2）。
    ac::room::roomLeave(*room_, client.session, lastPollMs_ == 0u ? startMs_ : lastPollMs_);
  }
  client = Client{};
}

void Runtime::receivePackets(std::uint64_t nowMs) {
  for (std::size_t i = 0u; i < kMaxPacketsPerPoll; ++i) {
    if (!udp_.poll(0)) break;
    ac::net::Endpoint from{};
    const int got = udp_.recvFrom(from, std::span<std::uint8_t>(recvBuffer_, sizeof(recvBuffer_)));
    if (got <= 0) break;
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kBytesIn,
                            static_cast<std::uint64_t>(got));
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kFramesIn, 1u);
    handlePacket(from, recvBuffer_, static_cast<std::size_t>(got), nowMs);
  }
}

void Runtime::handlePacket(const ac::net::Endpoint& from, const std::uint8_t* bytes,
                           std::size_t size, std::uint64_t nowMs) {
  const ac::net::DecodeResult<ac::net::PacketInfo> packet = ac::net::decodePacket(bytes, size);
  if (!packet.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const ac::net::PacketInfo& info = packet.value;
  // 超长包单独归类（§5 的 oversized 出口），其余解码失败都算丢弃帧。
  if (!ac::security::validatePacketSize(size).isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kOversizedFrames, 1u);
    return;
  }
  if (!ac::security::isClientToServerType(info.header.type)) {
    // 客户端方向没有的快照/事件/MatchState/Fragment：非法方向，丢弃并计数。
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const std::uint8_t* payload = ac::net::packetPayload(info, bytes, size);
  if (payload == nullptr) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const std::size_t payloadBytes = size - info.payloadOffset;
  // §5.2：任何带在册 session 的包都是存活证据。命令是否被采纳（tick/seq 校验）是另一回事——
  // 以前只有 Hello/Resume 会刷新心跳，于是「命令被校验拒掉」会顺带把会话判死（实测 soak 33s 后房间停摆）。
  if (ac::net::SessionRecord* record = handshake_.sessions().find(info.header.session);
      record != nullptr && record->phase != ac::net::SessionPhase::kReleased) {
    record->keepAlive.onAnyPacket(wallMs32(nowMs));
  }
  switch (static_cast<ac::net::PacketType>(info.header.type)) {
    case ac::net::PacketType::kHello:
      handleHello(from, bytes, size, nowMs);
      break;
    case ac::net::PacketType::kResume:
      handleResume(from, info.header.session, bytes, size, nowMs);
      break;
    case ac::net::PacketType::kCommand:
      handleCommand(info.header.session, bytes, size, payloadBytes, nowMs);
      break;
    case ac::net::PacketType::kKeepAlive: {
      const ac::net::SessionValidation validation =
          handshake_.validateSession(info.header.session);
      if (!validation.isAccepted) {
        ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
        break;
      }
      if (ac::net::SessionRecord* record = handshake_.sessions().find(info.header.session);
          record != nullptr) {
        record->keepAlive.onAnyPacket(wallMs32(nowMs));
      }
      break;
    }
    default:
      ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
      break;
  }
}

void Runtime::handleHello(const ac::net::Endpoint& from, const std::uint8_t* frame,
                          std::size_t size, std::uint64_t nowMs) {
  const ac::net::DecodeResult<ac::net::HelloPayload> hello = ac::net::decodeHello(frame, size);
  if (!hello.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const ac::net::HandshakeOutcome outcome =
      handshake_.onHello(hello.value.nonce, hello.value.token, wallMs32(nowMs));
  if (outcome.isDisconnectDue) {
    std::uint8_t buffer[32] = {};
    const ac::net::DisconnectPayload body{static_cast<std::uint8_t>(outcome.reason)};
    const ac::net::EncodeResult encoded = ac::net::encodeDisconnect(
        makeHeader(ac::net::PacketType::kDisconnect, 0u, 0u), ac::net::ReliableExt{}, body, buffer,
        sizeof(buffer));
    if (encoded.isOk) {
      (void)udp_.sendTo(from, std::span<const std::uint8_t>(buffer, encoded.bytes));
    }
    return;
  }
  if (!outcome.isHelloAckDue) return;  // 同一次握手的重发：静默去重
  ac::net::SessionRecord* record = handshake_.sessions().find(outcome.session);
  if (record == nullptr) return;
  Client* client = claimClient(outcome.session, from, nowMs);
  if (client == nullptr) return;  // 槽位打满：丢包不回（槽位上限是运行时自己的约束）
  client->session.token = record->token;
  if (room_ != nullptr && !ac::room::isInRoom(client->session)) {
    (void)ac::room::roomJoin(*room_, client->session, nowMs);
  }
  std::uint8_t buffer[64] = {};
  const ac::net::HelloAckPayload ack{room_ == nullptr ? 0u : room_->world->tick, record->salt};
  const ac::net::EncodeResult encoded = ac::net::encodeHelloAck(
      makeHeader(ac::net::PacketType::kHelloAck, outcome.session, 0u), ac::net::ReliableExt{}, ack,
      buffer, sizeof(buffer));
  if (encoded.isOk) {
    const int sent = udp_.sendTo(from, std::span<const std::uint8_t>(buffer, encoded.bytes));
    if (sent > 0) {
      ac::metrics::addCounter(counters_, ac::metrics::CounterId::kBytesOut,
                              static_cast<std::uint64_t>(encoded.bytes));
      ac::metrics::addCounter(counters_, ac::metrics::CounterId::kFramesOut, 1u);
    }
  }
}

void Runtime::handleResume(const ac::net::Endpoint& from, std::uint16_t session,
                           const std::uint8_t* frame, std::size_t size, std::uint64_t nowMs) {
  const ac::net::DecodeResult<ac::net::ResumePayload> resume =
      ac::net::decodeResume(frame, size);
  if (!resume.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  // Resume 的 session 在包头里：先按 §5.2 校验，再交给会话层。
  const ac::net::SessionValidation validation = handshake_.validateSession(session);
  if (!validation.isAccepted) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const ac::net::HandshakeOutcome outcome =
      handshake_.onResume(session, resume.value.token, wallMs32(nowMs));
  if (outcome.isDisconnectDue) return;
  if (!outcome.isResumed) return;
  Client* resumed = findClientByTransport(session);
  if (resumed == nullptr) return;
  resumed->endpoint = from;
  resumed->isFullSnapshotDue = outcome.isFullSnapshotDue;
  ac::metrics::addCounter(counters_, ac::metrics::CounterId::kGraceReconnects, 1u);
  if (room_ != nullptr) {
    for (std::size_t i = 0u; i < ac::room::kMaxPlayersPerRoom; ++i) {
      ac::room::Session* existing = room_->sessions[i];
      if (existing == nullptr || existing == &resumed->session) continue;
      if (existing->token == 0u || existing->token != resume.value.token) continue;
      if (ac::room::roomReconnect(*room_, *existing, resumed->session)) break;
    }
  }
}

void Runtime::handleCommand(std::uint16_t session, const std::uint8_t* frame,
                            std::size_t size, std::size_t payloadBytes, std::uint64_t nowMs) {
  (void)nowMs;
  // §5.2：非 Hello 包必须带在册 session，否则丢弃并计 kBadSession（S13 §5 名单里并入丢弃帧）。
  if (!handshake_.validateSession(session).isAccepted) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  if (!ac::security::validatePayloadSize(payloadBytes).isOk) {
    ac::security::noteValidateFailure(&counters_, ac::security::ValidateReason::kPayloadTooLarge);
    return;
  }
  const ac::net::DecodeResult<ac::net::CommandPayload> decoded =
      ac::net::decodeCommand(frame, size);
  if (!decoded.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  Client* client = findClientByTransport(session);
  if (client == nullptr || room_ == nullptr) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const std::uint32_t serverTick = room_->world->tick;
  const ac::security::ValidateResult tickCheck =
      ac::security::validateClientTick(decoded.value.clientTick, serverTick);
  if (!tickCheck.isOk) {
    ac::security::noteValidateFailure(&counters_, tickCheck.reason);
    return;
  }
  if (!ac::security::noteCommandSequence(client->dedup, decoded.value.seq)) {
    ac::security::noteValidateFailure(&counters_, ac::security::ValidateReason::kDuplicateSequence);
    return;
  }
  ac::security::ClampReport report{};
  const ac::sim::Command command = ac::security::sanitizeCommand(toSimCommand(decoded.value), report);
  if (!ac::room::roomApplyCommand(*room_, client->session, command)) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
  }
}

void Runtime::pollOnce(std::uint64_t nowMs) {
  if (!isRunning_) return;
  lastPollMs_ = nowMs;
  receivePackets(nowMs);

  const ac::net::SessionTick sessionTick = handshake_.sessions().tick(wallMs32(nowMs));
  if (sessionTick.wentOffline > 0u) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kGraceStarts,
                            sessionTick.wentOffline);
  }
  if (sessionTick.released > 0u) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kGraceTimeouts,
                            sessionTick.released);
    purgeReleasedClients();
  }

  if (room_ != nullptr) {
    ensureMatchRunning(nowMs);
    // 计「房间真正执行的 tick」而不是 world->tick 增量：loading/intermission 也按 50ms 走 tick，
    // 世界却只在 playing 步进；按 world->tick 记会把开局这段算成上千毫秒的假调度误差（实测 1.5s）。
    const auto workStart = std::chrono::steady_clock::now();
    (void)ac::room::updateRoom(*room_, deps_, nowMs);
    topUpSheep();
    const std::uint32_t workMs = millisBetween(workStart, std::chrono::steady_clock::now());
    // §5 G8：房间因补不上而丢掉的 tick 必须计进调度器（否则 G8 的这一项永远读 0）。
    const std::uint32_t skippedTotal = room_->match.counters.skipped;
    if (skippedTotal > skippedSeen_) {
      ac::core::noteTickSkip(scheduler_, skippedTotal - skippedSeen_, &counters_);
      skippedSeen_ = skippedTotal;
    }
    const std::uint32_t executedTicks = room_->match.counters.ticks;
    // 房间重启会把计数清零：只按「计数增长」取增量，无符号回绕会变成 40 亿次记账（实测直接挂死）。
    const std::uint32_t advanced = executedTicks > countedTicks_ ? executedTicks - countedTicks_ : 0u;
    countedTicks_ = executedTicks;
    // tickIndex 必须与世界的 tick 同步：没推进就不记账（否则轮询次数会把调度误差灌成假的）。
    for (std::uint32_t i = 0u; i < advanced; ++i) {
      const std::uint32_t share = i + 1u == advanced ? workMs : workMs / advanced;
      (void)ac::core::noteTickRun(scheduler_, nowMs, share, &counters_, &gauges_);
    }
  }

  if (config_.isHttpEnabled) (void)http_.serveOnce(wallMs32(nowMs), 0);
  updateRates(nowMs);
  publishMetrics(nowMs);
}

void Runtime::purgeReleasedClients() {
  for (Client& client : clients_) {
    if (!client.isUsed) continue;
    const ac::net::SessionRecord* record = handshake_.sessions().find(client.transportId);
    if (record != nullptr && record->phase != ac::net::SessionPhase::kReleased) continue;
    releaseClient(client);
  }
}

void Runtime::ensureMatchRunning(std::uint64_t nowMs) {
  if (room_ == nullptr) return;
  if (room_->phase == ac::room::MatchPhase::kPlaying) return;
  if (room_->phase == ac::room::MatchPhase::kEnded) {
    (void)ac::room::applyMatchTransition(*room_, ac::room::MatchPhase::kLobby);
    ac::room::resetMatchForRestart(*room_);
  }
  ac::room::Session* host = nullptr;
  for (std::size_t i = 0u; i < ac::room::kMaxPlayersPerRoom; ++i) {
    ac::room::Session* session = room_->sessions[i];
    if (session == nullptr || !ac::room::isConnected(*session)) continue;
    if (config_.isAutoReady) {
      (void)ac::room::roomSetReady(*room_, *session, true, 0u, deps_);
    }
    if (host == nullptr) host = session;
  }
  if (host != nullptr) {
    ac::room::tryStartMatch(*room_, *host);
    if (room_->phase == ac::room::MatchPhase::kPlaying) {
      // §5 G6：tick 调度误差只在比赛进行中有意义。世界只在 playing 步进，调度器若从
      // 进程启动就算起，会把「等第一位玩家进场」的这一段算成上千毫秒的假误差（本批实测 1.5s）。
      ac::core::startScheduler(scheduler_, nowMs, 0u);
      // 开球即对齐房间的累加器：包在门口等玩家/等进程启动的那段墙钟时间不是「欠下的 tick」，
      // 否则首帧会把 1.5s（≈30 tick）一次性补/丢（本批实测 skips 与 1.5s 假误差同源）。
      room_->lastUpdateMs = nowMs;
      room_->accumulatorMs = 0;
      countedTicks_ = room_->match.counters.ticks;
      tickBaseMs_ = nowMs;
    }
  }
  (void)nowMs;
}

void Runtime::topUpSheep() noexcept {
  if (room_ == nullptr || room_->world == nullptr) return;
  if (config_.sheepTarget <= 0) return;
  if (room_->phase != ac::room::MatchPhase::kPlaying) return;
  const std::size_t alive = static_cast<std::size_t>(ac::waves::aliveSheepCount(*room_->world));
  if (alive >= static_cast<std::size_t>(config_.sheepTarget)) return;
  std::size_t budget = static_cast<std::size_t>(config_.sheepTarget) - alive;
  if (budget > ac::config::kMaxSpawnsPerTick) budget = ac::config::kMaxSpawnsPerTick;
  std::size_t spawned = 0u;
  for (std::size_t i = 0u; i < budget; ++i) {
    const ac::Vec3 point =
        ac::sim::arena::kSpawnPoints[(alive + i) % ac::sim::arena::kSpawnPoints.size()];
    if (!ac::waves::spawnSheepAt(*room_->world, ac::config::SheepKind::kGrunt, point.x, point.z)) {
      break;
    }
    ++spawned;
  }
  // 与波次导演同口径：spawns 在本 tick 结束前先记账，避免 stats 落后一帧。
  room_->world->stats.aliveSheep += static_cast<std::uint32_t>(spawned);
}

void Runtime::replicateThunk(void* user, ac::room::Room& room) {
  static_cast<Runtime*>(user)->onReplicate(room);
}

void Runtime::matchStateThunk(void* user, const ac::room::Room& room, ac::room::Session& session,
                              const ac::net::MatchState& state) {
  static_cast<void>(room);
  static_cast<Runtime*>(user)->onSendMatchState(session, state);
}

void Runtime::httpFillThunk(void* user, ac::http::HttpDeps& out) {
  static_cast<Runtime*>(user)->httpFill(out);
}

void Runtime::onReplicate(ac::room::Room& room) noexcept {
  if (room.world == nullptr) return;
  const std::uint32_t tick = room.world->tick;
  for (std::size_t i = 0u; i < ac::room::kMaxPlayersPerRoom; ++i) {
    ac::room::Session* session = room.sessions[i];
    if (session == nullptr || !ac::room::isConnected(*session)) continue;
    Client* client = findClientByTransport(static_cast<std::uint16_t>(session->id));
    if (client == nullptr || !client->isUsed) continue;
    const std::uint16_t rateX10 = ac::replication::snapshotRateX10(client->rate);
    if (!ac::replication::shouldSendSnapshot(rateX10, tick)) continue;

    ac::replication::DeltaInput input{};
    input.world = room.world.get();
    input.session = client->transportId;
    input.seq = ++client->seq;
    input.lastAckedSeq = 0u;
    input.isForceFull =
        client->isFullSnapshotDue || ac::replication::shouldForceFull(client->baseline, tick);
    const ac::replication::DeltaOutcome outcome =
        ac::replication::encodeDelta(input, client->baseline, sendBuffer_, sizeof(sendBuffer_));
    if (!outcome.isOk) {
      ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
      continue;
    }
    client->isFullSnapshotDue = false;
    recordSnapshotSize(outcome.bytes);
    const ac::replication::QueueVerdict verdict = ac::replication::enqueueSnapshot(
        client->budget, outcome.bytes, &counters_, &gauges_, outcome.recordCount);
    if (verdict != ac::replication::QueueVerdict::kEnqueue) {
      if (verdict == ac::replication::QueueVerdict::kDisconnect) {
        client->endpoint = {};  // 慢消费者：运行时按断线处理（宽限期由会话层推进）
      }
      continue;
    }
    const int sent =
        udp_.sendTo(client->endpoint, std::span<const std::uint8_t>(sendBuffer_, outcome.bytes));
    if (sent <= 0) {
      ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
      continue;
    }
    ac::replication::noteDrained(client->budget, outcome.bytes);
    client->bytesOut += outcome.bytes;
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kBytesOut,
                            static_cast<std::uint64_t>(outcome.bytes));
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kFramesOut, 1u);
  }
}

void Runtime::onSendMatchState(ac::room::Session& session,
                               const ac::net::MatchState& state) noexcept {
  Client* client = findClientByTransport(static_cast<std::uint16_t>(session.id));
  if (client == nullptr || !client->isUsed) return;
  const ac::net::PacketHeader header =
      makeHeader(ac::net::PacketType::kMatchState, client->transportId, ++client->seq);
  ac::net::ReliableExt ext{};
  ext.msgId = client->seq;
  std::uint8_t buffer[ac::room::kMatchStateFrameMaxBytes] = {};
  const ac::net::EncodeResult encoded =
      ac::net::encodeMatchState(header, ext, state, buffer, sizeof(buffer));
  if (!encoded.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const int sent = udp_.sendTo(client->endpoint, std::span<const std::uint8_t>(buffer, encoded.bytes));
  if (sent <= 0) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  ac::replication::enqueueEvent(client->budget, encoded.bytes);
  ac::replication::noteDrained(client->budget, encoded.bytes);
  client->bytesOut += encoded.bytes;
  ac::metrics::addCounter(counters_, ac::metrics::CounterId::kBytesOut,
                          static_cast<std::uint64_t>(encoded.bytes));
  ac::metrics::addCounter(counters_, ac::metrics::CounterId::kFramesOut, 1u);
}

void Runtime::recordSnapshotSize(std::size_t bytes) noexcept {
  if (snapshotCount_ < kSnapshotRingSize) ++snapshotCount_;
  snapshotRing_[snapshotNext_] = static_cast<std::uint32_t>(bytes);
  snapshotNext_ = (snapshotNext_ + 1u) % kSnapshotRingSize;
  if (bytes > snapshotMax_) snapshotMax_ = bytes;
}

void Runtime::updateRates(std::uint64_t nowMs) noexcept {
  for (Client& client : clients_) {
    if (!client.isUsed) continue;
    ac::replication::RateTriggers triggers{};
    triggers.isBacklogOverHalf = ac::replication::isBacklogOverHalf(client.budget);
    // §5（S12 冻结）的三个降档信号：积压过半 / tickSkips 增长 / 房间预算超限增长。
    // 以前这里错喂了 droppedSnapshots，档位机的触发源与 S12 定义不符（评审 #4）。
    const std::uint64_t tickSkips =
        ac::metrics::counterValue(counters_, ac::metrics::CounterId::kTickSkips);
    const std::uint64_t budgetExceeded =
        ac::metrics::counterValue(counters_, ac::metrics::CounterId::kRoomBudgetExceeded);
    triggers.hasTickSkipsGrown = tickSkips > client.lastTickSkips;
    triggers.hasBudgetExceededGrown = budgetExceeded > client.lastBudgetExceeded;
    client.lastTickSkips = tickSkips;
    client.lastBudgetExceeded = budgetExceeded;
    (void)ac::replication::updateSnapshotRate(client.rate, wallMs32(nowMs), triggers, &counters_,
                                              &gauges_);
  }
}

double Runtime::snapshotP95() const noexcept {
  if (snapshotCount_ == 0u) return 0.0;
  std::uint32_t sorted[kSnapshotRingSize] = {};
  for (std::size_t i = 0u; i < snapshotCount_; ++i) sorted[i] = snapshotRing_[i];
  // 近邻秩（与 S12 的 ac_snapshot_bytes_* 同口径）：sorted[ceil(0.95 * N) - 1]。
  std::size_t count = snapshotCount_;
  for (std::size_t i = 1u; i < count; ++i) {
    const std::uint32_t value = sorted[i];
    std::size_t j = i;
    while (j > 0u && sorted[j - 1u] > value) {
      sorted[j] = sorted[j - 1u];
      --j;
    }
    sorted[j] = value;
  }
  std::size_t rank = (count * 95u + 99u) / 100u;
  if (rank == 0u) rank = 1u;
  if (rank > count) rank = count;
  return static_cast<double>(sorted[rank - 1u]);
}

std::size_t Runtime::clientStats(ClientStat* out, std::size_t capacity) const noexcept {
  std::size_t count = 0u;
  for (const Client& client : clients_) {
    if (!client.isUsed) continue;
    if (out != nullptr && count < capacity) {
      ClientStat& stat = out[count];
      stat.transportId = client.transportId;
      stat.bytesOut = client.bytesOut;
      stat.snapshotCount = client.budget.snapshotCount;
      stat.queuedBytes = client.budget.queuedBytes;
    }
    ++count;
  }
  return count;
}

RuntimeMetrics Runtime::metrics() const {
  RuntimeMetrics out{};
  out.ticks = scheduler_.tickIndex;
  out.clients = clientCount();
  out.rooms = roomCount();
  out.players = room_ == nullptr ? 0u : ac::room::activePlayerCount(*room_);
  out.graceActive = handshake_.sessions().graceCount();
  out.aliveSheep =
      room_ == nullptr || room_->world == nullptr
          ? 0u
          : static_cast<std::size_t>(ac::waves::aliveSheepCount(*room_->world));
  out.snapshotSamples = snapshotCount_;
  out.snapshotBytesMax = snapshotMax_;
  out.snapshotBytesP95 = snapshotP95();
  out.scheduleErrorP95Ms = ac::core::tickScheduleErrorP95Ms(scheduler_);
  out.scheduleHeadTailGapMs = ac::core::scheduleHeadTailGapMs(scheduler_);
  out.tickIntervalErrorP95Ms = ac::core::tickIntervalErrorP95Ms(scheduler_);
  out.tickWorkP95Ms = ac::core::tickWorkP95Ms(scheduler_);
  out.simDriftMs = simDriftMs();
  out.eventsDropped = eventsDroppedSeen_;
  out.udpBytesIn = static_cast<std::size_t>(
      ac::metrics::counterValue(counters_, ac::metrics::CounterId::kBytesIn));
  out.udpBytesOut = static_cast<std::size_t>(
      ac::metrics::counterValue(counters_, ac::metrics::CounterId::kBytesOut));
  out.framesIn = static_cast<std::size_t>(
      ac::metrics::counterValue(counters_, ac::metrics::CounterId::kFramesIn));
  out.framesOut = static_cast<std::size_t>(
      ac::metrics::counterValue(counters_, ac::metrics::CounterId::kFramesOut));
  out.hardCorrect = static_cast<std::size_t>(
      ac::metrics::counterValue(counters_, ac::metrics::CounterId::kHardCorrect));
  out.tickSkips = scheduler_.tickSkips;
  return out;
}

// §5 的 ac_sim_drift_ms：模拟已走时长 - 墙上已走时长（正 = 模拟超前）。
double Runtime::simDriftMs() const noexcept {
  if (tickBaseMs_ == 0u || lastPollMs_ < tickBaseMs_) {
    return 0.0;
  }
  const std::int64_t simMs =
      static_cast<std::int64_t>(scheduler_.tickIndex) * static_cast<std::int64_t>(ac::version::kTickMs);
  const std::int64_t wallMs = static_cast<std::int64_t>(lastPollMs_ - tickBaseMs_);
  return static_cast<double>(simMs - wallMs);
}

void Runtime::publishMetrics(std::uint64_t nowMs) {
  process_.protocol = ac::version::kProtocol;
  process_.tickMs = ac::version::kTickMs;
  process_.rooms = static_cast<std::uint32_t>(roomCount());
  process_.connections = static_cast<std::uint32_t>(clientCount());
  process_.players = room_ == nullptr ? 0u : ac::room::activePlayerCount(*room_);
  process_.graceActive = static_cast<std::uint32_t>(handshake_.sessions().graceCount());
  process_.recordsRetained = store_ == nullptr ? 0u : store_->recordCount();
  process_.uptimeSeconds =
      startMs_ == 0u ? 0u : static_cast<std::uint32_t>((nowMs - startMs_) / 1000u);

  double snapshotAvg = 0.0;
  std::size_t queueBytes = 0u;
  std::size_t snapshotCount = 0u;
  std::size_t snapshotClients = 0u;
  for (const Client& client : clients_) {
    if (!client.isUsed) continue;
    snapshotAvg += ac::replication::averageSnapshotBytes(client.budget);
    queueBytes += client.budget.queuedBytes;
    snapshotCount += client.budget.snapshotCount;
    ++snapshotClients;
  }
  // S12 的生产者写的是「单客户端均值」：这里必须除人数，否则 4 人时量值放大 4 倍（评审 #3）。
  if (snapshotClients > 0u) snapshotAvg /= static_cast<double>(snapshotClients);
  ac::metrics::setGaugeIf(&gauges_, ac::metrics::GaugeId::kSnapshotBytesAvg, snapshotAvg);
  ac::metrics::setGaugeIf(&gauges_, ac::metrics::GaugeId::kSnapshotBytesMax,
                          static_cast<double>(snapshotMax_));
  ac::metrics::setGaugeIf(&gauges_, ac::metrics::GaugeId::kSendQueueBytes,
                          static_cast<double>(queueBytes));
  ac::core::publishScheduleGauges(scheduler_, &gauges_);
  const std::size_t dropped =
      room_ == nullptr || room_->world == nullptr ? eventsDroppedSeen_ : room_->world->stats.eventsDropped;
  if (dropped > eventsDroppedSeen_) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kEventsDropped,
                            static_cast<std::uint64_t>(dropped - eventsDroppedSeen_));
    eventsDroppedSeen_ = dropped;
  }
}

void Runtime::httpFill(ac::http::HttpDeps& out) {
  out.store = store_.get();
  metricsBody_ = ac::metrics::renderMetrics({&counters_, &gauges_, process_});
  out.metricsBody = metricsBody_;
  out.health.process = process_;
  out.health.ticks = scheduler_.tickIndex;
  out.counters = &counters_;
}

}  // namespace ac::server
