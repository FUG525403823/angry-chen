#pragma once
// S04 §5.2/§5.5：会话短 ID、握手状态机（Hello/HelloAck/Resume/Disconnect）与重连令牌。
#include <cstddef>
#include <cstdint>
#include <vector>

#include "net/keepalive.hpp"
#include "net/reliability.hpp"

namespace ac::net {

// §5.5 冻结 1–7（与客户端逐字一致）。
enum class DisconnectReason : uint8_t {
  kVersionMismatch = 1u,
  kTokenInvalid = 2u,
  kTimeout = 3u,
  kServerShutdown = 4u,
  kMalformedPacket = 5u,
  kRateLimited = 6u,
  kSlowConsumer = 7u,
};

// §5.1/§5.2：同时在册会话上限（含宽限期）；打满后按 §8 先驱逐最早进宽限期的会话。
inline constexpr std::size_t kSessions = 256u;
inline constexpr uint16_t kSessionIdMax = 65535u;
// §5.5：客户端 Hello 按 1s × 5 次重发；同一 clientNonce 在此时窗内视为同一次握手。
inline constexpr uint32_t kHelloDedupMs = 5000u;

// 重连令牌 = salt ^ clientNonce（§5.5）：客户端本地算，服务器按会话记录复算比对。
uint32_t reconnectTokenFor(uint32_t clientNonce, uint32_t salt) noexcept;

// §5.2：version != 1 一律回 Disconnect(reason = 1)。
bool isVersionAccepted(uint8_t version) noexcept;

enum class SessionPhase : uint8_t {
  kAllocated = 0u,
  kConnected = 1u,
  kGracePeriod = 2u,
  kReleased = 3u,
};

struct SessionRecord {
  uint16_t id = 0u;
  uint32_t clientNonce = 0u;
  uint32_t salt = 0u;
  uint32_t token = 0u;
  uint32_t createdMs = 0u;
  SessionPhase phase = SessionPhase::kReleased;
  KeepAliveTimer keepAlive{};
  GraceTimer grace{};

  // 各通道独立的接收侧 ack 状态与发送侧重传表（§5.2/§5.3）。
  ReliableState recvCommand{};
  ReliableState recvEvent{};
  ReliableChannel sendCommand{};
  ReliableChannel sendEvent{};

  // §5.2：三个通道各一条发送/接收序号（Snapshot 用 seq 丢弃过期快照）。
  ChannelSeq seqCommand{};
  ChannelSeq seqEvent{};
  ChannelSeq seqSnapshot{};

  bool isConnected() const noexcept { return phase == SessionPhase::kConnected; }
  bool isResumable() const noexcept { return phase == SessionPhase::kGracePeriod; }

  // §5.6：返回 true 表示本 tick 刚判断线并进入宽限期（世界侧置 idle 由后续计划做）。
  bool tickLiveness(uint32_t nowMs);
};

struct SessionTick {
  std::size_t wentOffline = 0u;
  std::size_t released = 0u;
};

class SessionTable {
 public:
  // §5.2：next = (next % 65535) + 1，命中在用 ID 时继续 +1（最多试 65535 次）；0 表示分配失败。
  uint16_t allocate(uint32_t nowMs);
  SessionRecord* find(uint16_t id);
  const SessionRecord* find(uint16_t id) const;
  bool release(uint16_t id);

  // §5.5：Hello 重发去重——同一 clientNonce 在 kHelloDedupMs 内命中已建会话。
  SessionRecord* findByNonce(uint32_t clientNonce, uint32_t nowMs) noexcept;

  // 每 tick：刷新断线判定与宽限期到期（到期即释放名额）。
  SessionTick tick(uint32_t nowMs);

  std::size_t size() const noexcept { return sessions_.size(); }
  std::size_t connectedCount() const noexcept;
  std::size_t graceCount() const noexcept;

  // 会话层独立计数器（不消费 ai/spawn/fx 任一 RNG 流，否则对拍不可复现）：确定性派生，非随机。
  uint32_t nextSalt() noexcept;
  void clear();

 private:
  // §8 风险表：名额不足时驱逐最早进入宽限期的会话；没有宽限期会话可驱逐才算打满。
  bool releaseOldestGrace() noexcept;

  std::vector<SessionRecord> sessions_;
  uint16_t nextId_ = 0u;
  uint32_t saltCounter_ = 0u;
};

// §5.2：非 Hello 包的 session 校验（失败 = 丢弃并计 kBadSession，不回包）。
// 宽限期会话仍持有绑定，因此不算 kBadSession；isGracePeriod = true 时由调用方决定是否按
// C03 §5.3 的"Zombie 收到本会话任何包即回 Connected"复活（S04 §5.6：世界侧处置留给后续计划）。
struct SessionValidation {
  bool isAccepted = false;
  bool isBadSession = false;
  bool isGracePeriod = false;
};

struct HandshakeOutcome {
  bool isAccepted = false;
  bool isHelloAckDue = false;
  bool isDisconnectDue = false;
  bool isResumed = false;
  bool isFullSnapshotDue = false;  // §5.5：Resume 成功补一次 baselineTick = 0 的全量快照
  DisconnectReason reason = DisconnectReason::kVersionMismatch;
  uint16_t session = 0u;
  uint32_t salt = 0u;
  uint32_t token = 0u;
};

class HandshakeServer {
 public:
  // Hello：session 必须为 0，合法即分配会话、记录 nonce、派生 salt 并回 HelloAck。
  // 同一 clientNonce 在 kHelloDedupMs 内重发（§5.5 的客户端重试）复用同一会话，不占新名额。
  HandshakeOutcome onHello(uint32_t clientNonce, uint32_t reconnectToken, uint32_t nowMs);

  // Resume：仅宽限期会话可恢复；令牌不符或已释放/超期一律 Disconnect(reason = 2)。
  HandshakeOutcome onResume(uint16_t session, uint32_t reconnectToken, uint32_t nowMs);

  SessionValidation validateSession(uint16_t session) const;

  SessionTable& sessions() noexcept { return sessions_; }
  const SessionTable& sessions() const noexcept { return sessions_; }

 private:
  SessionTable sessions_{};
};

}  // namespace ac::net
