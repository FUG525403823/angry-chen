// S04 §5.6：心跳与宽限期的纯逻辑实现（无 IO、无系统时钟）。
#include "net/keepalive.hpp"

namespace ac::net {

KeepAliveTimer::KeepAliveTimer(uint32_t nowMs) noexcept
    : lastRecvMs_(nowMs), lastSendMs_(nowMs), sentCount_(0u) {}

bool KeepAliveTimer::due(uint32_t nowMs) const noexcept {
  return nowMs - lastSendMs_ >= kKeepAliveMs;
}

void KeepAliveTimer::markSent(uint32_t nowMs) noexcept {
  lastSendMs_ = nowMs;
  ++sentCount_;
}

void KeepAliveTimer::onAnyPacket(uint32_t nowMs) noexcept { lastRecvMs_ = nowMs; }

bool KeepAliveTimer::isOffline(uint32_t nowMs) const noexcept {
  return nowMs - lastRecvMs_ >= kDisconnectMs;
}

std::size_t KeepAliveTimer::sentCount() const noexcept { return sentCount_; }
uint32_t KeepAliveTimer::lastRecvMs() const noexcept { return lastRecvMs_; }
uint32_t KeepAliveTimer::lastSendMs() const noexcept { return lastSendMs_; }

void GraceTimer::enter(uint32_t nowMs) noexcept {
  isActive_ = true;
  startMs_ = nowMs;
}

void GraceTimer::clear() noexcept {
  isActive_ = false;
  startMs_ = 0u;
}

bool GraceTimer::isActive() const noexcept { return isActive_; }

bool GraceTimer::isExpired(uint32_t nowMs) const noexcept {
  return isActive_ && (nowMs - startMs_ >= kGraceMs);
}

uint32_t GraceTimer::startMs() const noexcept { return startMs_; }

}  // namespace ac::net
