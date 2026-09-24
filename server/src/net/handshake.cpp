// S04 §5.2/§5.5：会话表与握手状态机实现。
#include "net/handshake.hpp"

#include "net/wire.hpp"

namespace ac::net {

uint32_t reconnectTokenFor(uint32_t clientNonce, uint32_t salt) noexcept {
  return salt ^ clientNonce;
}

bool isVersionAccepted(uint8_t version) noexcept {
  return version == kProtocolVersion;
}

bool SessionRecord::tickLiveness(uint32_t nowMs) {
  if (phase != SessionPhase::kConnected) return false;
  if (!keepAlive.isOffline(nowMs)) return false;
  phase = SessionPhase::kGracePeriod;
  grace.enter(nowMs);
  return true;
}

uint16_t SessionTable::allocate(uint32_t nowMs) {
  if (sessions_.size() >= kSessions && !releaseOldestGrace()) return 0u;
  for (uint32_t attempt = 0u; attempt < kSessionIdMax; ++attempt) {
    nextId_ = static_cast<uint16_t>((nextId_ % kSessionIdMax) + 1u);
    if (find(nextId_) != nullptr) continue;  // 命中在用 ID：继续 +1
    SessionRecord record{};
    record.id = nextId_;
    record.createdMs = nowMs;
    record.phase = SessionPhase::kAllocated;  // 握手完成（onHello 成功）后转 kConnected
    record.keepAlive = KeepAliveTimer{nowMs};
    sessions_.push_back(std::move(record));
    return nextId_;
  }
  return 0u;  // 65535 次都撞上：不可能（上限 256 个在册会话）
}

bool SessionTable::releaseOldestGrace() noexcept {
  SessionRecord* oldest = nullptr;
  for (SessionRecord& record : sessions_) {
    if (!record.isResumable()) continue;
    if (oldest == nullptr || record.grace.startMs() < oldest->grace.startMs()) oldest = &record;
  }
  if (oldest == nullptr) return false;
  const uint16_t id = oldest->id;
  release(id);
  return true;
}

SessionRecord* SessionTable::findByNonce(uint32_t clientNonce, uint32_t nowMs) noexcept {
  for (SessionRecord& record : sessions_) {
    if (record.clientNonce != clientNonce) continue;
    if (record.phase == SessionPhase::kReleased) continue;
    if (nowMs - record.createdMs > kHelloDedupMs) continue;
    return &record;
  }
  return nullptr;
}

SessionRecord* SessionTable::find(uint16_t id) {
  for (SessionRecord& record : sessions_) {
    if (record.id == id) return &record;
  }
  return nullptr;
}

const SessionRecord* SessionTable::find(uint16_t id) const {
  for (const SessionRecord& record : sessions_) {
    if (record.id == id) return &record;
  }
  return nullptr;
}

bool SessionTable::release(uint16_t id) {
  for (std::size_t i = 0u; i < sessions_.size(); ++i) {
    if (sessions_[i].id == id) {
      sessions_.erase(sessions_.begin() + static_cast<std::ptrdiff_t>(i));
      return true;
    }
  }
  return false;
}

SessionTick SessionTable::tick(uint32_t nowMs) {
  SessionTick summary{};
  for (std::size_t i = 0u; i < sessions_.size();) {
    SessionRecord& record = sessions_[i];
    if (record.tickLiveness(nowMs)) ++summary.wentOffline;
    if (record.phase == SessionPhase::kGracePeriod && record.grace.isExpired(nowMs)) {
      record.phase = SessionPhase::kReleased;
      ++summary.released;
    }
    if (record.phase == SessionPhase::kReleased) {
      sessions_.erase(sessions_.begin() + static_cast<std::ptrdiff_t>(i));
      continue;
    }
    ++i;
  }
  return summary;
}

std::size_t SessionTable::connectedCount() const noexcept {
  std::size_t count = 0u;
  for (const SessionRecord& record : sessions_) {
    if (record.phase == SessionPhase::kConnected) ++count;
  }
  return count;
}

std::size_t SessionTable::graceCount() const noexcept {
  std::size_t count = 0u;
  for (const SessionRecord& record : sessions_) {
    if (record.phase == SessionPhase::kGracePeriod) ++count;
  }
  return count;
}

uint32_t SessionTable::nextSalt() noexcept {
  ++saltCounter_;
  uint32_t value = (saltCounter_ * 2654435761u) ^ 0x5BF03635u;
  if (value == 0u) value = 1u;  // 0 保留给"首次连接"，令牌里不用
  return value;
}

void SessionTable::clear() {
  sessions_.clear();
  nextId_ = 0u;
  saltCounter_ = 0u;
}

HandshakeOutcome HandshakeServer::onHello(uint32_t clientNonce, uint32_t reconnectToken,
                                          uint32_t nowMs) {
  // §5.5：Hello 的 reconnectToken 只在"首次连接填 0"时有意义，重连走 Resume；这里记账不使用。
  (void)reconnectToken;
  HandshakeOutcome outcome{};

  // §5.5：客户端会按 1s × 5 重发 Hello；同一 nonce 在 kHelloDedupMs 内复用已建会话（幂等 HelloAck）。
  if (SessionRecord* existing = sessions_.findByNonce(clientNonce, nowMs)) {
    if (existing->phase == SessionPhase::kAllocated) existing->phase = SessionPhase::kConnected;
    existing->keepAlive.onAnyPacket(nowMs);
    outcome.isAccepted = true;
    outcome.isHelloAckDue = true;
    outcome.session = existing->id;
    outcome.salt = existing->salt;
    outcome.token = existing->token;
    return outcome;
  }

  const uint16_t session = sessions_.allocate(nowMs);
  if (session == 0u) {
    // 在册会话打满（§8 风险表）：§5.5 没有专门的取值，按"服务端限流"语义拒收。
    outcome.isDisconnectDue = true;
    outcome.reason = DisconnectReason::kRateLimited;
    return outcome;
  }
  SessionRecord* record = sessions_.find(session);
  record->clientNonce = clientNonce;
  record->salt = sessions_.nextSalt();
  record->token = reconnectTokenFor(clientNonce, record->salt);
  record->phase = SessionPhase::kConnected;  // 握手成立：Alloc → Connected
  outcome.isAccepted = true;
  outcome.isHelloAckDue = true;
  outcome.session = session;
  outcome.salt = record->salt;
  outcome.token = record->token;
  return outcome;
}

HandshakeOutcome HandshakeServer::onResume(uint16_t session, uint32_t reconnectToken,
                                           uint32_t nowMs) {
  HandshakeOutcome outcome{};
  outcome.session = session;
  SessionRecord* record = sessions_.find(session);
  if (record == nullptr || !record->isResumable() || record->token != reconnectToken) {
    outcome.isDisconnectDue = true;
    outcome.reason = DisconnectReason::kTokenInvalid;  // 令牌不符 / 已释放 / 宽限期已过
    if (record != nullptr) {
      record->phase = SessionPhase::kReleased;
      sessions_.release(session);
    }
    return outcome;
  }
  record->phase = SessionPhase::kConnected;
  record->grace.clear();
  record->keepAlive.onAnyPacket(nowMs);
  outcome.isAccepted = true;
  outcome.isResumed = true;
  outcome.isFullSnapshotDue = true;
  outcome.salt = record->salt;
  outcome.token = record->token;
  return outcome;
}

SessionValidation HandshakeServer::validateSession(uint16_t session) const {
  SessionValidation validation{};
  const SessionRecord* record = sessions_.find(session);
  if (session == 0u || record == nullptr || record->phase == SessionPhase::kReleased) {
    validation.isBadSession = true;
    return validation;
  }
  validation.isAccepted = true;
  validation.isGracePeriod = record->isResumable();
  return validation;
}

}  // namespace ac::net
