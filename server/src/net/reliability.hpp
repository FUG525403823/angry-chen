#pragma once
// S04 §5.3：每通道序号、ack 位图、重传表与失联判定。纯逻辑，不碰套接字。
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ac::net {

// §5.1 常量表：RTO 序列由纯整数递推给出，禁用幂函数与浮点退避（ADR-010 运算子集）。
inline constexpr uint32_t kInitialRtoMs = 200u;
inline constexpr double kRtoBackoff = 1.5;
inline constexpr uint32_t kMaxRtoMs = 1000u;
inline constexpr std::size_t kMaxRetransmits = 5u;

constexpr std::array<uint32_t, kMaxRetransmits> makeRtoTable() noexcept {
  std::array<uint32_t, kMaxRetransmits> table{};
  uint32_t value = kInitialRtoMs;
  for (std::size_t i = 0; i < kMaxRetransmits; ++i) {
    table[i] = value;
    const uint32_t next = value + value / 2u;
    value = next > kMaxRtoMs ? kMaxRtoMs : next;
  }
  return table;
}

inline constexpr std::array<uint32_t, kMaxRetransmits> kRtoTableMs = makeRtoTable();
static_assert(kRtoTableMs[0] == 200u && kRtoTableMs[1] == 300u && kRtoTableMs[2] == 450u &&
                  kRtoTableMs[3] == 675u && kRtoTableMs[4] == 1000u,
              "RTO 表必须逐字等于 §5.1 的 {200,300,450,675,1000}");

// §5.3 冻结结构（逐字）。
struct ReliableState {
  uint32_t sendMsgId = 1u;
  uint32_t ackBase = 0u;
  uint32_t ackBits = 0u;
};

// 收到 msgId = m：true = 新包（已更新 ack 位图），false = 重复或落在 32 位窗口之外。
bool ackOnReceive(ReliableState& state, uint32_t msgId) noexcept;

// bit i 表示 msgId == ackBase - 1 - i 已收到。
bool isAckedBy(const ReliableState& state, uint32_t msgId) noexcept;

// 只有 flags.reliable = 1、非 ackOnly 且载荷 > 0 的消息进重传表（§5.3）。
bool isRetransmitTracked(uint16_t flags, std::size_t payloadBytes) noexcept;

// §5.2：每条通道各一条发送序号与一条接收序号（u16 回绕）。
// msgId 管去重与 ack（§5.3），seq 管"丢弃过期快照"——两者互不干涉。
struct ChannelSeq {
  uint16_t send = 0u;
  uint16_t recv = 0u;

  uint16_t nextSend() noexcept {
    ++send;
    return send;
  }
  // 相对差值比较（u16 回绕）：incoming 比已接受的更新 → true。
  bool isNewerThanRecv(uint16_t incoming) const noexcept {
    return static_cast<int16_t>(incoming - recv) > 0;
  }
  void accept(uint16_t incoming) noexcept { recv = incoming; }
};

// 一条通道的发送侧重传表。
class ReliableChannel {
 public:
  struct Entry {
    uint32_t msgId = 0u;
    std::vector<uint8_t> packet;
    std::size_t retransmits = 0u;
    uint32_t nextSendMs = 0u;
  };

  // 发送序号（包头 seq）与 msgId 相互独立：seq 在 ChannelSeq 里，msgId 在 sendState_ 里。
  uint32_t allocateMsgId() noexcept { return sendState_.sendMsgId++; }
  const ReliableState& sendState() const noexcept { return sendState_; }

  void track(uint32_t msgId, std::vector<uint8_t> packet, uint32_t nowMs);
  std::size_t applyAck(const ReliableState& peer) noexcept;
  std::vector<Entry> collectDue(uint32_t nowMs);

  bool isLost() const noexcept { return isLost_; }
  std::size_t pendingCount() const noexcept { return entries_.size(); }
  std::size_t retransmitCount() const noexcept { return retransmitCount_; }
  void clear() noexcept;

 private:
  ReliableState sendState_{};
  std::vector<Entry> entries_;
  bool isLost_ = false;
  std::size_t retransmitCount_ = 0u;
};

}  // namespace ac::net
