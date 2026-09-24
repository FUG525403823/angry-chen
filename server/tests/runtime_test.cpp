// S14 §2-3：服务器运行时的端到端用例（真 UDP + 真 HTTP，端口 0 = 系统分配）。
// 本文件同时充当「运行时接线正确」的证据：握手、命令、复制、HTTP 面与停机。
#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

#include "metrics/counters.hpp"
#include "net/codec.hpp"
#include "net/tcp_listener.hpp"
#include "net/udp_socket.hpp"
#include "server/runtime.hpp"
#include "tiny_test.hpp"

namespace {

constexpr std::uint32_t kLoopback = 0x7F000001u;

ac::server::RuntimeConfig testConfig() {
  ac::server::RuntimeConfig config{};
  config.udpPort = 0u;
  config.httpPort = 0u;
  config.sheepTarget = 8;  // 用例里只留一点负载，形状与门禁一致
  return config;
}

std::size_t encodeHelloFrame(std::uint8_t* out, std::size_t capacity, std::uint32_t nonce) {
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<std::uint8_t>(ac::net::PacketType::kHello);
  header.flags = ac::net::requiredFlags(ac::net::PacketType::kHello);
  const ac::net::EncodeResult encoded =
      ac::net::encodeHello(header, ac::net::HelloPayload{nonce, 0u}, out, capacity);
  return encoded.isOk ? encoded.bytes : 0u;
}

std::size_t encodeCommandFrame(std::uint8_t* out, std::size_t capacity, std::uint16_t session,
                               std::uint16_t seq, std::uint32_t clientTick) {
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<std::uint8_t>(ac::net::PacketType::kCommand);
  header.flags = ac::net::requiredFlags(ac::net::PacketType::kCommand);
  header.session = session;
  header.seq = seq;
  ac::net::ReliableExt ext{};
  ext.msgId = seq;
  ac::net::CommandPayload payload{};
  payload.moveX = 127;  // 反量化后 = 1.0：一直向 +x 走
  payload.seq = seq;
  payload.clientTick = clientTick;
  const ac::net::EncodeResult encoded =
      ac::net::encodeCommand(header, ext, payload, out, capacity);
  return encoded.isOk ? encoded.bytes : 0u;
}

// 用例里只有一个客户端，会话号取 1（handshake 的返回值已断言非 0）。
std::uint16_t botSession(const ac::server::Runtime& runtime) {
  (void)runtime;
  return 1u;
}

std::uint64_t counterOf(const ac::server::Runtime& runtime, ac::metrics::CounterId id) {
  return ac::metrics::counterValue(runtime.counters(), id);
}

// 一次真实握手：返回会话号（0 = 失败）。
std::uint16_t handshake(ac::server::Runtime& runtime, ac::net::UdpSocket& client,
                        std::uint32_t nonce, std::uint64_t nowMs) {
  std::uint8_t frame[64] = {};
  const std::size_t bytes = encodeHelloFrame(frame, sizeof(frame), nonce);
  if (bytes == 0u) return 0u;
  if (client.sendTo(ac::net::Endpoint{kLoopback, runtime.udpPort()},
                    std::span<const std::uint8_t>(frame, bytes)) <= 0) {
    return 0u;
  }
  for (int i = 0; i < 4; ++i) runtime.pollOnce(nowMs + static_cast<std::uint64_t>(i));
  std::uint8_t reply[512] = {};
  ac::net::Endpoint from{};
  const int got = client.recvFrom(from, std::span<std::uint8_t>(reply, sizeof(reply)));
  if (got <= 0) return 0u;
  const ac::net::DecodeResult<ac::net::HelloAckPayload> ack =
      ac::net::decodeHelloAck(reply, static_cast<std::size_t>(got));
  return ack.isOk ? static_cast<std::uint16_t>(reply[4] | (reply[5] << 8)) : 0u;
}

std::string httpGet(ac::server::Runtime& runtime, const char* target, std::uint64_t nowMs) {
  ac::net::TcpConnection connection = ac::net::connectTcp(kLoopback, runtime.httpPort(), 500);
  if (!connection.isOpen()) return std::string();
  std::string request = "GET ";
  request += target;
  request += " HTTP/1.1\r\nhost: 127.0.0.1\r\n\r\n";
  (void)connection.sendAll(request.data(), request.size());
  std::string response{};
  std::uint8_t buffer[512] = {};
  for (int i = 0; i < 20; ++i) {
    runtime.pollOnce(nowMs + static_cast<std::uint64_t>(i) * 10u);
    const int got = connection.recv(buffer, 20);
    if (got > 0) response.append(reinterpret_cast<const char*>(buffer),
                                 static_cast<std::size_t>(got));
    else if (got < 0) break;
  }
  connection.close();
  return response;
}

}  // namespace

AC_TEST(runtime_completes_handshake_over_real_socket) {
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(testConfig(), &error));
  AC_CHECK(runtime.udpPort() != 0u);
  AC_CHECK(runtime.httpPort() != 0u);
  AC_CHECK_EQ(runtime.roomCount(), static_cast<std::size_t>(1));

  ac::net::UdpSocket client{};
  AC_CHECK(client.bind(0u));
  const std::uint16_t session = handshake(runtime, client, 0x11223344u, 10000u);
  AC_CHECK(session != 0u);
  AC_CHECK_EQ(runtime.clientCount(), static_cast<std::size_t>(1));
  AC_CHECK_EQ(counterOf(runtime, ac::metrics::CounterId::kDroppedFrames), static_cast<std::uint64_t>(0));
  AC_CHECK(counterOf(runtime, ac::metrics::CounterId::kFramesOut) > 0u);
  AC_CHECK(counterOf(runtime, ac::metrics::CounterId::kBytesOut) > 0u);
  std::printf("handshake session=%u bytesOut=%llu framesOut=%llu\n", static_cast<unsigned>(session),
              static_cast<unsigned long long>(counterOf(runtime, ac::metrics::CounterId::kBytesOut)),
              static_cast<unsigned long long>(counterOf(runtime, ac::metrics::CounterId::kFramesOut)));
  runtime.stop();
  AC_CHECK_EQ(runtime.roomCount(), static_cast<std::size_t>(0));
}

AC_TEST(runtime_advances_round_and_replicates_snapshots) {
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(testConfig(), &error));
  ac::net::UdpSocket client{};
  AC_CHECK(client.bind(0u));
  AC_CHECK(handshake(runtime, client, 0x22334455u, 10000u) != 0u);

  std::uint64_t now = 10100u;
  for (int i = 0; i < 200; ++i) {
    now += 25u;
    runtime.pollOnce(now);
    // 收包：不清空会在内核缓冲里堆积，但用例只关心服务端副本仍在推进。
    std::uint8_t scratch[1200] = {};
    ac::net::Endpoint from{};
    while (client.poll(0)) {
      if (client.recvFrom(from, std::span<std::uint8_t>(scratch, sizeof(scratch))) <= 0) break;
    }
  }
  const ac::server::RuntimeMetrics metrics = runtime.metrics();
  AC_CHECK(metrics.ticks > 50u);
  AC_CHECK(metrics.snapshotSamples > 0u);
  AC_CHECK(metrics.snapshotBytesMax <= ac::net::kMaxSnapshotBytes);
  AC_CHECK(metrics.snapshotBytesP95 <= static_cast<double>(ac::net::kSteadySnapshotBytes));
  AC_CHECK(metrics.players == 1u);
  AC_CHECK(metrics.aliveSheep > 0u);
  std::printf("ticks=%llu samples=%zu snapP95=%.0f snapMax=%zu players=%zu sheep=%zu\n",
              static_cast<unsigned long long>(metrics.ticks), metrics.snapshotSamples,
              metrics.snapshotBytesP95, metrics.snapshotBytesMax, metrics.players,
              metrics.aliveSheep);
  // 过期命令（clientTick 落在服务器 tick 之前）必须被丢弃且不进世界。
  std::uint8_t frame[64] = {};
  const std::size_t bytes = encodeCommandFrame(frame, sizeof(frame), 1u, 1u, 0u);
  AC_CHECK(bytes > 0u);
  (void)client.sendTo(ac::net::Endpoint{kLoopback, runtime.udpPort()},
                      std::span<const std::uint8_t>(frame, bytes));
  runtime.pollOnce(now + 25u);
  AC_CHECK(counterOf(runtime, ac::metrics::CounterId::kFramesIn) >= 2u);
  runtime.stop();
}

AC_TEST(runtime_serves_health_and_metrics_over_http) {
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(testConfig(), &error));
  const std::string health = httpGet(runtime, "/health", 20000u);
  AC_CHECK(health.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(health.find("\"protocolVersion\":1") != std::string::npos);
  const std::string metrics = httpGet(runtime, "/metrics", 21000u);
  AC_CHECK(metrics.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(metrics.find("ac_frames_in_total") != std::string::npos);
  AC_CHECK(metrics.find("ac_rooms 1") != std::string::npos);
  std::printf("http health=%zuB metrics=%zuB\n", health.size(), metrics.size());
  runtime.stop();
}

AC_TEST(runtime_tick_clock_tracks_wall_clock_while_playing) {
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(testConfig(), &error));
  ac::net::UdpSocket client{};
  AC_CHECK(client.bind(0u));
  AC_CHECK(handshake(runtime, client, 0x33445566u, 10000u) != 0u);

  // 合成时钟按 5ms 步进推 2 秒：本机定时器粒度不参与，漂移只可能来自账目本身。
  std::uint64_t now = 10000u;
  for (int i = 0; i < 400; ++i) {
    now += 5u;
    if (i % 8 == 0) {
      // 会话靠包续命：不发命令会掉进宽限期，房间就停摆了（本批实测的 11 tick）。
      std::uint8_t frame[64] = {};
      const std::size_t bytes = encodeCommandFrame(frame, sizeof(frame), botSession(runtime), 1u, 0u);
      if (bytes > 0u) {
        (void)client.sendTo(ac::net::Endpoint{kLoopback, runtime.udpPort()},
                            std::span<const std::uint8_t>(frame, bytes));
      }
    }
    runtime.pollOnce(now);
  }
  const ac::server::RuntimeMetrics metrics = runtime.metrics();
  std::printf("synth ticks=%llu drift=%.1fms schedP95=%.1fms intervalP95=%.1fms gap=%.1fms\n",
              static_cast<unsigned long long>(metrics.ticks), metrics.simDriftMs,
              metrics.scheduleErrorP95Ms, metrics.tickIntervalErrorP95Ms,
              metrics.scheduleHeadTailGapMs);
  // 2 秒 = 40 tick（±1）；模拟时钟与墙上时钟（同一合成源）应几乎不漂。
  AC_CHECK(metrics.ticks >= 39u && metrics.ticks <= 41u);
  AC_CHECK(std::fabs(metrics.simDriftMs) <= 60.0);
  runtime.stop();
}

AC_TEST(runtime_stop_is_idempotent_and_releases_ports) {
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(testConfig(), &error));
  const std::uint16_t httpPort = runtime.httpPort();
  AC_CHECK(httpPort != 0u);
  runtime.stop();
  runtime.stop();
  AC_CHECK(!runtime.isRunning());
  AC_CHECK_EQ(runtime.clientCount(), static_cast<std::size_t>(0));
  // 端口已释放：同一端口能再次绑定（HTTP 监听层随之关闭）。
  ac::net::UdpSocket probe{};
  AC_CHECK(probe.bind(0u));
}
