#include "server/runtime.hpp"

#include <chrono>
#include <span>

#include "config/player.hpp"
#include "config/upgrades.hpp"
#include "config/waves.hpp"
#include "core/clock.hpp"
#include "core/log.hpp"
#include "core/quantize.hpp"
#include "core/scheduler.hpp"
#include "core/version.hpp"
#include "net/codec.hpp"
#include "report.hpp"
#include "replication/delta.hpp"
#include "waves/director.hpp"

namespace ac::server {
namespace {

std::uint32_t wallMs32(std::uint64_t nowMs) noexcept { return static_cast<std::uint32_t>(nowMs); }

// S13 §5 的工作量样本要亚毫秒分辨率：整毫秒四舍五入会把 <1 ms 的房间更新压成恒 0（云端复验观察项）。
double millisBetween(std::chrono::steady_clock::time_point from,
                     std::chrono::steady_clock::time_point to) noexcept {
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(to - from).count();
  return micros <= 0 ? 0.0 : static_cast<double>(micros) / 1000.0;
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
  dataDir_ = config_.dataDir.empty() ? ac::persist::dataDirFromEnv() : config_.dataDir;
  store_ = ac::persist::openMatchStore(dataDir_, error);
  if (store_ == nullptr) return false;

  if (!udp_.bind(config_.udpPort)) {
    if (error != nullptr) *error = "udp bind failed on port " + std::to_string(config_.udpPort);
    return false;
  }
  udpPort_ = udp_.boundPort();

  startMs_ = ac::core::nowMs();
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
    const ac::http::HttpListenerOptions httpOptions{config_.httpBindIpv4, config_.trustLoopbackProxy};
    if (!http_.start(config_.httpPort, &httpState_, httpDeps, error, httpOptions)) {
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
  // 进程收尾：对局已结束但没轮到下一拍 ensureMatchRunning（例如 --minutes 恰好到时）时补一次落盘。
  flushMatchOutcome();
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
                           std::size_t size, std::uint64_t nowMs, bool isReassembled) {
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
    // 客户端方向没有的快照/事件/MatchState：非法方向，丢弃并计数（Fragment 在白名单里，见下）。
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const std::uint8_t* payload = ac::net::packetPayload(info, bytes, size);
  if (payload == nullptr) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const std::size_t payloadBytes = size - info.payloadOffset;
  const auto type = static_cast<ac::net::PacketType>(info.header.type);
  if (type == ac::net::PacketType::kResume) {
    handleResume(from, info.header.session, bytes, size, nowMs);
    return;
  }
  if (type == ac::net::PacketType::kHello) {
    if (info.header.session != 0u) {
      ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
      return;
    }
    handleHello(from, bytes, size, nowMs);
    return;
  }
  const Client* sender = findClientByTransport(info.header.session);
  const auto validation = handshake_.validateSession(info.header.session);
  if (sender == nullptr || sender->endpoint != from || !validation.isAccepted ||
      validation.isGracePeriod) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  // §5.2：任何带在册 session 的包都是存活证据。命令是否被采纳（tick/seq 校验）是另一回事——
  // 以前只有 Hello/Resume 会刷新心跳，于是「命令被校验拒掉」会顺带把会话判死（实测 soak 33s 后房间停摆）。
  if (ac::net::SessionRecord* record = handshake_.sessions().find(info.header.session);
      record != nullptr && record->phase != ac::net::SessionPhase::kReleased) {
    record->keepAlive.onAnyPacket(wallMs32(nowMs));
  }
  // §5.3 的服务端半边：**必须回 ack**。客户端把 Command/Join/Resume/KeepAlive/Disconnect 都算同一条
  // 命令流（`UdpTransport.StreamOf`），所以这些带 reliable ext 的入站包共用同一个接收窗口；回执位图
  // 随 MatchState 下行（客户端同样把 MatchState 归到命令流上 —— 只有那条流上的 ack 会被它采纳）。
  // 不回 ack 的后果是联调实测到的：客户端每条命令重传到耗尽 → `OnRetransmitExhausted` → Reconnecting，
  // retx 一路涨到数千、RTT 恒 0（没有 ack 就没有往返样本）。
  if (info.hasReliableExt) {
    if (Client* sender = findClientByTransport(info.header.session); sender != nullptr) {
      (void)ac::net::ackOnReceive(sender->commandRecv, info.reliableExt.msgId);
      // 收到可靠包就记一笔"该回执了"：本 poll 结束时合并成一帧 KeepAlive 发回去（§5.6）。
      sender->ackDue = true;
    }
  }
  switch (static_cast<ac::net::PacketType>(info.header.type)) {
    case ac::net::PacketType::kCommand:
      handleCommand(info.header.session, bytes, size, payloadBytes, nowMs);
      break;
    case ac::net::PacketType::kJoin:
      // ADR-009「握手时序」的 type 11：昵称上报。方向白名单（security::isClientToServerType）
      // 收它，所以必须在这里被处置，否则会落到 default 计成丢弃帧。
      handleJoin(info.header.session, bytes, size);
      break;
    case ac::net::PacketType::kUpgradeSelect:
      // S16 type 12：波间购买升级。同样在方向白名单里，必须在此处置（否则计成丢弃帧）。
      handleUpgradeSelect(info.header.session, bytes, size);
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
    case ac::net::PacketType::kFragment:
      // §5.1 的 type 9 在方向白名单里（`security::isClientToServerType`）⇒ 必须在这里被处置，
      // 不能落到 default 计丢弃帧（S14 §15.3-20 的缺陷）。
      if (isReassembled) {
        ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
        break;
      }
      handleFragment(from, info, payload, payloadBytes);
      break;
    default:
      ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
      break;
  }
}

void Runtime::handleFragment(const ac::net::Endpoint& from, const ac::net::PacketInfo& info,
                             const std::uint8_t* payload, std::size_t payloadBytes) {
  if (payload == nullptr || !info.hasFragmentHeader) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  // 重组键 = (session, kFragment, fragId)：线上 type 恒为 9（ADR-009 的分片头裁定段），
  // fragId 在会话内全局唯一，所以快照/事件通道共用同一命名空间。
  const ac::net::FragmentKey key{
      info.header.session, static_cast<std::uint8_t>(ac::net::PacketType::kFragment),
      info.fragment.fragId};
  const std::uint32_t nowTick =
      room_ != nullptr && room_->world != nullptr ? room_->world->tick : 0u;
  const ac::net::Reassembler::Status status =
      reassembler_.add(key, info.fragment.fragIndex, info.fragment.fragCount,
                       std::span<const std::uint8_t>(payload, payloadBytes), nowTick,
                       reassemblyBuffer_);
  if (status == ac::net::Reassembler::Status::kBadValue) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;  // 整组已丢弃（片数不一致 / index 越界），本片不再计第二次
  }
  if (status != ac::net::Reassembler::Status::kComplete) return;  // 未收齐：静默挂起
  // 收齐：重组结果本身就是一帧完整的 v2 包（逻辑消息），重走一遍分派。
  handlePacket(from, reassemblyBuffer_.data(), reassemblyBuffer_.size(), lastPollMs_,
               /*isReassembled=*/true);
}

void Runtime::handleHello(const ac::net::Endpoint& from, const std::uint8_t* frame,
                          std::size_t size, std::uint64_t nowMs) {
  const ac::net::DecodeResult<ac::net::HelloPayload> hello = ac::net::decodeHello(frame, size);
  if (!hello.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const ac::net::HandshakeOutcome outcome =
      handshake_.onHello(hello.value.nonce, hello.value.token, wallMs32(nowMs),
                         (static_cast<std::uint64_t>(from.ipv4) << 16u) | from.port);
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
  bool admitted = true;
  if (room_ != nullptr && !ac::room::isInRoom(client->session)) {
    // ADR-012：准入结果不得静默丢弃 —— 被拒（满员 / 对局进行中）时在 HelloAck 之后补一帧 Disconnect。
    admitted =
        ac::room::roomJoin(*room_, client->session, nowMs) == ac::room::JoinOutcome::kOk;
  }
  client->session.token = record->token;
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
  if (!admitted) sendRoomUnavailable(from, outcome.session);
}

void Runtime::sendRoomUnavailable(const ac::net::Endpoint& to, std::uint16_t session) {
  std::uint8_t buffer[32] = {};
  const ac::net::DisconnectPayload body{
      static_cast<std::uint8_t>(ac::net::DisconnectReason::kRoomUnavailable)};
  const ac::net::EncodeResult encoded = ac::net::encodeDisconnect(
      makeHeader(ac::net::PacketType::kDisconnect, session, 0u), ac::net::ReliableExt{}, body,
      buffer, sizeof(buffer));
  if (encoded.isOk) {
    (void)udp_.sendTo(to, std::span<const std::uint8_t>(buffer, encoded.bytes));
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
  Client* resumed = findClientByTransport(session);
  if (resumed == nullptr || (!validation.isGracePeriod && resumed->endpoint != from)) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const ac::net::HandshakeOutcome outcome =
      handshake_.onResume(session, resume.value.token, wallMs32(nowMs));
  if (!outcome.isAccepted) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const auto packet = ac::net::decodePacket(frame, size);
  if (packet.isOk && packet.value.hasReliableExt) {
    (void)ac::net::ackOnReceive(resumed->commandRecv, packet.value.reliableExt.msgId);
    resumed->ackDue = true;
  }
  if (!outcome.isResumed) return;
  resumed->endpoint = from;
  resumed->isFullSnapshotDue = outcome.isFullSnapshotDue;
  ac::metrics::addCounter(counters_, ac::metrics::CounterId::kGraceReconnects, 1u);
  if (room_ != nullptr) {
    for (std::size_t i = 0u; i < ac::room::kMaxPlayersPerRoom; ++i) {
      ac::room::Session* existing = room_->sessions[i];
      if (existing == nullptr) continue;
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
  // §5.5 的窗口去重：`noteCommandSequence` 的契约（`security_test.cpp:security_dedup_window_eviction`）
  // 是「命中窗口返回 true = 重复，丢」，这里此前写成 `!`，方向反了 —— 结果**每条命令的首次发送都被
  // 当重复丢掉**，只有可靠层的重传（同 seq 第二次）才会被应用：移动/开火/准备位全部迟一个 RTT 生效，
  // `ac_dropped_frames_total` 还把这当成"重复"在涨。联调实测（Ready 位第一帧就被丢）抓到这条。
  if (ac::security::noteCommandSequence(client->dedup, decoded.value.seq)) {
    ac::security::noteValidateFailure(&counters_, ac::security::ValidateReason::kDuplicateSequence);
    return;
  }
  ac::security::ClampReport report{};
  const ac::sim::Command command = ac::security::sanitizeCommand(toSimCommand(decoded.value), report);
  // ADR-013：就绪位来自客户端命令里的 Ready 位（config::kButtonReady = 0x80）。此前服务端从不读它，
  // 只靠 `--auto-ready` 强制就绪，于是"大厅里按准备 → 房主开局"这条产品路径根本不存在（联调实测红）。
  // kLobby 收（大厅开局）；S16 起 kIntermission 也收：清波进波间时 ready 已重置，重按准备可提前开波
  // （5s 最短停留保留）。`roomSetReady` 幂等且只在真变化时广播。
  if ((room_->phase == ac::room::MatchPhase::kLobby ||
       room_->phase == ac::room::MatchPhase::kIntermission) &&
      ac::room::isInRoom(client->session)) {
    const bool wantReady = (command.buttons & ac::config::kButtonReady) != 0u;
    // 武器预览沿用大厅口径：只在带 SwitchWeapon 位时把 switchTo 当选择，否则传越界值让 roomSetReady 不碰武器。
    const std::uint8_t weapon = (command.buttons & ac::config::kButtonSwitchWeapon) != 0u
                                    ? command.switchTo
                                    : static_cast<std::uint8_t>(0xFFu);
    (void)ac::room::roomSetReady(*room_, client->session, wantReady, weapon, deps_);
  }
  if (!ac::room::roomApplyCommand(*room_, client->session, command)) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
  }
}

void Runtime::handleJoin(std::uint16_t session, const std::uint8_t* frame, std::size_t size) {
  // §5.2：非 Hello 包必须带在册 session。
  if (!handshake_.validateSession(session).isAccepted) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const ac::net::DecodeResult<ac::net::JoinPayload> decoded = ac::net::decodeJoin(frame, size);
  if (!decoded.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  Client* client = findClientByTransport(session);
  if (client == nullptr) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  // 净化器（setSessionName）是昵称字节合法性的唯一来源：非法 UTF-8 / 全被过滤 / 净化后越界
  // 一律**保留旧昵称**，这条报文本身不算坏包（重发幂等，客户端可反复上报）。
  if (!ac::room::setSessionName(client->session, decoded.value.name)) return;
  // 开局的战绩记录也要跟着改：matches.ndjson 与结算榜读的是记录里的名字，不是会话里的。
  // joinMatchRecord 是 upsert（找不到才建），所以这里等价于"改名"，不新增 API。
  if (room_ != nullptr) {
    ac::room::joinMatchRecord(*room_, client->session.pid, client->session.name,
                              client->session.nameBytes);
  }
}

// S16 type 12 UpgradeSelect：波间购买升级（可靠 C→S，载荷 1 字节 upgradeId）。
// 语义校验（缺一即拒收，不广播）：phase==intermission、upgradeId 合法、有剩余升级点、
// 目标等级未满。成功：points−1、level+1、立即广播 MatchState。
void Runtime::handleUpgradeSelect(std::uint16_t session, const std::uint8_t* frame, std::size_t size) {
  // §5.2：非 Hello 包必须带在册 session。
  if (!handshake_.validateSession(session).isAccepted) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const ac::net::DecodeResult<ac::net::UpgradeSelectPayload> decoded =
      ac::net::decodeUpgradeSelect(frame, size);
  if (!decoded.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  Client* client = findClientByTransport(session);
  if (client == nullptr || room_ == nullptr || room_->world == nullptr ||
      !ac::room::isInRoom(client->session)) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const std::uint8_t id = decoded.value.upgradeId;
  if (room_->phase != ac::room::MatchPhase::kIntermission || id >= ac::config::kUpgradeCount) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  ac::sim::Entity* entity = ac::room::playerEntityAt(*room_, client->session.pid);
  if (entity == nullptr || entity->upgrade.points == 0u) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  std::uint8_t* level = nullptr;
  switch (id) {
    case ac::config::kUpgradeDamage: level = &entity->upgrade.damageLevel; break;
    case ac::config::kUpgradeSpeed: level = &entity->upgrade.speedLevel; break;
    case ac::config::kUpgradeReload: level = &entity->upgrade.reloadLevel; break;
    case ac::config::kUpgradeReserve: level = &entity->upgrade.reserveLevel; break;
    default: break;
  }
  if (level == nullptr || *level >= ac::config::kUpgradeMaxLevel) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  entity->upgrade.points -= 1u;
  *level += 1u;
  (void)ac::room::broadcastMatchState(*room_, deps_);
}

void Runtime::pollOnce(std::uint64_t nowMs) {
  if (!isRunning_) return;
  lastPollMs_ = nowMs;
  receivePackets(nowMs);
  // §5.6：收完包就处理心跳/回执（同一 poll 内的多个入站包合并成一帧，不按包放大出站量）。
  flushHeartbeats(nowMs);

  const ac::net::SessionTick sessionTick = handshake_.sessions().tick(wallMs32(nowMs));
  if (sessionTick.wentOffline > 0u) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kGraceStarts,
                            sessionTick.wentOffline);
    if (room_ != nullptr) {
      for (Client& client : clients_) {
        if (!client.isUsed || !ac::room::isInRoom(client.session)) continue;
        const auto* record = handshake_.sessions().find(client.transportId);
        if (record != nullptr && record->isResumable()) {
          (void)ac::room::roomDisconnect(*room_, client.session, nowMs);
        }
      }
    }
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
    const ac::room::MatchPhase phaseBeforeUpdate = room_->phase;
    bool isEpochJustStarted = false;
    const auto workStart = std::chrono::steady_clock::now();
    (void)ac::room::updateRoom(*room_, deps_, nowMs);
    // S04 §5.4：分片组 60 tick 未收齐即整组丢弃（与 Reassembler::add 用同一个 tick 时钟）。
    if (room_->world != nullptr) (void)reassembler_.expire(room_->world->tick);
    topUpSheep();
    const double workMs = millisBetween(workStart, std::chrono::steady_clock::now());
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
      // 本轮房间更新的总耗时按本轮执行的 tick 数均摊：只有最后一份吃掉整数除法的余数。
      const double share = i + 1u == advanced ? workMs : workMs / static_cast<double>(advanced);
      (void)ac::core::noteTickRun(scheduler_, nowMs, share, &counters_, &gauges_);
    }
    // S13 §5：报告里的 ticks 组以「本局首个 tick」为原点。loading→playing 的那一拍就是本局首个 tick
    //（世界从这一拍开始步进、durationMs 也从这一拍起算），把采样原点钉在它的**理想时刻**上：
    // 房间累加器的残差正是这一拍迟到的那一段，扣掉它之后各 tick 的误差就是「相对 50 ms 网格迟到了多久」。
    if (advanced > 0u && phaseBeforeUpdate == ac::room::MatchPhase::kLoading &&
        room_->phase == ac::room::MatchPhase::kPlaying) {
      const auto residual =
          static_cast<std::uint64_t>(room_->accumulatorMs > 0 ? room_->accumulatorMs : 0);
      ac::core::beginTickEpoch(scheduler_, nowMs > residual ? nowMs - residual : nowMs, 1u);
      isEpochJustStarted = true;
    }
    // §5 的漂移：本拍读数进 ac_sim_drift_ms 量值表，|漂移| 的本局最大值进报告（ticks.simDriftMsMax）。
    // 开球那一拍复位（本局自己的最大值），此后只增不减 —— 这样它不随进程存活时间、也不随上一局累计。
    const double driftMs = simDriftMs();
    ac::metrics::setGaugeIf(&gauges_, ac::metrics::GaugeId::kSimDriftMs, driftMs);
    if (scheduler_.epoch.isActive) {
      const double absDriftMs = std::fabs(driftMs);
      simDriftAbsMaxMs_ = isEpochJustStarted ? absDriftMs
                                             : (absDriftMs > simDriftAbsMaxMs_ ? absDriftMs : simDriftAbsMaxMs_);
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

void Runtime::flushMatchOutcome() {
  if (room_ == nullptr || !room_->match.hasResult) return;
  // 复用既有序列化：房间结算记录 → 战绩记录（字段名与 §5 的 schema 一一对应）。
  const ac::persist::MatchResultRecord record = ac::persist::toStoredRecord(room_->match.lastResult);
  if (record.matchId.empty() || record.matchId == lastStoredMatchId_) return;
  const bool isStored = store_ != nullptr && store_->append(record);
  if (!isStored) {
    // 失败不吞：走既有的 store.error 名，并把路径与当前常驻条数一并记下；此后 /metrics 的
    // ac_records_retained 不增长就是可观测信号（不新增计数名，§5 的名字表是冻结契约）。
    ac::log::event(ac::log::Level::error, ac::log::EventName::kStoreError, {},
                   {ac::log::DetailField("matchId", std::string_view(record.matchId)),
                    ac::log::DetailField("path", store_ == nullptr ? std::string_view{}
                                                                   : store_->path()),
                    ac::log::DetailField("retained",
                                         static_cast<std::uint64_t>(store_ == nullptr
                                                                        ? 0u
                                                                        : store_->recordCount()))});
  }
  lastStoredMatchId_ = record.matchId;  // 无论成败都只尝试一次（重置会清掉 hasResult）
  // 单局诊断报告：与战绩各自独立落盘，报告侧失败由 writeReport 记 report.write_failed。
  ac::report::MatchRunSummary summary{};
  summary.matchId = record.matchId;
  summary.durationMs = record.durationMs;
  // 本局 |漂移| 的最大值（毫秒，取整）：开球那一拍起累计，见 pollOnce；不是落盘瞬间的瞬时值。
  summary.simDriftMsMax = static_cast<std::int64_t>(std::lround(simDriftAbsMaxMs_));
  for (uint8_t i = 0u; i < room_->match.recordCount && i < ac::room::kMaxPlayersPerRoom; ++i) {
    const ac::room::PlayerStats& stats = room_->match.records[i].stats;
    summary.shotsFiredTotal += stats.shotsFired;
    summary.hitsTotal += stats.hits;
  }
  summary.peakEntities = room_->match.counters.peakEntities;
  summary.peakPlayers = room_->match.counters.peakPlayers;
  std::string reportError;
  const ac::report::MatchDiagnostics diagnostics =
      ac::report::buildMatchDiagnostics(summary, counters_, gauges_, scheduler_);
  (void)ac::report::writeReport(dataDir_, diagnostics, &reportError);
}

void Runtime::ensureMatchRunning(std::uint64_t nowMs) {
  if (room_ == nullptr) return;
  // 「已结束、还没重置」是结算记录唯一可读的时刻：先落盘（战绩 + 报告），再走重置。
  flushMatchOutcome();
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
    // 这里不再重锚调度器：tryStartMatch 只把相位推到 kLoading（loading 倒计时 1500 ms），
    // kPlaying 是 30 拍之后在 roomTick 内部翻的 —— 旧代码在这里判 kPlaying，永远不成立，
    // 于是 tick 时序一路按「进程首个 tick」取样（云端复验实测 jitterP50/P95 ≈ 2055 ms）。
    // 本局的调度原点改由 pollOnce 在 loading→playing 的那一拍上打（beginTickEpoch）。
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
    input.seq = ++client->snapshotSeq;  // §5.2：快照通道自己的序号（与 MatchState 的分开，见 runtime.hpp）
    // §5.3 的「最后被采纳的命令 seq」= 本会话最近一次真正执行过的命令载荷 seq。客户端拿它
    // `buffer.AckUpTo(...)` 裁剪本机输入缓冲（Reconciler/C06 §5(d)）：恒填 0 会让缓冲只进不出
    // —— 联调里客户端跑几万 tick 后这条线一直在长（服务端一直在按真值执行，只是没回报）。
    input.lastAckedSeq = session->command.seq;
    // S03 §5.4 + S10 §5.7-5：本 tick 的事件条目随帧下发（生产在房间侧，见 room/event_map.*）。
    input.events = room.eventEntries;
    input.eventCount = room.eventEntryCount;
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
    // S03 §5.3/§5.4：本代帧真的发出去了 ⇒ 确认本帧带出的条目（按重传窗口递减/出队）。被跳过的 tick
    // （档位降档 / 背压 / 编码失败 / 发送失败）不走到这里，未确认条目留在房间队列里下一帧续投。
    ac::room::confirmFrameEvents(room, room.eventFrameGeneration, outcome.eventCount);
    client->bytesOut += outcome.bytes;
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kBytesOut,
                            static_cast<std::uint64_t>(outcome.bytes));
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kFramesOut, 1u);
    // S03 §5.4 的事件条目随帧上线：按实际发出的条数记（每客户端各记一次）。
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kEventsSent,
                            static_cast<std::uint64_t>(outcome.eventCount));
  }
}

void Runtime::onSendMatchState(ac::room::Session& session,
                               const ac::net::MatchState& state) noexcept {
  Client* client = findClientByTransport(static_cast<std::uint16_t>(session.id));
  if (client == nullptr || !client->isUsed) return;
  const ac::net::PacketHeader header =
      makeHeader(ac::net::PacketType::kMatchState, client->transportId, ++client->matchStateSeq);
  ac::net::ReliableExt ext{};
  // 包头 seq（每条类型独立的流）与 msgId（控制通道共用的可靠消息号）是两回事，见 runtime.hpp。
  ext.msgId = ++client->seq;
  // §5.3：回执位图（本客户端命令流的接收窗口）随 MatchState 下行。客户端把 MatchState 归到命令流，
  // 所以这是它唯一会采纳的 ack 载体；1Hz 的回执足以让客户端在 RTO 表耗尽（约 2.6s）之前收到确认。
  ext.ackBase = client->commandRecv.ackBase;
  ext.ackBits = client->commandRecv.ackBits;
  std::uint8_t buffer[ac::room::kMatchStateFrameMaxBytes] = {};
  const ac::net::EncodeResult encoded =
      ac::net::encodeMatchState(header, ext, state, buffer, sizeof(buffer),
                                static_cast<std::uint16_t>(session.pid));
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

void Runtime::sendKeepAlive(Client& client) noexcept {
  const ac::net::PacketHeader header =
      makeHeader(ac::net::PacketType::kKeepAlive, client.transportId, ++client.keepAliveSeq);
  ac::net::ReliableExt ext{};
  // msgId 与 MatchState 共用一条控制流（§5.2「msgId 与通道 seq 相互独立」）：客户端按 msgId 去重与
  // ack，两条流混用会让它把对方当成重复/乱序。**必须逐帧推进** —— 写死一个常数的话每一帧心跳都被
  // 客户端判成重复，连里面的 ack 位图一起丢掉（联调实测：dup 31、rttMs 485 完全等于 MatchState 的 1Hz）。
  ext.msgId = ++client.seq;
  ext.ackBase = client.commandRecv.ackBase;
  ext.ackBits = client.commandRecv.ackBits;
  std::uint8_t buffer[ac::net::kCommonHeaderBytes + ac::net::kReliableExtBytes] = {};
  const ac::net::EncodeResult encoded =
      ac::net::encodeKeepAlive(header, ext, buffer, sizeof(buffer));
  if (!encoded.isOk) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  const int sent =
      udp_.sendTo(client.endpoint, std::span<const std::uint8_t>(buffer, encoded.bytes));
  if (sent <= 0) {
    ac::metrics::addCounter(counters_, ac::metrics::CounterId::kDroppedFrames, 1u);
    return;
  }
  client.bytesOut += encoded.bytes;
  ac::metrics::addCounter(counters_, ac::metrics::CounterId::kBytesOut,
                          static_cast<std::uint64_t>(encoded.bytes));
  ac::metrics::addCounter(counters_, ac::metrics::CounterId::kFramesOut, 1u);
}

void Runtime::flushHeartbeats(std::uint64_t nowMs) noexcept {
  const std::uint32_t now = wallMs32(nowMs);
  for (Client& client : clients_) {
    if (!client.isUsed) continue;
    const bool isDue = client.heartbeat.due(now);
    if (!isDue && !client.ackDue) continue;
    sendKeepAlive(client);
    client.ackDue = false;
    // 纯回执不能顶掉心跳的周期：只有真到点的那一帧才推进 lastSendMs（§5.6 的 500ms 判据不受回执影响）。
    if (isDue) client.heartbeat.markSent(now);
  }
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
  out.ticks = scheduler_.tickIndex;  // 进程级累计（/health 的 ticks）；报告里的 ticks.total 是 epoch 口径
  out.clients = clientCount();
  out.rooms = roomCount();
  out.players = room_ == nullptr ? 0u : ac::room::connectedSessionCount(*room_);
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

// §5 的 ac_sim_drift_ms：模拟已走时长 − 墙上已走时长（负 = 模拟落后）。本局口径，只在一局之内有值
// （epoch 未打基准 = 没开球，报 0 而不是拿上个进程/上一局的账充数）。
// 取法与 S12 §5 等价但更稳：同一条 50ms 网格上，墙钟已走 = 已执行 tick 数 × 50 + 房间累加器残差，
// 所以「模拟 − 墙钟」就是残差取负 —— 残差按房间自己的网格算（每 tick 扣 50、每拍加实际时长），
// 对这个网格的任何重锚（开球对齐、换局、无人连接时的暂停）都自动跟随。
// 反面教材（接线首版实测）：用「epoch 已执行 tick 数 × 50 − 从 epoch 原点起的墙钟」——只要房间的
// 网格在中途重锚（同房间第二局的 kLoading、掉线暂停），这个差就把重锚前的那段时间算成漂移，
// 短对局里量到 80ms、掉线段里量到数千毫秒，正好把 G6 的 |漂移| ≤ 50ms 判据顶红。
double Runtime::simDriftMs() const noexcept {
  if (!scheduler_.epoch.isActive || room_ == nullptr) return 0.0;
  return -static_cast<double>(room_->accumulatorMs);
}

void Runtime::publishMetrics(std::uint64_t nowMs) {
  process_.protocol = ac::version::kProtocol;
  process_.tickMs = ac::version::kTickMs;
  process_.rooms = static_cast<std::uint32_t>(roomCount());
  process_.connections = static_cast<std::uint32_t>(clientCount());
  // 诊断口径必须如实：activePlayerCount 对空房夹到 1（那是给波次预算用的夹取，见 waves.hpp），
  // 拿它渲染 /health 与 ac_players 会让"没人"和"1 个人"无法区分（联调时脚本就被它骗过一次）。
  process_.players = room_ == nullptr ? 0u : ac::room::connectedSessionCount(*room_);
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
  // G8 的事件丢弃口径 = sim 事件缓冲溢出（S05 §5.1）+ 房间条目缓冲也满时的真丢（room.eventOverflowCount）；
  // 房间不存在时保持上一拍读数（与旧口径一致，不把「没有房间」算成丢弃）。
  std::size_t dropped = eventsDroppedSeen_;
  if (room_ != nullptr && room_->world != nullptr) {
    dropped = static_cast<std::size_t>(room_->world->stats.eventsDropped) +
              static_cast<std::size_t>(room_->eventOverflowCount);
  }
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
