#pragma once
// S04 §5.1/§5.6：心跳、断线判定与宽限期计时。全部由调用方喂 nowMs：本文件不读系统时钟、不做 IO。
#include <cstddef>
#include <cstdint>

namespace ac::net {

// §5.1 常量表（逐字对齐 ADR-009）。单包 1200B / 分片上限 8 / 逻辑消息 9408B 在 S03 的 wire.hpp，
// 这里只放传输层自己的常量，避免同一常量两处定义。
inline constexpr uint32_t kKeepAliveMs = 500u;                      // 心跳周期
inline constexpr uint32_t kDisconnectMs = 3000u;                    // 未收到任何包即判断线
inline constexpr uint32_t kGraceMs = 30000u;                        // 宽限期，期间保留会话与房间名额
inline constexpr std::size_t kOutboundBacklogBytes = 65536u;         // 出站积压上限（达上限丢最旧快照，事件不丢）
inline constexpr std::size_t kSnapshotBudgetBytes = 1228u;           // 稳态快照目标上限（= wire.hpp kSteadySnapshotBytes）
inline constexpr std::size_t kClientBandwidthBytesPerSec = 40960u;   // 每客户端 40 KB/s

// §5.6：500ms 心跳。心跳包是 flags = reliable|ackOnly、载荷 0 字节，不进重传表（§5.3）。
class KeepAliveTimer {
 public:
  explicit KeepAliveTimer(uint32_t nowMs = 0u) noexcept;

  bool due(uint32_t nowMs) const noexcept;
  void markSent(uint32_t nowMs) noexcept;
  void onAnyPacket(uint32_t nowMs) noexcept;
  bool isOffline(uint32_t nowMs) const noexcept;

  std::size_t sentCount() const noexcept;
  uint32_t lastRecvMs() const noexcept;
  uint32_t lastSendMs() const noexcept;

 private:
  uint32_t lastRecvMs_ = 0u;
  uint32_t lastSendMs_ = 0u;
  std::size_t sentCount_ = 0u;
};

// §5.2/§5.6：宽限期（Alloc → Connected → GracePeriod(<=30s) → Released）。
class GraceTimer {
 public:
  void enter(uint32_t nowMs) noexcept;
  void clear() noexcept;
  bool isActive() const noexcept;
  bool isExpired(uint32_t nowMs) const noexcept;
  uint32_t startMs() const noexcept;

 private:
  bool isActive_ = false;
  uint32_t startMs_ = 0u;
};

}  // namespace ac::net
