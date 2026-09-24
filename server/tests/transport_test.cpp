// S04 §5–§7：传输子层（可靠性、分片、握手、心跳/宽限期、内存总线）的环回集成。
// 全部用例由调用方喂虚拟时间，不读系统时钟、不起线程；只有 udp_socket 那条碰真实套接字（回环）。
#include "tiny_test.hpp"

#include "core/math.hpp"
#include "net/codec.hpp"
#include "net/fragment.hpp"
#include "net/handshake.hpp"
#include "net/keepalive.hpp"
#include "net/memory_transport.hpp"
#include "net/reliability.hpp"
#include "net/udp_socket.hpp"
#include "net/wire.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <span>
#include <vector>

namespace net = ac::net;
using net::PacketHeader;
using net::PacketType;
using net::ReliableExt;
using net::ReliableState;

namespace {

constexpr uint32_t kMsPerTick = static_cast<uint32_t>(ac::kTickMs);
constexpr uint16_t kSession = 0x123u;
constexpr net::EndpointId kClient{1u};
constexpr net::EndpointId kServer{2u};

PacketHeader makeHeader(PacketType type, uint16_t flags, uint16_t session, uint16_t seq) {
  return PacketHeader{net::kProtocolVersion, static_cast<uint8_t>(type), flags, session, seq};
}

std::vector<uint8_t> finish(const net::EncodeResult& encoded, std::vector<uint8_t>& buffer) {
  if (!encoded.isOk) return {};
  buffer.resize(encoded.bytes);
  return buffer;
}

std::vector<uint8_t> encodeCommandPacket(uint16_t session, uint16_t seq, uint32_t msgId,
                                        uint32_t ackBase, uint32_t ackBits, int8_t moveX) {
  const net::CommandPayload command{moveX, 0, 0u, 0u, 0u, 0u, 0u, 0u};
  std::vector<uint8_t> buffer(net::kMaxPacketBytes, 0u);
  return finish(net::encodeCommand(makeHeader(PacketType::kCommand, net::kFlagReliable, session, seq),
                                   ReliableExt{msgId, ackBase, ackBits}, command, buffer.data(),
                                   buffer.size()),
                buffer);
}

std::vector<uint8_t> encodeAckPacket(uint16_t session, const ReliableState& state) {
  std::vector<uint8_t> buffer(net::kMaxPacketBytes, 0u);
  return finish(net::encodeKeepAlive(
                    makeHeader(PacketType::kKeepAlive, net::kFlagReliable | net::kFlagAckOnly, session, 0u),
                    ReliableExt{0u, state.ackBase, state.ackBits}, buffer.data(), buffer.size()),
                buffer);
}

std::vector<uint8_t> encodeHelloPacket(uint32_t nonce) {
  std::vector<uint8_t> buffer(64u, 0u);
  return finish(net::encodeHello(makeHeader(PacketType::kHello, 0u, 0u, 0u),
                                 net::HelloPayload{nonce, 0u}, buffer.data(), buffer.size()),
                buffer);
}

std::vector<uint8_t> encodeResumePacket(uint16_t session, uint32_t token) {
  std::vector<uint8_t> buffer(64u, 0u);
  return finish(net::encodeResume(makeHeader(PacketType::kResume, net::kFlagReliable, session, 0u),
                                  ReliableExt{1u, 0u, 0u}, net::ResumePayload{token}, buffer.data(),
                                  buffer.size()),
                buffer);
}

std::vector<uint8_t> encodeDisconnectPacket(uint16_t session, net::DisconnectReason reason) {
  std::vector<uint8_t> buffer(64u, 0u);
  return finish(net::encodeDisconnect(makeHeader(PacketType::kDisconnect, net::kFlagReliable, session, 0u),
                                      ReliableExt{1u, 0u, 0u},
                                      net::DisconnectPayload{static_cast<uint8_t>(reason)},
                                      buffer.data(), buffer.size()),
                buffer);
}

net::FragmentKey fragmentKeyOf(const net::PacketInfo& info) {
  return net::FragmentKey{info.header.session, info.header.type, info.fragment.fragId};
}

std::span<const uint8_t> payloadOf(const std::vector<uint8_t>& packet, const net::PacketInfo& info) {
  return std::span<const uint8_t>(packet.data() + info.payloadOffset, info.payloadBytes);
}

std::vector<uint8_t> makeMessage(std::size_t size) {
  std::vector<uint8_t> message(size, 0u);
  for (std::size_t i = 0u; i < size; ++i) {
    message[i] = static_cast<uint8_t>((i * 31u + 7u) & 0xFFu);
  }
  return message;
}

}  // namespace

// ---------- 可靠性（--filter=reliability）----------

AC_TEST(reliability_rto_table_sequence) {
  std::printf("rto=");
  for (std::size_t i = 0u; i < net::kRtoTableMs.size(); ++i) {
    std::printf("%s%u", i == 0u ? "" : ",", net::kRtoTableMs[i]);
  }
  std::printf("\n");

  AC_CHECK_EQ(net::kRtoTableMs.size(), net::kMaxRetransmits);
  AC_CHECK_EQ(net::kRtoTableMs[0], 200u);
  AC_CHECK_EQ(net::kRtoTableMs[1], 300u);
  AC_CHECK_EQ(net::kRtoTableMs[2], 450u);
  AC_CHECK_EQ(net::kRtoTableMs[3], 675u);
  AC_CHECK_EQ(net::kRtoTableMs[4], 1000u);
  AC_CHECK_EQ(net::kInitialRtoMs, 200u);
  AC_CHECK_EQ(net::kMaxRtoMs, 1000u);
  AC_CHECK_EQ(net::kMaxRetransmits, 5u);
  AC_CHECK(net::kRtoBackoff == 1.5);
  // 递增且被 kMaxRtoMs 截断（纯整数递推，禁 pow）
  for (std::size_t i = 1u; i < net::kRtoTableMs.size(); ++i) {
    AC_CHECK(net::kRtoTableMs[i] > net::kRtoTableMs[i - 1u]);
    AC_CHECK(net::kRtoTableMs[i] <= net::kMaxRtoMs);
  }
}

AC_TEST(reliability_ack_bitmap_advance) {
  net::ReliableState state{};
  AC_CHECK_EQ(state.sendMsgId, 1u);
  AC_CHECK_EQ(state.ackBase, 0u);
  AC_CHECK_EQ(state.ackBits, 0u);

  AC_CHECK(net::ackOnReceive(state, 1u));
  AC_CHECK_EQ(state.ackBase, 1u);
  AC_CHECK_EQ(state.ackBits, 0x1u);
  AC_CHECK(net::ackOnReceive(state, 2u));
  AC_CHECK_EQ(state.ackBase, 2u);
  AC_CHECK_EQ(state.ackBits, 0x3u);  // 2 = ackBase，1 = 后退 1 位

  net::ReliableState jumped{};
  AC_CHECK(net::ackOnReceive(jumped, 1u));
  AC_CHECK(net::ackOnReceive(jumped, 5u));
  AC_CHECK_EQ(jumped.ackBase, 5u);
  // bit3 = 旧 ackBase（1），bit4 = 首次收包时按公式记下的 msgId 0（§5.3 公式的产物，两端一致即可）。
  AC_CHECK_EQ(jumped.ackBits, 0x18u);

  // 32 位窗口边界：k == 32 时旧 ackBase 仍在窗口里；k >= 33 全滑出（位图清零）
  net::ReliableState wide{};
  AC_CHECK(net::ackOnReceive(wide, 1u));
  AC_CHECK(net::ackOnReceive(wide, 33u));
  AC_CHECK_EQ(wide.ackBase, 33u);
  AC_CHECK_EQ(wide.ackBits, 0x80000000u);
  net::ReliableState slide{};
  AC_CHECK(net::ackOnReceive(slide, 1u));
  AC_CHECK(net::ackOnReceive(slide, 40u));  // k = 39
  AC_CHECK_EQ(slide.ackBase, 40u);
  AC_CHECK_EQ(slide.ackBits, 0u);
}

AC_TEST(reliability_ack_bitmap_old_ids) {
  net::ReliableState state{};
  AC_CHECK(net::ackOnReceive(state, 100u));
  AC_CHECK(net::ackOnReceive(state, 99u));
  AC_CHECK(net::ackOnReceive(state, 98u));
  AC_CHECK_EQ(state.ackBase, 100u);
  AC_CHECK_EQ(state.ackBits, 0x3u);
  AC_CHECK(net::isAckedBy(state, 100u));
  AC_CHECK(net::isAckedBy(state, 99u));
  AC_CHECK(net::isAckedBy(state, 98u));
  AC_CHECK(!net::isAckedBy(state, 97u));
  AC_CHECK(!net::isAckedBy(state, 101u));

  AC_CHECK(!net::ackOnReceive(state, 100u));  // 重复（== ackBase）
  AC_CHECK(!net::ackOnReceive(state, 99u));   // 重复（位图已置位）
  AC_CHECK(!net::ackOnReceive(state, 60u));   // 过旧：落后 40 > 32，落在窗口外
  AC_CHECK_EQ(state.ackBase, 100u);
  AC_CHECK_EQ(state.ackBits, 0x3u);
  AC_CHECK(!net::ackOnReceive(state, 0u));    // msgId 从 1 起
}

AC_TEST(reliability_retransmit_then_lost) {
  net::ReliableChannel channel;
  const uint32_t msgId = channel.allocateMsgId();
  AC_CHECK_EQ(msgId, 1u);
  channel.track(msgId, std::vector<uint8_t>{1u, 2u, 3u}, 0u);
  AC_CHECK_EQ(channel.pendingCount(), 1u);

  // 发送时刻 0；等待序列 = 200/300/450/675/1000 → 重传时刻 200/500/950/1625/2625
  const uint32_t expected[5] = {200u, 500u, 950u, 1625u, 2625u};
  for (std::size_t i = 0u; i < net::kMaxRetransmits; ++i) {
    AC_CHECK(channel.collectDue(expected[i] - 1u).empty());  // 差 1ms 不发
    const std::vector<net::ReliableChannel::Entry> due = channel.collectDue(expected[i]);
    AC_CHECK_EQ(due.size(), 1u);
    if (due.size() == 1u) {
      AC_CHECK_EQ(due[0].msgId, msgId);
      AC_CHECK_EQ(due[0].retransmits, i);
      AC_CHECK_EQ(due[0].packet.size(), 3u);
    }
  }
  AC_CHECK_EQ(channel.retransmitCount(), 5u);
  AC_CHECK(!channel.isLost());
  AC_CHECK(channel.collectDue(3624u).empty());
  // 第 6 次重传之前判失联：不再产生任何重传包，条目出表
  AC_CHECK(channel.collectDue(3625u).empty());
  AC_CHECK(channel.isLost());
  AC_CHECK_EQ(channel.pendingCount(), 0u);
  AC_CHECK_EQ(channel.retransmitCount(), 5u);
}

AC_TEST(reliability_duplicate_is_ignored) {
  // 只有可靠、非 ackOnly、载荷 > 0 的消息进重传表（§5.3）
  AC_CHECK(net::isRetransmitTracked(net::kFlagReliable, 4u));
  AC_CHECK(!net::isRetransmitTracked(net::kFlagReliable, 0u));
  AC_CHECK(!net::isRetransmitTracked(net::kFlagReliable | net::kFlagAckOnly, 0u));
  AC_CHECK(!net::isRetransmitTracked(net::kFlagReliable | net::kFlagAckOnly, 4u));
  AC_CHECK(!net::isRetransmitTracked(0u, 4u));

  net::ReliableChannel channel;
  const uint32_t first = channel.allocateMsgId();
  const uint32_t second = channel.allocateMsgId();
  channel.track(first, std::vector<uint8_t>{0u}, 0u);
  channel.track(second, std::vector<uint8_t>{1u}, 0u);
  net::ReliableState peer{};
  AC_CHECK(net::ackOnReceive(peer, first));
  AC_CHECK_EQ(channel.applyAck(peer), 1u);
  AC_CHECK_EQ(channel.pendingCount(), 1u);
  AC_CHECK_EQ(channel.applyAck(peer), 0u);  // 幂等
  AC_CHECK(net::ackOnReceive(peer, second));
  AC_CHECK_EQ(channel.applyAck(peer), 1u);
  AC_CHECK_EQ(channel.pendingCount(), 0u);
  // 收到的重复 msgId 直接丢弃，不计错误
  AC_CHECK(!net::ackOnReceive(peer, second));

  // §5.2：每条通道的发送/接收序号与 msgId 相互独立，u16 回绕；Snapshot 用 seq 丢弃过期包。
  net::ChannelSeq seq{};
  AC_CHECK_EQ(seq.send, 0u);
  AC_CHECK_EQ(seq.nextSend(), 1u);
  seq.send = 65535u;
  AC_CHECK_EQ(seq.nextSend(), 0u);  // u16 回绕
  AC_CHECK_EQ(seq.recv, 0u);
  AC_CHECK(seq.isNewerThanRecv(1u));
  seq.accept(1u);
  AC_CHECK(!seq.isNewerThanRecv(1u));      // 重复
  AC_CHECK(!seq.isNewerThanRecv(0u));      // 过期
  AC_CHECK(!seq.isNewerThanRecv(65535u));  // 回绕前的旧包同样判过期
  AC_CHECK(seq.isNewerThanRecv(2u));
  AC_CHECK(seq.isNewerThanRecv(32767u));   // 半程之内仍算新
}

// ---------- 分片（--filter=fragment）----------

AC_TEST(fragment_split_5000_bytes_reassembles) {
  const std::vector<uint8_t> message = makeMessage(5000u);
  AC_CHECK(net::isSplittable(message.size()));
  AC_CHECK_EQ(net::fragmentCountFor(message.size()), 5u);  // 1176 * 4 = 4704 < 5000

  const PacketHeader header = makeHeader(PacketType::kSnapshot, 0u, kSession, 3u);
  const std::vector<std::vector<uint8_t>> slices =
      net::splitMessage(header, ReliableExt{}, message, 7u);
  AC_CHECK_EQ(slices.size(), 5u);
  for (const std::vector<uint8_t>& slice : slices) {
    AC_CHECK(slice.size() <= net::kMaxPacketBytes);
    const auto info = net::decodePacket(slice.data(), slice.size());
    AC_CHECK(info.isOk);
    if (!info.isOk) continue;
    AC_CHECK_EQ(info.value.header.type, static_cast<uint8_t>(net::PacketType::kFragment));
    AC_CHECK(info.value.hasFragmentHeader);
    AC_CHECK_EQ(info.value.fragment.fragId, 7u);
    AC_CHECK_EQ(info.value.fragment.fragCount, 5u);
    AC_CHECK(info.value.payloadBytes <= net::kMaxFragmentPayload);
    AC_CHECK_EQ(info.value.payloadOffset + info.value.payloadBytes, slice.size());
  }

  net::Reassembler reassembler;
  std::vector<uint8_t> assembled;
  for (std::size_t i = slices.size(); i-- > 0u;) {  // 逆序投递
    const auto info = net::decodePacket(slices[i].data(), slices[i].size());
    AC_CHECK(info.isOk);
    const auto status = reassembler.add(fragmentKeyOf(info.value), info.value.fragment.fragIndex,
                                        info.value.fragment.fragCount,
                                        payloadOf(slices[i], info.value), 0u, assembled);
    AC_CHECK(status == (i == 0u ? net::Reassembler::Status::kComplete
                                : net::Reassembler::Status::kIncomplete));
  }
  AC_CHECK_EQ(assembled.size(), message.size());
  AC_CHECK(std::equal(assembled.begin(), assembled.end(), message.begin()));
  AC_CHECK_EQ(reassembler.completedCount(), 1u);
  AC_CHECK_EQ(reassembler.activeGroups(), 0u);

  // 可靠分片（事件通道）：每片都带 12B 可靠扩展头，载荷偏移整体后移
  const std::vector<std::vector<uint8_t>> reliableSlices =
      net::splitMessage(makeHeader(PacketType::kEvent, net::kFlagReliable, kSession, 8u),
                        ReliableExt{5u, 3u, 0x7u}, message, 6u);
  AC_CHECK_EQ(reliableSlices.size(), 5u);
  const std::size_t reliableOffset =
      net::kCommonHeaderBytes + net::kReliableExtBytes + net::kFragmentHeaderBytes;
  for (const std::vector<uint8_t>& slice : reliableSlices) {
    const auto info = net::decodePacket(slice.data(), slice.size());
    AC_CHECK(info.isOk);
    if (!info.isOk) continue;
    AC_CHECK(info.value.hasReliableExt);
    AC_CHECK_EQ(info.value.reliableExt.msgId, 5u);
    AC_CHECK_EQ(info.value.header.type, static_cast<uint8_t>(net::PacketType::kFragment));
    AC_CHECK_EQ(info.value.payloadOffset, reliableOffset);
    AC_CHECK(slice.size() <= net::kMaxPacketBytes);
  }
  net::Reassembler reliableReassembler;
  std::vector<uint8_t> reliableOut;
  for (std::size_t i = reliableSlices.size(); i-- > 0u;) {
    const auto info = net::decodePacket(reliableSlices[i].data(), reliableSlices[i].size());
    AC_CHECK(info.isOk);
    if (!info.isOk) continue;
    AC_CHECK(reliableReassembler.add(fragmentKeyOf(info.value), info.value.fragment.fragIndex,
                                     info.value.fragment.fragCount,
                                     payloadOf(reliableSlices[i], info.value), 0u,
                                     reliableOut) ==
             (i == 0u ? net::Reassembler::Status::kComplete : net::Reassembler::Status::kIncomplete));
  }
  AC_CHECK_EQ(reliableOut.size(), message.size());
  AC_CHECK(std::equal(reliableOut.begin(), reliableOut.end(), message.begin()));
}

AC_TEST(fragment_over_eight_slices_rejected) {
  AC_CHECK_EQ(net::fragmentCountFor(net::kMaxLogicalMessageBytes), 8u);
  AC_CHECK(net::isSplittable(net::kMaxLogicalMessageBytes));
  AC_CHECK(!net::isSplittable(net::kMaxLogicalMessageBytes + 1u));
  AC_CHECK(!net::isSplittable(0u));
  // 不可分时 splitMessage 返回空，由调用方拒绝（不崩溃、不越界）
  AC_CHECK(net::splitMessage(makeHeader(PacketType::kSnapshot, 0u, kSession, 1u), ReliableExt{},
                             makeMessage(net::kMaxLogicalMessageBytes + 1u), 1u)
               .empty());

  net::Reassembler reassembler;
  std::vector<uint8_t> out;
  const net::FragmentKey key{kSession, static_cast<uint8_t>(PacketType::kSnapshot), 7u};
  const std::vector<uint8_t> payload{1u, 2u, 3u, 4u};
  AC_CHECK(reassembler.add(key, 0u, 9u, payload, 0u, out) == net::Reassembler::Status::kBadValue);
  AC_CHECK(reassembler.add(key, 9u, 9u, payload, 0u, out) == net::Reassembler::Status::kBadValue);
  AC_CHECK(reassembler.add(key, 0u, 0u, payload, 0u, out) == net::Reassembler::Status::kBadValue);
  AC_CHECK_EQ(reassembler.activeGroups(), 0u);
  AC_CHECK_EQ(reassembler.completedCount(), 0u);
}

AC_TEST(fragment_out_of_order_reassembles) {
  const std::vector<uint8_t> message = makeMessage(2000u);
  const std::vector<std::vector<uint8_t>> slices = net::splitMessage(
      makeHeader(PacketType::kSnapshot, 0u, kSession, 4u), ReliableExt{}, message, 11u);
  AC_CHECK_EQ(slices.size(), 2u);

  net::Reassembler reassembler;
  std::vector<uint8_t> assembled;
  const auto second = net::decodePacket(slices[1].data(), slices[1].size());
  const auto first = net::decodePacket(slices[0].data(), slices[0].size());
  AC_CHECK(second.isOk && first.isOk);
  AC_CHECK(reassembler.add(fragmentKeyOf(second.value), 1u, 2u, payloadOf(slices[1], second.value), 0u,
                           assembled) == net::Reassembler::Status::kIncomplete);
  // 重复投递同一片：不计数、不破坏
  AC_CHECK(reassembler.add(fragmentKeyOf(second.value), 1u, 2u, payloadOf(slices[1], second.value), 1u,
                           assembled) == net::Reassembler::Status::kIncomplete);
  AC_CHECK(reassembler.add(fragmentKeyOf(first.value), 0u, 2u, payloadOf(slices[0], first.value), 2u,
                           assembled) == net::Reassembler::Status::kComplete);
  AC_CHECK_EQ(assembled.size(), message.size());
  AC_CHECK(std::equal(assembled.begin(), assembled.end(), message.begin()));
  // 不同 fragId 的组互不干扰
  std::vector<uint8_t> other;
  const std::vector<uint8_t> slicePayload{5u, 6u, 7u};
  // 线上分片一律 type = kFragment（原通道身份不上线），不同组只能靠 fragId 区分
  const net::FragmentKey otherKey{kSession, static_cast<uint8_t>(PacketType::kFragment), 12u};
  AC_CHECK(reassembler.add(otherKey, 0u, 2u, slicePayload, 0u, other) ==
           net::Reassembler::Status::kIncomplete);
  AC_CHECK_EQ(reassembler.activeGroups(), 1u);
}

AC_TEST(fragment_60_tick_timeout_drops_group) {
  net::Reassembler reassembler;
  std::vector<uint8_t> out;
  const net::FragmentKey key{kSession, static_cast<uint8_t>(PacketType::kSnapshot), 21u};
  const std::vector<uint8_t> payload{9u, 8u, 7u};
  AC_CHECK(reassembler.add(key, 0u, 2u, payload, 100u, out) == net::Reassembler::Status::kIncomplete);
  AC_CHECK_EQ(reassembler.activeGroups(), 1u);
  AC_CHECK_EQ(reassembler.expire(159u), 0u);  // 59 tick：未到期
  AC_CHECK_EQ(reassembler.activeGroups(), 1u);
  AC_CHECK_EQ(reassembler.expire(160u), 1u);  // 60 tick：整组丢弃
  AC_CHECK_EQ(reassembler.activeGroups(), 0u);
  AC_CHECK_EQ(reassembler.timedOutCount(), 1u);
  AC_CHECK_EQ(reassembler.completedCount(), 0u);
  // 迟到切片开启新组（旧组已丢），并按新组自己的计时到期
  AC_CHECK(reassembler.add(key, 1u, 2u, payload, 161u, out) == net::Reassembler::Status::kIncomplete);
  AC_CHECK_EQ(reassembler.expire(220u), 0u);
  AC_CHECK_EQ(reassembler.expire(221u), 1u);
  AC_CHECK_EQ(reassembler.timedOutCount(), 2u);
}

// ---------- 宽限期与令牌（--filter=grace）----------

AC_TEST(grace_disconnect_after_three_seconds) {
  net::HandshakeServer server{};
  const net::HandshakeOutcome hello = server.onHello(0x11223344u, 0u, 0u);
  AC_CHECK(hello.isAccepted);
  AC_CHECK(hello.isHelloAckDue);
  net::SessionRecord* record = server.sessions().find(hello.session);
  AC_CHECK(record != nullptr);
  if (record == nullptr) return;
  AC_CHECK(record->isConnected());
  AC_CHECK_EQ(record->token, net::reconnectTokenFor(0x11223344u, hello.salt));

  AC_CHECK(!record->keepAlive.isOffline(2999u));
  const net::SessionTick early = server.sessions().tick(2999u);
  AC_CHECK_EQ(early.wentOffline, 0u);
  AC_CHECK_EQ(early.released, 0u);
  AC_CHECK(record->isConnected());

  const net::SessionTick at3s = server.sessions().tick(3000u);
  AC_CHECK_EQ(at3s.wentOffline, 1u);
  AC_CHECK_EQ(at3s.released, 0u);
  record = server.sessions().find(hello.session);
  AC_CHECK(record != nullptr);
  if (record == nullptr) return;
  AC_CHECK(record->isResumable());
  AC_CHECK(record->grace.isActive());
  AC_CHECK_EQ(record->grace.startMs(), 3000u);
  AC_CHECK_EQ(server.sessions().connectedCount(), 0u);
  AC_CHECK_EQ(server.sessions().graceCount(), 1u);
}

AC_TEST(grace_resume_within_window_succeeds) {
  net::HandshakeServer server{};
  const net::HandshakeOutcome hello = server.onHello(0x0BADF00Du, 0u, 0u);
  AC_CHECK(hello.isAccepted);
  const uint16_t session = hello.session;
  AC_CHECK_EQ(server.sessions().tick(3000u).wentOffline, 1u);

  const net::HandshakeOutcome resume = server.onResume(session, hello.token, 20000u);
  AC_CHECK(resume.isAccepted);
  AC_CHECK(resume.isResumed);
  AC_CHECK(resume.isFullSnapshotDue);  // §5.5：恢复后补一次 baselineTick = 0 的全量快照
  AC_CHECK(!resume.isDisconnectDue);
  const net::SessionRecord* record = server.sessions().find(session);
  AC_CHECK(record != nullptr);
  if (record == nullptr) return;
  AC_CHECK(record->isConnected());
  AC_CHECK(!record->grace.isActive());
  AC_CHECK_EQ(record->keepAlive.lastRecvMs(), 20000u);  // 刷新 lastRecvMs
  AC_CHECK(!record->keepAlive.isOffline(22999u));
  AC_CHECK_EQ(server.sessions().connectedCount(), 1u);
  AC_CHECK_EQ(server.sessions().graceCount(), 0u);
}

AC_TEST(grace_release_after_thirty_seconds) {
  net::HandshakeServer server{};
  const net::HandshakeOutcome hello = server.onHello(0x22334455u, 0u, 0u);
  AC_CHECK(hello.isAccepted);
  AC_CHECK_EQ(server.sessions().tick(3000u).wentOffline, 1u);
  AC_CHECK_EQ(server.sessions().size(), 1u);

  AC_CHECK_EQ(server.sessions().tick(32999u).released, 0u);  // 29999ms：还在宽限期
  AC_CHECK(server.sessions().find(hello.session) != nullptr);
  const net::SessionTick expired = server.sessions().tick(33000u);  // 3000 + 30000
  AC_CHECK_EQ(expired.released, 1u);
  AC_CHECK_EQ(expired.wentOffline, 0u);
  AC_CHECK(server.sessions().find(hello.session) == nullptr);
  AC_CHECK_EQ(server.sessions().size(), 0u);

  // 名额回到池里：可以再分配，且旧令牌失效
  const net::HandshakeOutcome again = server.onHello(0x66778899u, 0u, 34000u);
  AC_CHECK(again.isAccepted);
  AC_CHECK(again.session != hello.session);
  AC_CHECK_EQ(server.sessions().size(), 1u);
  const net::HandshakeOutcome late = server.onResume(hello.session, hello.token, 35000u);
  AC_CHECK(late.isDisconnectDue);
  AC_CHECK(late.reason == net::DisconnectReason::kTokenInvalid);

  // §8 风险表：在册会话打满 kSessions 时，先驱逐最早进入宽限期的会话；没有宽限期会话才算真打满。
  net::HandshakeServer full{};
  const net::HandshakeOutcome firstSlot = full.onHello(0x2000u, 0u, 0u);
  AC_CHECK(firstSlot.isAccepted);
  for (std::size_t i = 1u; i + 1u < net::kSessions; ++i) {  // 其余 254 个仍在 t = 0 建立
    AC_CHECK(full.onHello(0x2000u + static_cast<uint32_t>(i), 0u, 0u).isAccepted);
  }
  const net::HandshakeOutcome lastSlot = full.onHello(0x7FFF0000u, 0u, 1000u);  // 第 256 个
  AC_CHECK(lastSlot.isAccepted);
  AC_CHECK_EQ(full.sessions().size(), net::kSessions);
  AC_CHECK_EQ(full.sessions().connectedCount(), net::kSessions);

  const net::HandshakeOutcome noRoom = full.onHello(0x7FFF0001u, 0u, 1000u);
  AC_CHECK(!noRoom.isAccepted);
  AC_CHECK(noRoom.isDisconnectDue);
  AC_CHECK(noRoom.reason == net::DisconnectReason::kRateLimited);  // §5.5 无专门取值
  AC_CHECK_EQ(full.sessions().size(), net::kSessions);

  AC_CHECK_EQ(full.sessions().tick(3000u).wentOffline, net::kSessions - 1u);  // t = 0 那批判断线
  AC_CHECK_EQ(full.sessions().graceCount(), net::kSessions - 1u);
  AC_CHECK_EQ(full.sessions().tick(4000u).wentOffline, 1u);  // 第 256 个随后判断线
  AC_CHECK_EQ(full.sessions().graceCount(), net::kSessions);

  const net::HandshakeOutcome admitted = full.onHello(0x7FFF0002u, 0u, 4100u);
  AC_CHECK(admitted.isAccepted);
  AC_CHECK_EQ(full.sessions().size(), net::kSessions);  // 驱逐一个、收下一个
  AC_CHECK(full.sessions().find(firstSlot.session) == nullptr);  // 最早进宽限期的那个被驱逐
  AC_CHECK(full.sessions().find(lastSlot.session) != nullptr);    // 最晚进宽限期的还在
}

AC_TEST(grace_wrong_token_rejected) {
  net::HandshakeServer server{};
  const net::HandshakeOutcome hello = server.onHello(0x99887766u, 0u, 0u);
  AC_CHECK(hello.isAccepted);
  AC_CHECK_EQ(server.sessions().tick(3000u).wentOffline, 1u);

  const net::HandshakeOutcome wrong = server.onResume(hello.session, hello.token ^ 1u, 3500u);
  AC_CHECK(!wrong.isAccepted);
  AC_CHECK(wrong.isDisconnectDue);
  AC_CHECK(wrong.reason == net::DisconnectReason::kTokenInvalid);
  // 令牌错误即释放名额（§5.5：失败回 reason=2）
  AC_CHECK(server.sessions().find(hello.session) == nullptr);
  AC_CHECK_EQ(server.sessions().size(), 0u);

  // 已连接的会话不接受 Resume
  const net::HandshakeOutcome live = server.onHello(0x1234u, 0u, 0u);
  AC_CHECK(live.isAccepted);
  const net::HandshakeOutcome premature = server.onResume(live.session, live.token, 100u);
  AC_CHECK(premature.isDisconnectDue);
  AC_CHECK(premature.reason == net::DisconnectReason::kTokenInvalid);
}

// ---------- 内存总线（--filter=memory）----------

AC_TEST(memory_loss_rate_drops_packets) {
  net::MemoryTransport bus{0x1234u};
  const std::vector<uint8_t> packet{1u, 2u, 3u, 4u};
  bus.setLossRate(1.0);
  bus.send(kClient, kServer, packet);
  std::size_t delivered = 0u;
  bus.pump(10u, [&](net::EndpointId, std::span<const uint8_t>) { ++delivered; });
  AC_CHECK_EQ(delivered, 0u);
  AC_CHECK_EQ(bus.droppedCount(), 1u);
  AC_CHECK_EQ(bus.sentCount(), 1u);
  AC_CHECK_EQ(bus.deliveredCount(), 0u);
  AC_CHECK_EQ(bus.inFlightCount(), 0u);

  bus.setLossRate(0.0);
  bus.send(kClient, kServer, packet);
  bus.pump(20u, [&](net::EndpointId, std::span<const uint8_t>) { ++delivered; });
  AC_CHECK_EQ(delivered, 1u);
  AC_CHECK_EQ(bus.droppedCount(), 1u);
  AC_CHECK_EQ(bus.deliveredCount(), 1u);

  // 延迟生效：未到送达时刻不投递
  bus.setLatencyMs(50u, 0u);
  bus.send(kClient, kServer, packet);
  bus.pump(30u, [&](net::EndpointId, std::span<const uint8_t>) { ++delivered; });
  AC_CHECK_EQ(delivered, 1u);
  bus.pump(69u, [&](net::EndpointId, std::span<const uint8_t>) { ++delivered; });
  AC_CHECK_EQ(delivered, 1u);
  bus.pump(70u, [&](net::EndpointId, std::span<const uint8_t>) { ++delivered; });
  AC_CHECK_EQ(delivered, 2u);
}

AC_TEST(memory_reliable_delivery_in_order) {
  // DoD §7：20% 丢包 + 50ms 延迟 + 10% 乱序，1000 tick 内可靠消息全部到达并按 msgId 升序交付。
  net::MemoryTransport bus{0x7E57u};
  bus.setLossRate(0.2);
  bus.setLatencyMs(50u, 10u);
  bus.setReorderRate(0.1);

  net::ReliableChannel sender;
  net::ReliableState peerState{};   // 发送方看到的对端 ack 状态
  net::ReliableState receiver{};    // 接收方自己的 ack 位图
  std::map<uint32_t, std::vector<uint8_t>> buffered;
  std::vector<uint32_t> deliveredIds;
  uint32_t expected = 1u;
  const std::size_t kMessages = 50u;
  std::size_t sentMessages = 0u;
  uint32_t lastSendMs = 0u;

  const auto step = [&](uint32_t now) {
    if (sentMessages < kMessages && now - lastSendMs >= 1000u) {
      const uint32_t msgId = sender.allocateMsgId();
      ++sentMessages;
      lastSendMs = now;
      const std::vector<uint8_t> packet =
          encodeCommandPacket(kSession, static_cast<uint16_t>(sentMessages), msgId, peerState.ackBase,
                              peerState.ackBits, static_cast<int8_t>(sentMessages));
      AC_CHECK(!packet.empty());
      bus.send(kClient, kServer, packet);
      sender.track(msgId, packet, now);
    }
    for (const net::ReliableChannel::Entry& entry : sender.collectDue(now)) {
      bus.send(kClient, kServer, entry.packet);
    }
    bus.pump(now, [&](net::EndpointId, std::span<const uint8_t> bytes) {
      const auto info = net::decodePacket(bytes.data(), bytes.size());
      if (!info.isOk) return;
      if (info.value.payloadBytes > 0u) {
        const uint32_t msgId = info.value.reliableExt.msgId;
        if (net::ackOnReceive(receiver, msgId)) {
          buffered[msgId].assign(bytes.data() + info.value.payloadOffset,
                                 bytes.data() + info.value.payloadOffset + info.value.payloadBytes);
          while (true) {
            const auto it = buffered.find(expected);
            if (it == buffered.end()) break;
            deliveredIds.push_back(expected);
            buffered.erase(it);
            ++expected;
          }
        }
        const std::vector<uint8_t> ack = encodeAckPacket(kSession, receiver);
        if (!ack.empty()) bus.send(kServer, kClient, ack);
      } else if (info.value.header.isAckOnly()) {
        peerState.ackBase = info.value.reliableExt.ackBase;
        peerState.ackBits = info.value.reliableExt.ackBits;
        sender.applyAck(peerState);
      }
    });
  };

  for (uint32_t tick = 1u; tick <= 1000u; ++tick) step(tick * kMsPerTick);
  for (uint32_t tick = 1001u; tick <= 1200u && sender.pendingCount() > 0u; ++tick) {
    step(tick * kMsPerTick);
  }

  AC_CHECK_EQ(sentMessages, kMessages);
  AC_CHECK_EQ(deliveredIds.size(), kMessages);
  AC_CHECK_EQ(expected, kMessages + 1u);
  for (std::size_t i = 0u; i < deliveredIds.size(); ++i) {
    AC_CHECK_EQ(deliveredIds[i], static_cast<uint32_t>(i + 1u));  // 严格升序交付
  }
  AC_CHECK_EQ(sender.pendingCount(), 0u);
  AC_CHECK(!sender.isLost());
  AC_CHECK(sender.retransmitCount() > 0u);   // 20% 丢包确实触发了重传
  AC_CHECK(bus.droppedCount() > 0u);
  AC_CHECK(bus.deliveredCount() > kMessages);
}

// ---------- 环回集成（--filter=transport）----------

AC_TEST(transport_handshake_assigns_session) {
  AC_CHECK(net::isVersionAccepted(net::kProtocolVersion));
  AC_CHECK(!net::isVersionAccepted(2u));

  net::MemoryTransport bus{1u};
  net::HandshakeServer server{};
  bool hasBadSession = false;

  const std::vector<uint8_t> hello = encodeHelloPacket(0x11223344u);
  AC_CHECK_EQ(hello.size(), net::kCommonHeaderBytes + net::kHelloPayloadBytes);
  bus.send(kClient, kServer, hello);

  std::vector<uint8_t> received;
  bus.pump(0u, [&](net::EndpointId, std::span<const uint8_t> bytes) {
    received.assign(bytes.begin(), bytes.end());
  });
  const auto decoded = net::decodeHello(received.data(), received.size());
  AC_CHECK(decoded.isOk);
  if (!decoded.isOk) return;
  AC_CHECK_EQ(decoded.value.nonce, 0x11223344u);

  const net::HandshakeOutcome outcome = server.onHello(decoded.value.nonce, decoded.value.token, 0u);
  AC_CHECK(outcome.isAccepted);
  AC_CHECK(outcome.isHelloAckDue);
  AC_CHECK(outcome.session >= 1u);
  AC_CHECK(outcome.session <= net::kSessionIdMax);
  AC_CHECK(outcome.salt != 0u);
  AC_CHECK_EQ(outcome.token, outcome.salt ^ decoded.value.nonce);
  AC_CHECK(server.validateSession(outcome.session).isAccepted);
  AC_CHECK(server.validateSession(0u).isBadSession);
  hasBadSession = server.validateSession(static_cast<uint16_t>(outcome.session + 1u)).isBadSession;
  AC_CHECK(hasBadSession);

  // 回 HelloAck：session 填分配值、salt 按会话层独立计数器派生
  std::vector<uint8_t> ackBuffer(64u, 0u);
  const std::vector<uint8_t> ack = finish(
      net::encodeHelloAck(makeHeader(PacketType::kHelloAck, net::kFlagReliable, outcome.session, 0u),
                          ReliableExt{1u, 0u, 0u}, net::HelloAckPayload{0u, outcome.salt},
                          ackBuffer.data(), ackBuffer.size()),
      ackBuffer);
  AC_CHECK(!ack.empty());
  const auto ackDecoded = net::decodeHelloAck(ack.data(), ack.size());
  AC_CHECK(ackDecoded.isOk);
  if (!ackDecoded.isOk) return;
  AC_CHECK_EQ(ackDecoded.value.salt, outcome.salt);
  AC_CHECK_EQ(ackDecoded.value.serverTick, 0u);
  AC_CHECK_EQ(net::reconnectTokenFor(decoded.value.nonce, ackDecoded.value.salt), outcome.token);

  // 第二条会话拿到不同的 ID 与 salt
  const net::HandshakeOutcome second = server.onHello(0x55667788u, 0u, 500u);
  AC_CHECK(second.isAccepted);
  AC_CHECK(second.session != outcome.session);
  AC_CHECK(second.salt != outcome.salt);
  AC_CHECK_EQ(server.sessions().size(), 2u);

  // §5.5：客户端 Hello 会按 1s × 5 重发；同一 nonce 在 kHelloDedupMs 内复用同一会话（幂等 HelloAck）
  const net::HandshakeOutcome retry = server.onHello(decoded.value.nonce, 0u, 4000u);
  AC_CHECK(retry.isAccepted);
  AC_CHECK_EQ(retry.session, outcome.session);
  AC_CHECK_EQ(retry.token, outcome.token);
  AC_CHECK_EQ(server.sessions().size(), 2u);
  // 超过去重窗口即视为新的一次握手
  const net::HandshakeOutcome later = server.onHello(decoded.value.nonce, 0u, 5001u);
  AC_CHECK(later.isAccepted);
  AC_CHECK(later.session != outcome.session);
  AC_CHECK_EQ(server.sessions().size(), 3u);
}

AC_TEST(transport_loss_causes_resend) {
  net::MemoryTransport bus{0x3u};
  bus.setLossRate(1.0);  // 先全丢
  net::ReliableChannel sender;
  net::ReliableState receiver{};
  const uint32_t msgId = sender.allocateMsgId();
  const std::vector<uint8_t> packet =
      encodeCommandPacket(kSession, 1u, msgId, 0u, 0u, 7);
  AC_CHECK(!packet.empty());
  sender.track(msgId, packet, 0u);

  std::size_t observed = 0u;
  bool isAcked = false;
  uint32_t now = 0u;
  for (int step = 0; step < 60 && !isAcked; ++step) {
    now += kMsPerTick;
    for (const net::ReliableChannel::Entry& entry : sender.collectDue(now)) {
      bus.send(kClient, kServer, entry.packet);
      ++observed;
    }
    bus.pump(now, [&](net::EndpointId, std::span<const uint8_t> bytes) {
      const auto info = net::decodePacket(bytes.data(), bytes.size());
      if (!info.isOk || info.value.payloadBytes == 0u) return;
      AC_CHECK(net::ackOnReceive(receiver, info.value.reliableExt.msgId));
      const std::vector<uint8_t> ack = encodeAckPacket(kSession, receiver);
      if (!ack.empty()) bus.send(kServer, kClient, ack);
    });
    if (step == 20) bus.setLossRate(0.0);  // 之后放行
    if (sender.applyAck(receiver) > 0u) isAcked = true;
  }
  AC_CHECK(isAcked);
  AC_CHECK(observed > 0u);                 // 至少发生过一次重传
  AC_CHECK_EQ(sender.pendingCount(), 0u);
  AC_CHECK_EQ(sender.retransmitCount(), observed);
  AC_CHECK(bus.droppedCount() > 0u);
  AC_CHECK(!sender.isLost());              // 20 tick = 1000ms，仍在重传预算内
}

AC_TEST(transport_out_of_order_dedup) {
  net::MemoryTransport bus{0x9u};
  bus.setReorderRate(1.0);  // 强制与上一条交换
  bus.setLatencyMs(0u, 0u);
  net::ReliableState receiver{};
  std::vector<uint32_t> acked;

  const auto onDeliver = [&](net::EndpointId, std::span<const uint8_t> bytes) {
    const auto info = net::decodePacket(bytes.data(), bytes.size());
    if (!info.isOk) return;
    if (net::ackOnReceive(receiver, info.value.reliableExt.msgId)) {
      acked.push_back(info.value.reliableExt.msgId);
    }
  };

  bus.send(kClient, kServer, encodeCommandPacket(kSession, 1u, 1u, 0u, 0u, 1));
  bus.send(kClient, kServer, encodeCommandPacket(kSession, 2u, 2u, 0u, 0u, 2));
  bus.send(kClient, kServer, encodeCommandPacket(kSession, 3u, 3u, 0u, 0u, 3));
  bus.pump(100u, onDeliver);
  AC_CHECK_EQ(acked.size(), 3u);
  AC_CHECK_EQ(receiver.ackBase, 3u);         // ackBase 只跟最大 msgId 走
  AC_CHECK_EQ(receiver.ackBits, 0x7u);       // 1、2 都在窗口里（bit2 是 msgId 0 的公式产物）

  // 重复投递同样的三个包：全部判重复，ack 状态不变
  bus.send(kClient, kServer, encodeCommandPacket(kSession, 1u, 1u, 0u, 0u, 1));
  bus.send(kClient, kServer, encodeCommandPacket(kSession, 2u, 2u, 0u, 0u, 2));
  bus.send(kClient, kServer, encodeCommandPacket(kSession, 3u, 3u, 0u, 0u, 3));
  bus.pump(200u, onDeliver);
  AC_CHECK_EQ(acked.size(), 3u);
  AC_CHECK_EQ(receiver.ackBase, 3u);
  AC_CHECK_EQ(receiver.ackBits, 0x7u);

  // ackOnly 包把接收状态原样带回发送方（线上形状与 S03 编解码一致）
  const std::vector<uint8_t> ack = encodeAckPacket(kSession, receiver);
  AC_CHECK(!ack.empty());
  const auto info = net::decodePacket(ack.data(), ack.size());
  AC_CHECK(info.isOk);
  if (!info.isOk) return;
  AC_CHECK_EQ(info.value.reliableExt.ackBase, 3u);
  AC_CHECK_EQ(info.value.reliableExt.ackBits, 0x7u);
  AC_CHECK_EQ(info.value.payloadBytes, 0u);
  AC_CHECK(net::decodeKeepAlive(ack.data(), ack.size()) == net::DecodeFailure::kOk);
  AC_CHECK(!net::isRetransmitTracked(info.value.header.flags, 0u));  // ackOnly 不进重传表
}

AC_TEST(transport_split_logical_message) {
  const std::vector<uint8_t> message = makeMessage(5000u);
  const std::vector<std::vector<uint8_t>> slices =
      net::splitMessage(makeHeader(PacketType::kSnapshot, 0u, kSession, 9u), ReliableExt{}, message, 33u);
  AC_CHECK_EQ(slices.size(), 5u);

  net::MemoryTransport bus{0x51CEu};
  bus.setLossRate(0.2);
  bus.setLatencyMs(20u, 5u);
  net::Reassembler reassembler;
  std::vector<uint8_t> assembled;
  bool isDone = false;
  uint32_t lastRoundMs = 0u;
  std::size_t rounds = 0u;

  for (uint32_t tick = 1u; tick <= 400u && !isDone; ++tick) {
    const uint32_t now = tick * kMsPerTick;
    if (now - lastRoundMs >= net::kRtoTableMs[0]) {  // 每 200ms 重发一轮全部切片
      lastRoundMs = now;
      ++rounds;
      for (const std::vector<uint8_t>& slice : slices) bus.send(kClient, kServer, slice);
    }
    bus.pump(now, [&](net::EndpointId, std::span<const uint8_t> bytes) {
      if (isDone) return;  // 已收齐：后续重复切片不再交给重组器
      const auto info = net::decodePacket(bytes.data(), bytes.size());
      if (!info.isOk || !info.value.hasFragmentHeader) return;
      const auto status = reassembler.add(fragmentKeyOf(info.value), info.value.fragment.fragIndex,
                                          info.value.fragment.fragCount,
                                          std::span<const uint8_t>(bytes.data() + info.value.payloadOffset,
                                                                   info.value.payloadBytes),
                                          now / kMsPerTick, assembled);
      if (status == net::Reassembler::Status::kComplete) isDone = true;
    });
  }
  AC_CHECK(isDone);
  AC_CHECK(rounds > 1u);                    // 20% 丢包下至少重发过一轮
  AC_CHECK_EQ(assembled.size(), message.size());
  AC_CHECK(std::equal(assembled.begin(), assembled.end(), message.begin()));
  AC_CHECK_EQ(reassembler.completedCount(), 1u);
  AC_CHECK_EQ(reassembler.timedOutCount(), 0u);
  AC_CHECK(bus.droppedCount() > 0u);
}

AC_TEST(transport_heartbeat_every_500ms) {
  net::KeepAliveTimer timer{0u};
  std::vector<uint32_t> sends;
  for (uint32_t tick = 0u; tick <= 1000u; ++tick) {  // 0..50000ms
    const uint32_t now = tick * kMsPerTick;
    if (timer.due(now)) {
      sends.push_back(now);
      timer.markSent(now);
    }
  }
  AC_CHECK_EQ(sends.size(), 100u);  // 1000 tick = 50s / 500ms
  for (std::size_t i = 1u; i < sends.size(); ++i) {
    AC_CHECK_EQ(sends[i] - sends[i - 1u], net::kKeepAliveMs);  // 间隔恒为 500ms（±0 tick）
  }
  AC_CHECK_EQ(sends[0], net::kKeepAliveMs);
  AC_CHECK_EQ(timer.sentCount(), sends.size());
  // 收到任意合法包只刷新 lastRecvMs，不影响心跳节拍
  timer.onAnyPacket(1000u);
  AC_CHECK(!timer.isOffline(3999u));
  AC_CHECK(timer.isOffline(4000u));
  // 心跳包自身是 reliable|ackOnly、0 载荷，不进重传表
  const std::vector<uint8_t> keepAlive = encodeAckPacket(kSession, ReliableState{});
  AC_CHECK(!keepAlive.empty());
  const auto info = net::decodePacket(keepAlive.data(), keepAlive.size());
  AC_CHECK(info.isOk);
  if (!info.isOk) return;
  AC_CHECK(info.value.header.isReliable());
  AC_CHECK(info.value.header.isAckOnly());
  AC_CHECK(!net::isRetransmitTracked(info.value.header.flags, info.value.payloadBytes));
}

AC_TEST(transport_three_second_silence_is_offline) {
  net::HandshakeServer server{};
  const net::HandshakeOutcome hello = server.onHello(0xABCDEF01u, 0u, 0u);
  AC_CHECK(hello.isAccepted);
  net::SessionRecord* record = server.sessions().find(hello.session);
  AC_CHECK(record != nullptr);
  if (record == nullptr) return;

  // 每 100ms 有一个合法包：永远不判断线，心跳按 500ms 发
  std::size_t heartbeats = 0u;
  for (uint32_t tick = 0u; tick <= 1000u; ++tick) {  // 0..50000ms
    const uint32_t now = tick * kMsPerTick;
    record = server.sessions().find(hello.session);
    AC_CHECK(record != nullptr);
    if (record == nullptr) return;
    record->keepAlive.onAnyPacket(now);
    if (record->keepAlive.due(now)) {
      record->keepAlive.markSent(now);
      ++heartbeats;
    }
  }
  AC_CHECK_EQ(heartbeats, 100u);
  AC_CHECK(!server.sessions().find(hello.session)->keepAlive.isOffline(52999u));

  // 之后彻底静默：从最后一个合法包（t = 50000ms）起 3000ms 判断线并进入宽限期
  const uint32_t lastPacketMs = 1000u * kMsPerTick;
  AC_CHECK(!server.sessions().find(hello.session)->keepAlive.isOffline(lastPacketMs + 2999u));
  const net::SessionTick offline = server.sessions().tick(lastPacketMs + 3000u);
  AC_CHECK_EQ(offline.wentOffline, 1u);
  record = server.sessions().find(hello.session);
  AC_CHECK(record != nullptr);
  if (record == nullptr) return;
  AC_CHECK(record->isResumable());
  AC_CHECK_EQ(server.sessions().connectedCount(), 0u);
}

AC_TEST(transport_resume_within_window) {
  net::MemoryTransport bus{0x22u};
  net::HandshakeServer server{};
  const net::HandshakeOutcome hello = server.onHello(0x0F1E2D3Cu, 0u, 0u);
  AC_CHECK(hello.isAccepted);
  AC_CHECK_EQ(server.sessions().tick(3000u).wentOffline, 1u);

  // 客户端在宽限期内用真实字节发 Resume
  const std::vector<uint8_t> resume = encodeResumePacket(hello.session, hello.token);
  AC_CHECK_EQ(resume.size(), net::kCommonHeaderBytes + net::kReliableExtBytes + net::kResumePayloadBytes);
  bus.send(kClient, kServer, resume);
  std::vector<uint8_t> received;
  bus.pump(9000u, [&](net::EndpointId, std::span<const uint8_t> bytes) {
    received.assign(bytes.begin(), bytes.end());
  });
  const auto info = net::decodePacket(received.data(), received.size());
  AC_CHECK(info.isOk);
  if (!info.isOk) return;
  const net::SessionValidation inGrace = server.validateSession(info.value.header.session);
  AC_CHECK(inGrace.isAccepted);     // §5.2：宽限期仍持有绑定，不算 kBadSession
  AC_CHECK(inGrace.isGracePeriod);  // 是否按 C03 §5.3 复活由调用方决定
  AC_CHECK(!inGrace.isBadSession);
  const auto decoded = net::decodeResume(received.data(), received.size());
  AC_CHECK(decoded.isOk);
  if (!decoded.isOk) return;

  const net::HandshakeOutcome resumed = server.onResume(info.value.header.session, decoded.value.token, 9000u);
  AC_CHECK(resumed.isAccepted);
  AC_CHECK(resumed.isResumed);
  AC_CHECK(resumed.isFullSnapshotDue);
  const net::SessionValidation afterResume = server.validateSession(info.value.header.session);
  AC_CHECK(afterResume.isAccepted);      // 恢复后普通包放行
  AC_CHECK(!afterResume.isGracePeriod);
  AC_CHECK_EQ(server.sessions().connectedCount(), 1u);
  AC_CHECK_EQ(server.sessions().graceCount(), 0u);
}

AC_TEST(transport_stale_token_rejected) {
  net::HandshakeServer server{};
  const net::HandshakeOutcome hello = server.onHello(0x77777777u, 0u, 0u);
  AC_CHECK(hello.isAccepted);
  AC_CHECK_EQ(server.sessions().tick(5000u).wentOffline, 1u);

  const std::vector<uint8_t> resume = encodeResumePacket(hello.session, hello.token ^ 0xDEADBEEFu);
  const auto decoded = net::decodeResume(resume.data(), resume.size());
  AC_CHECK(decoded.isOk);
  if (!decoded.isOk) return;
  const net::HandshakeOutcome rejected = server.onResume(hello.session, decoded.value.token, 8000u);
  AC_CHECK(!rejected.isAccepted);
  AC_CHECK(rejected.isDisconnectDue);
  AC_CHECK(rejected.reason == net::DisconnectReason::kTokenInvalid);

  // 服务器回 Disconnect(reason = 2)：客户端按 S03 编解码拿到同一个值
  const std::vector<uint8_t> disconnect = encodeDisconnectPacket(hello.session, rejected.reason);
  AC_CHECK(!disconnect.empty());
  const auto reason = net::decodeDisconnect(disconnect.data(), disconnect.size());
  AC_CHECK(reason.isOk);
  if (!reason.isOk) return;
  AC_CHECK_EQ(reason.value.reason, 2u);
  AC_CHECK(reason.value.reason == static_cast<uint8_t>(net::DisconnectReason::kTokenInvalid));
  AC_CHECK_EQ(static_cast<uint8_t>(net::DisconnectReason::kVersionMismatch), 1u);
  AC_CHECK_EQ(static_cast<uint8_t>(net::DisconnectReason::kTimeout), 3u);
  AC_CHECK_EQ(static_cast<uint8_t>(net::DisconnectReason::kServerShutdown), 4u);
  AC_CHECK_EQ(static_cast<uint8_t>(net::DisconnectReason::kMalformedPacket), 5u);
  AC_CHECK_EQ(static_cast<uint8_t>(net::DisconnectReason::kRateLimited), 6u);
  AC_CHECK_EQ(static_cast<uint8_t>(net::DisconnectReason::kSlowConsumer), 7u);
  AC_CHECK(server.sessions().find(hello.session) == nullptr);  // 令牌错误即释放名额
}

// ---------- 套接字缝（不在 §6 五组内，单独一条）----------

AC_TEST(udp_socket_loopback_roundtrip) {
  net::UdpSocket server;
  net::UdpSocket client;
  AC_CHECK(server.bind(0u));
  AC_CHECK(client.bind(0u));
  AC_CHECK(server.boundPort() != 0u);
  AC_CHECK(client.boundPort() != 0u);
  AC_CHECK(server.boundPort() != client.boundPort());

  const std::vector<uint8_t> payload{0x11u, 0x22u, 0x33u, 0x44u, 0x55u};
  const net::Endpoint target{0x7F000001u, server.boundPort()};
  AC_CHECK_EQ(client.sendTo(target, payload), static_cast<int>(payload.size()));

  AC_CHECK(server.poll(1000));
  net::Endpoint from{};
  std::vector<uint8_t> buffer(64u, 0u);
  const int received = server.recvFrom(from, buffer);
  AC_CHECK_EQ(received, static_cast<int>(payload.size()));
  if (received > 0) {
    AC_CHECK(std::equal(buffer.begin(), buffer.begin() + received, payload.begin()));
  }
  AC_CHECK_EQ(from.port, client.boundPort());
  AC_CHECK_EQ(from.ipv4, 0x7F000001u);
  // 非阻塞：第二次 poll 立即返回 false，不卡住
  AC_CHECK(!server.poll(0));
  server.close();
  client.close();
  AC_CHECK(!server.isOpen());
}
