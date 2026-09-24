// S04 §5.3：ack 位图与重传表的纯逻辑实现。
#include "net/reliability.hpp"

#include "net/wire.hpp"

namespace ac::net {

bool ackOnReceive(ReliableState& state, uint32_t msgId) noexcept {
  if (msgId == 0u) return false;  // msgId 从 1 起
  if (msgId <= state.ackBase) {
    const uint32_t distance = state.ackBase - msgId;  // >= 1（相等即当前 ackBase，已收到）
    if (distance < 1u || distance > 32u) return false;
    const uint32_t bit = 1u << (distance - 1u);
    if ((state.ackBits & bit) != 0u) return false;  // 重复：直接丢弃，不计错误
    state.ackBits |= bit;
    return true;
  }
  const uint32_t k = msgId - state.ackBase;
  if (k >= 33u) {
    // 比新 ackBase 早 32 以上的 id 全部落在窗口外：位图清空。
    //（§5.3 的伪代码只护住了左移，没护住 1u << (k-1)，k >= 33 会移位越界，这里显式清零。）
    state.ackBits = 0u;
  } else {
    state.ackBits = (k >= 32u ? 0u : (state.ackBits << k)) | (1u << (k - 1u));
  }
  state.ackBase = msgId;
  return true;
}

bool isAckedBy(const ReliableState& state, uint32_t msgId) noexcept {
  if (msgId == state.ackBase) return true;
  if (msgId > state.ackBase) return false;
  const uint32_t distance = state.ackBase - msgId;
  if (distance < 1u || distance > 32u) return false;
  return (state.ackBits & (1u << (distance - 1u))) != 0u;
}

bool isRetransmitTracked(uint16_t flags, std::size_t payloadBytes) noexcept {
  const bool isReliable = (flags & kFlagReliable) != 0u;
  const bool isAckOnly = (flags & kFlagAckOnly) != 0u;
  return isReliable && !isAckOnly && payloadBytes > 0u;
}

void ReliableChannel::track(uint32_t msgId, std::vector<uint8_t> packet, uint32_t nowMs) {
  Entry entry{};
  entry.msgId = msgId;
  entry.packet = std::move(packet);
  entry.retransmits = 0u;
  entry.nextSendMs = nowMs + kRtoTableMs[0];  // 第 1 次重传等 200ms
  entries_.push_back(std::move(entry));
}

std::size_t ReliableChannel::applyAck(const ReliableState& peer) noexcept {
  const std::size_t before = entries_.size();
  std::size_t write = 0u;
  for (std::size_t read = 0u; read < entries_.size(); ++read) {
    if (isAckedBy(peer, entries_[read].msgId)) continue;
    if (write != read) entries_[write] = std::move(entries_[read]);
    ++write;
  }
  entries_.resize(write);
  return before - write;
}

std::vector<ReliableChannel::Entry> ReliableChannel::collectDue(uint32_t nowMs) {
  std::vector<Entry> due;
  std::size_t write = 0u;
  for (std::size_t read = 0u; read < entries_.size(); ++read) {
    Entry& entry = entries_[read];
    if (nowMs < entry.nextSendMs) {
      if (write != read) entries_[write] = std::move(entry);
      ++write;
      continue;
    }
    if (entry.retransmits >= kMaxRetransmits) {
      // 累计重传已达 5 次：第 6 次重传之前判失联（§5.1/§5.3），条目出表。
      isLost_ = true;
      continue;
    }
    due.push_back(entry);  // 拷贝：重传内容与原包逐字节一致
    ++entry.retransmits;
    ++retransmitCount_;
    const std::size_t step = entry.retransmits < kMaxRetransmits ? entry.retransmits : kMaxRetransmits - 1u;
    entry.nextSendMs = nowMs + kRtoTableMs[step];
    if (write != read) entries_[write] = std::move(entry);
    ++write;
  }
  entries_.resize(write);
  return due;
}

void ReliableChannel::clear() noexcept {
  entries_.clear();
  isLost_ = false;
  retransmitCount_ = 0u;
}

}  // namespace ac::net
