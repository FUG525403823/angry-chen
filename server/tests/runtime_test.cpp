// S14 §2-3：服务器运行时的端到端用例（真 UDP + 真 HTTP，端口 0 = 系统分配）。
// 本文件同时充当「运行时接线正确」的证据：握手、命令、复制、HTTP 面与停机。
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "config/player.hpp"
#include "metrics/counters.hpp"
#include "net/codec.hpp"
#include "net/tcp_listener.hpp"
#include "net/udp_socket.hpp"
#include "server/runtime.hpp"
#include <filesystem>

#include "test_io.hpp"
#include "tiny_test.hpp"
#include "tmp_workdir.hpp"

namespace {

constexpr std::uint32_t kLoopback = 0x7F000001u;

ac::server::RuntimeConfig testConfig() {
  ac::server::RuntimeConfig config{};
  config.udpPort = 0u;
  config.httpPort = 0u;
  config.sheepTarget = 8;  // 用例里只留一点负载，形状与门禁一致
  // CI runner 不是 root：不能落到 AC_DATA_DIR 的 /var/lib/angry-chen 默认值（README §19.2）。
  // 用构建目录下的相对路径，并自己建目录（MatchStore 不会替调用方建）。
  config.dataDir = "ac-runtime-test-data";
  // ADR-013：这批用例走的是"装载路径"（客户端不发 Ready 位，直接看对局流量），所以显式开自动准备；
  // 产品默认（false）由 serve_cli 用例守着。
  config.isAutoReady = true;
  std::error_code code;
  std::filesystem::create_directories(config.dataDir, code);
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
                               std::uint16_t seq, std::uint32_t clientTick,
                               std::uint8_t buttons = 0u, std::int8_t moveX = 127) {
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<std::uint8_t>(ac::net::PacketType::kCommand);
  header.flags = ac::net::requiredFlags(ac::net::PacketType::kCommand);
  header.session = session;
  header.seq = seq;
  ac::net::ReliableExt ext{};
  ext.msgId = seq;
  ac::net::CommandPayload payload{};
  payload.moveX = static_cast<std::uint8_t>(moveX);  // 127 = 反量化后 1.0：一直向 +x 走
  payload.buttons = buttons;
  payload.seq = seq;
  payload.clientTick = clientTick;
  const ac::net::EncodeResult encoded =
      ac::net::encodeCommand(header, ext, payload, out, capacity);
  return encoded.isOk ? encoded.bytes : 0u;
}

// §5.4 的 kJoin（type 11，reliable）：昵称 1..12 字节；房内改名走这一条（准入在 Hello 那一步）。
std::size_t encodeJoinFrame(std::uint8_t* out, std::size_t capacity, std::uint16_t session,
                            std::uint16_t seq, const char* name) {
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<std::uint8_t>(ac::net::PacketType::kJoin);
  header.flags = ac::net::requiredFlags(ac::net::PacketType::kJoin);
  header.session = session;
  header.seq = seq;
  ac::net::ReliableExt ext{};
  ext.msgId = seq;
  const ac::net::EncodeResult encoded =
      ac::net::encodeJoin(header, ext, ac::net::JoinPayload{std::string(name)}, out, capacity);
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

// 一次真实握手：返回会话号（0 = 失败）。`serverTick` 非空时带出 HelloAck 里的服务器 tick
// （§5.3 的 clientTick 必须落在 [serverTick - kClientTickSlackTicks, serverTick] 窗口内，命令才不会被 kStaleTick/kFutureTick 丢掉）。
std::uint16_t handshake(ac::server::Runtime& runtime, ac::net::UdpSocket& client,
                        std::uint32_t nonce, std::uint64_t nowMs, std::uint32_t* serverTick = nullptr) {
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
  if (!ack.isOk) return 0u;
  if (serverTick != nullptr) *serverTick = ack.value.serverTick;
  return static_cast<std::uint16_t>(reply[4] | (reply[5] << 8));
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

AC_TEST(runtime_reassembles_client_fragments_without_dropping_them) {
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(testConfig(), &error));
  ac::net::UdpSocket client{};
  AC_CHECK(client.bind(0u));
  const std::uint16_t session = handshake(runtime, client, 0x44556677u, 10000u);
  AC_CHECK(session != 0u);
  const ac::net::Endpoint endpoint{kLoopback, runtime.udpPort()};
  std::uint64_t now = 10100u;
  const std::uint64_t dropped = counterOf(runtime, ac::metrics::CounterId::kDroppedFrames);

  ac::net::PacketHeader messageHeader{};
  messageHeader.version = ac::net::kProtocolVersion;
  messageHeader.type = static_cast<std::uint8_t>(ac::net::PacketType::kCommand);
  messageHeader.flags = ac::net::requiredFlags(ac::net::PacketType::kCommand);
  messageHeader.session = session;
  messageHeader.seq = 1u;
  const ac::net::ReliableExt ext{1u, 0u, 0u};

  // ① 逻辑消息 1400 B → 2 片；只送第 0 片：分片包本身**不**计丢弃帧（未收齐时静默挂起）。
  std::uint8_t junk[1400] = {};
  const std::vector<std::vector<std::uint8_t>> slices = ac::net::splitMessage(
      messageHeader, ext, std::span<const std::uint8_t>(junk, sizeof(junk)), 7u);
  AC_CHECK_EQ(slices.size(), 2u);
  AC_CHECK(client.sendTo(endpoint, std::span<const std::uint8_t>(slices[0])) > 0);
  runtime.pollOnce(now += 25u);
  AC_CHECK_EQ(counterOf(runtime, ac::metrics::CounterId::kDroppedFrames), dropped);

  // ② 收齐：重组结果里是一帧版本非法的坏包 —— 切片仍不计丢弃帧，只有被重走分派的内层坏包按普通坏包计 1。
  AC_CHECK(client.sendTo(endpoint, std::span<const std::uint8_t>(slices[1])) > 0);
  runtime.pollOnce(now += 25u);
  AC_CHECK_EQ(counterOf(runtime, ac::metrics::CounterId::kDroppedFrames), dropped + 1u);

  // ③ 单片包裹一帧真 KeepAlive（fragCount = 1）：重组 → 重新分派 → 白名单内的包正常消费，丢弃帧不再增长。
  const std::uint64_t beforeKeepAlive = counterOf(runtime, ac::metrics::CounterId::kDroppedFrames);
  std::uint8_t keepAlive[ac::net::kCommonHeaderBytes + ac::net::kReliableExtBytes] = {};
  ac::net::PacketHeader keepAliveHeader{};
  keepAliveHeader.version = ac::net::kProtocolVersion;
  keepAliveHeader.type = static_cast<std::uint8_t>(ac::net::PacketType::kKeepAlive);
  keepAliveHeader.flags = ac::net::requiredFlags(ac::net::PacketType::kKeepAlive);
  keepAliveHeader.session = session;
  keepAliveHeader.seq = 2u;
  {
    ac::net::ByteWriter writer(keepAlive, sizeof(keepAlive));
    ac::net::writeHeader(writer, keepAliveHeader);
    ac::net::writeReliableExt(writer, ac::net::ReliableExt{2u, 0u, 0u});
    AC_CHECK(!writer.isOverflow);
  }
  const std::vector<std::vector<std::uint8_t>> wrapped = ac::net::splitMessage(
      keepAliveHeader, ac::net::ReliableExt{2u, 0u, 0u},
      std::span<const std::uint8_t>(keepAlive, sizeof(keepAlive)), 9u);
  AC_CHECK_EQ(wrapped.size(), 1u);
  AC_CHECK(client.sendTo(endpoint, std::span<const std::uint8_t>(wrapped[0])) > 0);
  runtime.pollOnce(now += 25u);
  AC_CHECK_EQ(counterOf(runtime, ac::metrics::CounterId::kDroppedFrames), beforeKeepAlive);
  std::printf("fragments accepted dropped=%llu (junk inner +1)\n",
              static_cast<unsigned long long>(counterOf(runtime, ac::metrics::CounterId::kDroppedFrames)));
  runtime.stop();
}

AC_TEST(runtime_serves_health_and_metrics_over_http) {
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(testConfig(), &error));
  // process_ 只在 Runtime::publishMetrics 里写（运行循环每次 pollOnce 调它），所以先推一次
  // 时钟，两个端点的数字才都来自「已经填过的快照」而不是默认值。
  runtime.pollOnce(19990u);
  const std::string health = httpGet(runtime, "/health", 20000u);
  AC_CHECK(health.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(health.find("\"protocolVersion\":1") != std::string::npos);
  const std::string metrics = httpGet(runtime, "/metrics", 21000u);
  AC_CHECK(metrics.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(metrics.find("ac_frames_in_total") != std::string::npos);
  AC_CHECK(metrics.find("ac_rooms 1") != std::string::npos);
  // C3：ProcessSnapshot 的生产写入方是 Runtime::publishMetrics（runtime.cpp），两个读方
  // （/metrics 渲染、/health JSON）必须给出同一份快照的数 —— 这里在真运行时上再钉一遍。
  const auto healthNumber = [&health](std::string_view key) -> std::string {
    const std::string needle = "\"" + std::string(key) + "\":";
    const std::size_t at = health.find(needle);
    if (at == std::string::npos) return {};
    const std::size_t begin = at + needle.size();
    const std::size_t end = health.find_first_of(",}", begin);
    return health.substr(begin, end - begin);
  };
  const auto metricsNumber = [&metrics](std::string_view name) -> std::string {
    const std::string needle = "\n" + std::string(name) + " ";  // 带换行：避开 # HELP 行
    const std::size_t at = metrics.find(needle);
    if (at == std::string::npos) return {};
    const std::size_t begin = at + needle.size();
    const std::size_t end = metrics.find_first_of("\r\n", begin);
    return metrics.substr(begin, end - begin);
  };
  AC_CHECK_EQ(healthNumber("rooms"), metricsNumber("ac_rooms"));
  AC_CHECK_EQ(healthNumber("connections"), metricsNumber("ac_connections"));
  AC_CHECK_EQ(healthNumber("players"), metricsNumber("ac_players"));
  AC_CHECK_EQ(healthNumber("graceActive"), metricsNumber("ac_grace_active"));
  AC_CHECK_EQ(healthNumber("recordsRetained"), metricsNumber("ac_records_retained"));
  AC_CHECK_EQ(healthNumber("rooms"), std::string("1"));
  // ADR-012：空房里一个人也没有 ⇒ 两个面都必须报 0。这里曾经是 1（`activePlayerCount` 的"空房夹到 1"
  // 是波次预算用的口径，被误用到了诊断面），会把"没人连"和"有 1 个人"这两件事混成同一行。
  AC_CHECK_EQ(healthNumber("players"), std::string("0"));
  AC_CHECK_EQ(metricsNumber("ac_players"), std::string("0"));
  // uptimeSeconds 随请求时刻变化（两次请求各推一段时钟），只断言两端点都有这一项。
  AC_CHECK(!healthNumber("uptimeSeconds").empty());
  AC_CHECK(!metricsNumber("ac_uptime_seconds").empty());
  std::printf("http health=%zuB metrics=%zuB rooms=%s players=%s retained=%s\n", health.size(),
              metrics.size(), healthNumber("rooms").c_str(), healthNumber("players").c_str(),
              healthNumber("recordsRetained").c_str());
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

// F1（清单 D 的「真打一局后 matches.ndjson 仍 0 字节、reports/ 为空」）：对局结束必须真的入库 + 出报告。
AC_TEST(runtime_persists_match_record_and_report_on_match_end) {
  ac::test::TempDir dir("runtime-persist");
  AC_CHECK(dir.isReady());
  const std::string dataDir = dir.file("data");
  std::error_code code;
  std::filesystem::create_directories(dataDir, code);  // MatchStore 不替调用方建目录

  ac::server::RuntimeConfig config = testConfig();
  config.dataDir = dataDir;
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config, &error));
  ac::net::UdpSocket client{};
  AC_CHECK(client.bind(0u));
  AC_CHECK(handshake(runtime, client, 0x55667788u, 10000u) != 0u);

  // 打到 playing（loading 冻结 1500ms，按 25ms 步进给 2.5s 余量）。
  std::uint64_t now = 10000u;
  for (int i = 0; i < 100; ++i) runtime.pollOnce(now += 25u);
  AC_CHECK(runtime.metrics().players >= 1u);

  // 客户端掉线（3s 无包判离线 + 30s 宽限）→ 房间清空 → updateRoom 判结束（winnerTeam = 羊群）。
  // 合成时钟一次跳 2s，最多 25 拍（50s）足以越过 33s 的离线上限与宽限期之和。
  for (int i = 0; i < 25 && runtime.clientCount() > 0u; ++i) runtime.pollOnce(now += 2000u);
  AC_CHECK_EQ(runtime.clientCount(), static_cast<std::size_t>(0));
  runtime.pollOnce(now += 25u);  // 上一拍已 endMatch：这一拍 ensureMatchRunning 落盘

  // ① store 里出现该 record
  const ac::persist::MatchStore* store = runtime.store();
  AC_CHECK(store != nullptr);
  if (store == nullptr) {
    runtime.stop();
    return;
  }
  AC_CHECK_EQ(store->recordCount(), static_cast<std::size_t>(1));
  const std::vector<ac::persist::MatchResultRecord> stored = store->listRecent(1u);
  AC_CHECK_EQ(stored.size(), static_cast<std::size_t>(1));
  if (stored.empty()) {
    runtime.stop();
    return;
  }
  const std::string matchId = stored[0].matchId;
  AC_CHECK(ac::persist::isSafeMatchId(matchId));
  AC_CHECK_EQ(stored[0].winnerTeam, static_cast<std::uint8_t>(1));
  AC_CHECK_EQ(stored[0].players.size(), static_cast<std::size_t>(1));
  AC_CHECK(!stored[0].players[0].name.empty());

  // ② reports/<matchId>.json 落盘且 8 组字段齐全
  const std::string reportPath = dataDir + "/reports/" + matchId + ".json";
  const std::string report = ac::test::readTextFile(reportPath);
  AC_CHECK(!report.empty());
  AC_CHECK(report.find("\"matchId\":\"" + matchId + "\"") != std::string::npos);
  AC_CHECK(report.find(",\"durationMs\":") != std::string::npos);
  for (const char* group : {"\"ticks\":{\"total\":", "\"net\":{\"snapshotBytesAvg\":",
                            "\"fair\":{\"hardCorrectTotal\":", "\"grace\":{\"starts\":",
                            "\"peak\":{\"entities\":"}) {
    AC_CHECK(report.find(group) != std::string::npos);
  }
  const auto numberAfter = [](const std::string& text, const char* needle) -> double {
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) return -1.0;
    return std::strtod(text.c_str() + at + std::strlen(needle), nullptr);
  };
  AC_CHECK(numberAfter(report, "\"peak\":{\"entities\":") >= 1.0);
  AC_CHECK(numberAfter(report, "\"peak\":{\"entities\":") >=
           numberAfter(report, "\"peak\":{\"players\":"));

  // ③ HTTP 层能查到该条（同一进程的 store 出 /api/matches/recent），且 /metrics 的常驻条数跟上
  const std::string recent = httpGet(runtime, "/api/matches/recent?limit=5", now += 100u);
  AC_CHECK(recent.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(recent.find("\"" + matchId + "\"") != std::string::npos);
  const std::string metrics = httpGet(runtime, "/metrics", now += 100u);
  AC_CHECK(metrics.find("ac_records_retained 1") != std::string::npos);
  std::printf("matchId=%s records=%zu reportPath=%s\n", matchId.c_str(), store->recordCount(),
              reportPath.c_str());
  runtime.stop();

  // ④ 重启（重建 Runtime + store）后仍能查到：NDJSON 是持久化的
  ac::server::Runtime restarted;
  std::string restartError;
  AC_CHECK(restarted.start(config, &restartError));
  AC_CHECK(restarted.store() != nullptr);
  if (restarted.store() != nullptr) {
    AC_CHECK(restarted.store()->recordCount() >= static_cast<std::size_t>(1));
  }
  const std::string afterRestart = httpGet(restarted, "/api/matches/recent?limit=5", now += 100u);
  AC_CHECK(afterRestart.rfind("HTTP/1.1 200 OK", 0u) == 0u);
  AC_CHECK(afterRestart.find("\"" + matchId + "\"") != std::string::npos);
  restarted.stop();
}

// S13 云端复验观察项：报告里的 tick 时序曾取到「进程首个 tick → 本 tick」的墙钟（实测 jitterMsP50/P95
// ≈ 2055 ms、scheduleErrorMsP95 ≈ 2056 ms），`workMsP95` 又被整毫秒截断成恒 0。本用例在**同一进程**里
// 连打两局长短不同的对局：每局的时序量必须只反映本局 —— ②`ticks.total × 50` 落在本局 `durationMs` 上
// （不随进程存活时间增长）、①抖动/调度误差远小于本局时长、③工作量是亚毫秒分辨率下的真实测量。
AC_TEST(runtime_report_tick_timing_is_match_scoped) {
  ac::test::TempDir dir("runtime-tick-timing");
  AC_CHECK(dir.isReady());
  const std::string dataDir = dir.file("data");
  std::error_code code;
  std::filesystem::create_directories(dataDir, code);

  ac::server::RuntimeConfig config = testConfig();
  config.dataDir = dataDir;
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config, &error));
  ac::net::UdpSocket client{};
  AC_CHECK(client.bind(0u));

  std::uint64_t now = 10000u;
  std::uint32_t seq = 0u;
  // 会话靠包续命：3s 不发包就掉进宽限期，房间停摆（见 runtime_tick_clock_tracks_wall_clock_while_playing）。
  // 命令本身不推着走（moveX = 0）：本用例量的是**每 tick 的时序**，不是移动；朝 +x 一路冲锋会在
  // 1 秒内撞进羊群被扑倒（allDowned → 开局即结束），把测量窗口一起带走（去重方向修正后实测 18 tick）。
  const auto keepAlive = [&](int index) {
    if (index % 4 != 0) return;
    std::uint8_t frame[64] = {};
    const std::size_t bytes = encodeCommandFrame(frame, sizeof(frame), botSession(runtime),
                                                static_cast<std::uint16_t>(++seq),
                                                static_cast<std::uint32_t>(index), 0u, 0);
    if (bytes > 0u) {
      (void)client.sendTo(ac::net::Endpoint{kLoopback, runtime.udpPort()},
                          std::span<const std::uint8_t>(frame, bytes));
    }
  };
  const auto numberAfter = [](const std::string& text, const char* needle) -> double {
    const std::size_t at = text.find(needle);
    if (at == std::string::npos) return -1.0;
    return std::strtod(text.c_str() + at + std::strlen(needle), nullptr);
  };
  // §5 的 ac_sim_drift_ms 是同一量的瞬时值（未开球 0）：采样本局 playing 段的量值表读数。
  // 取值行行首（`\n` 前缀）而不是裸名字 —— 裸名字会先命中 `# HELP` 那一行，读到说明文字。
  const auto gaugeValue = [&](const char* name) -> double {
    return numberAfter(httpGet(runtime, "/metrics", now), name);
  };
  // 上一局的 MatchState/快照还在 socket 缓冲里：不清空的话握手会把旧包当成 HelloAck 而判失败。
  const auto drainClient = [&]() -> int {
    std::uint8_t scratch[1024] = {};
    ac::net::Endpoint from{};
    int drained = 0;
    for (int i = 0; i < 20000; ++i) {
      if (client.recvFrom(from, std::span<std::uint8_t>(scratch, sizeof(scratch))) <= 0) break;
      ++drained;
    }
    return drained;
  };

  std::string previousMatchId{};
  const int playPolls[2] = {80, 400};
  for (int round = 0; round < 2; ++round) {
    const int drained = drainClient();
    const std::uint16_t session = handshake(runtime, client, 0x55667788u + static_cast<std::uint32_t>(round), now);
    std::printf("round=%d drained=%d session=%u clients=%zu\n", round, drained, session,
                runtime.clientCount());
    AC_CHECK(session != 0u);
    // 25/30ms 交替步进：均匀步进会让迟到量恰好为 0，量不出「抖动」这一项。
    for (int i = 0; i < 100; ++i) {
      runtime.pollOnce(now += (i % 2 == 0 ? 25u : 30u));
      keepAlive(i);
    }
    AC_CHECK(runtime.metrics().players >= 1u);
    double gaugeDriftAbsMax = 0.0;
    for (int i = 0; i < playPolls[round]; ++i) {
      runtime.pollOnce(now += (i % 2 == 0 ? 25u : 30u));
      keepAlive(i);
      if (i % 8 == 0) {
        const double gauge = gaugeValue("\nac_sim_drift_ms ");
        AC_CHECK(gauge <= 0.0);  // 网格落后量为负（正 = 模拟超前，只有让出预算时才会出现）
        if (-gauge > gaugeDriftAbsMax) gaugeDriftAbsMax = -gauge;
      }
    }
    // ④' /metrics 的同名字段与报告同源：不是恒 0，且不越 50ms 上限（S14 G6 的替代判据输入）。
    AC_CHECK(gaugeDriftAbsMax >= 1.0);
    AC_CHECK(gaugeDriftAbsMax <= 50.0);
    // 掉线 → 房间清空 → endMatch。40/50ms 步进：每拍至多执行 1 个 tick（迟到量 ≤ 一步），
    // 不会触发追帧上限（不产生跳过），也不会把一堆 tick 挤进同一拍。
    for (int i = 0; i < 1000 && runtime.clientCount() > 0u; ++i) {
      runtime.pollOnce(now += (i % 2 == 0 ? 40u : 50u));
    }
    AC_CHECK_EQ(runtime.clientCount(), static_cast<std::size_t>(0));
    runtime.pollOnce(now += 25u);  // 上一拍已 endMatch：这一拍 ensureMatchRunning 落盘

    const ac::persist::MatchStore* store = runtime.store();
    AC_CHECK(store != nullptr);
    if (store == nullptr) break;
    const std::vector<ac::persist::MatchResultRecord> recent = store->listRecent(2u);
    const ac::persist::MatchResultRecord* record = nullptr;
    for (const ac::persist::MatchResultRecord& candidate : recent) {
      if (candidate.matchId != previousMatchId) {
        record = &candidate;
        break;
      }
    }
    AC_CHECK(record != nullptr);  // 第二局必须是另一局（同进程内换了 matchId）
    if (record == nullptr) break;
    previousMatchId = record->matchId;

    const std::string report = ac::test::readTextFile(dataDir + "/reports/" + record->matchId + ".json");
    AC_CHECK(!report.empty());
    const double durationMs = numberAfter(report, "\"durationMs\":");
    const double ticksTotal = numberAfter(report, "\"ticks\":{\"total\":");
    const double jitterP50 = numberAfter(report, "\"jitterMsP50\":");
    const double jitterP95 = numberAfter(report, "\"jitterMsP95\":");
    const double scheduleP95 = numberAfter(report, "\"scheduleErrorMsP95\":");
    const double workP95 = numberAfter(report, "\"workMsP95\":");
    const double workP99 = numberAfter(report, "\"workMsP99\":");
    const double driftMax = numberAfter(report, "\"simDriftMsMax\":");
    std::printf("round=%d matchId=%s duration=%.0fms ticks=%0.f jitterP50=%.3f jitterP95=%.3f "
                "schedP95=%.3f workP95=%.3f workP99=%.3f simDriftMsMax=%.1f\n",
                round, record->matchId.c_str(), durationMs, ticksTotal, jitterP50, jitterP95,
                scheduleP95, workP95, workP99, driftMax);
    AC_CHECK(durationMs > 1000.0);
    AC_CHECK(jitterP50 >= 0.0 && jitterP50 <= jitterP95);
    // ① 抖动/调度误差是「本局每 tick 相对 50ms 网格的迟到量」：不超过一步轮询的量级，
    //   远小于对局时长（旧行为取进程首个 tick 起的墙钟，这里是数千毫秒）。
    AC_CHECK(jitterP95 > 0.0);
    AC_CHECK(jitterP95 <= 60.0);
    AC_CHECK(scheduleP95 > 0.0);
    AC_CHECK(scheduleP95 <= 60.0);
    // ② ticks.total 是本局自己的账（含波间 tick、不含 loading 倒计时）：× 50 落在本局 durationMs 上。
    //   旧行为把「进程首个 tick 起的全部 tick」写进来，这一项会随进程存活时间线性增长。
    AC_CHECK(std::fabs(ticksTotal * 50.0 - durationMs) <= 500.0);
    // ③ 工作量有真实测量点（房间更新 <1ms 时旧行为四舍五入成 0）。
    AC_CHECK(workP95 > 0.0);
    AC_CHECK_EQ(workP95 <= workP99, true);
    // ④ 漂移：本局 |漂移| 最大值是真值（非恒 0、非 NaN/缺字段），且不越冻结的 50ms 上限
    //   （S14 G6 替代判据的 |漂移| ≤ 50ms）。两局各自独立 —— 若从进程首个 tick 起累计，
    //   第二局必然超过上限（这也是旧代码恒 0 与「接线后可能变红」的同一处输入）。
    AC_CHECK(driftMax >= 1.0);
    AC_CHECK(driftMax <= 50.0);
    // 本用例的步进（25/30、40/50）下 |漂移| = 房间累加器残差 ≤ 45ms：模拟时长若多算一个 tick，
    // 这一项会正好顶到 50（接线首版就这么错，靠这条断言抓出来）。
    AC_CHECK(driftMax <= 49.0);
  }
  runtime.stop();
}

// ADR-012：房间拒绝必须回执。Hello 路径过去把准入结果丢掉 —— 第 5 个玩家拿到 HelloAck 就再无下文
// （既没 MatchState 也没错误），客户端只能干等，联调实测就是"连上了但大厅里什么都没有"。
AC_TEST(runtime_room_full_answers_disconnect_reason_eight) {
  ac::server::RuntimeConfig config = testConfig();
  config.sheepTarget = 0;
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config, &error));
  const ac::net::Endpoint server{kLoopback, runtime.udpPort()};

  // 4 个居民占满房间（kMaxPlayersPerRoom = 4）：真握手 + kJoin，昵称各不相同。
  std::array<ac::net::UdpSocket, 4> residents{};
  std::uint64_t now = 20000u;
  for (std::size_t i = 0; i < residents.size(); ++i) {
    AC_CHECK(residents[i].bind(0u));
    const std::uint16_t session =
        handshake(runtime, residents[i], 0x20000000u + static_cast<std::uint32_t>(i), now);
    AC_CHECK(session != 0u);
    const std::string name = "p" + std::to_string(i);
    std::uint8_t frame[128] = {};
    const std::size_t bytes = encodeJoinFrame(frame, sizeof(frame), session, 1u, name.c_str());
    AC_CHECK(bytes > 0u);
    AC_CHECK(residents[i].sendTo(server, std::span<const std::uint8_t>(frame, bytes)) > 0);
    for (int k = 0; k < 3; ++k) runtime.pollOnce(now += 30u);
  }
  AC_CHECK_EQ(runtime.metrics().players, 4u);
  AC_CHECK(httpGet(runtime, "/health", now).find("\"players\":4") != std::string::npos);

  // 第 5 个：HelloAck 之后必须紧跟一帧 Disconnect(8)，否则客户端只会干等。
  ac::net::UdpSocket extra{};
  AC_CHECK(extra.bind(0u));
  std::uint8_t request[64] = {};
  const std::size_t requestBytes = encodeHelloFrame(request, sizeof(request), 0x5A5A5A5Au);
  AC_CHECK(requestBytes > 0u);
  AC_CHECK(extra.sendTo(server, std::span<const std::uint8_t>(request, requestBytes)) > 0);
  for (int i = 0; i < 4; ++i) runtime.pollOnce(now + 30u + static_cast<std::uint64_t>(i));
  std::uint8_t reply[512] = {};
  ac::net::Endpoint from{};
  // 心跳（§5.6 的 KeepAlive）会插进这条流里，所以按**类型**取帧，不能假设相邻。取到就返回 true。
  const auto takeFrame = [](ac::net::UdpSocket& socket, ac::net::PacketType wanted,
                            std::uint8_t* out, std::size_t capacity,
                            ac::net::Endpoint& from) -> int {
    for (int i = 0; i < 16; ++i) {
      const int got = socket.recvFrom(from, std::span<std::uint8_t>(out, capacity));
      if (got <= 0) return 0;
      const ac::net::DecodeResult<ac::net::PacketInfo> info =
          ac::net::decodePacket(out, static_cast<std::size_t>(got));
      if (info.isOk && static_cast<ac::net::PacketType>(info.value.header.type) == wanted) {
        return got;
      }
    }
    return 0;
  };
  int got = takeFrame(extra, ac::net::PacketType::kHelloAck, reply, sizeof(reply), from);
  AC_CHECK(got > 0);
  AC_CHECK(ac::net::decodeHelloAck(reply, static_cast<std::size_t>(got)).isOk);
  got = takeFrame(extra, ac::net::PacketType::kDisconnect, reply, sizeof(reply), from);
  AC_CHECK(got > 0);
  const ac::net::DecodeResult<ac::net::DisconnectPayload> first =
      ac::net::decodeDisconnect(reply, static_cast<std::size_t>(got));
  AC_CHECK(first.isOk);
  AC_CHECK_EQ(static_cast<int>(first.value.reason),
              static_cast<int>(ac::net::DisconnectReason::kRoomUnavailable));
  AC_CHECK_EQ(runtime.metrics().players, 4u);
  AC_CHECK(httpGet(runtime, "/health", now + 20u).find("\"players\":4") != std::string::npos);

  // 同一次握手的重发：§5.5 去重会把 HelloAck 幂等再发一遍（准入也被重走一遍），因此这一轮同样要
  // 再补一帧 Disconnect —— 一帧 UDP 丢掉就等于把玩家挂回黑洞，这条断言钉住的就是那件事。
  AC_CHECK(extra.sendTo(server, std::span<const std::uint8_t>(request, requestBytes)) > 0);
  for (int i = 0; i < 4; ++i) runtime.pollOnce(now + 30u + static_cast<std::uint64_t>(i));
  got = takeFrame(extra, ac::net::PacketType::kHelloAck, reply, sizeof(reply), from);
  AC_CHECK(got > 0);
  AC_CHECK(ac::net::decodeHelloAck(reply, static_cast<std::size_t>(got)).isOk);  // 重发的 HelloAck
  got = takeFrame(extra, ac::net::PacketType::kDisconnect, reply, sizeof(reply), from);
  AC_CHECK(got > 0);
  const ac::net::DecodeResult<ac::net::DisconnectPayload> resent =
      ac::net::decodeDisconnect(reply, static_cast<std::size_t>(got));
  AC_CHECK(resent.isOk);
  AC_CHECK_EQ(static_cast<int>(resent.value.reason),
              static_cast<int>(ac::net::DisconnectReason::kRoomUnavailable));
  std::printf("room-full: helloAck + disconnect(8), players 仍为 4, 重发再补 disconnect(8)\n");
  runtime.stop();
}

// 联调实测（`build/joint-run-diag3.out.txt` 第 2c 步）：服务端 `/health` 说 `players=4`，客户端却只
// 解出 `rows=1`（`rawIn` 直方图里 MatchState 只有 2 帧）—— 满员那一条 MatchState **整条广播都没发出去**，
// 根因是 `encodeMatchState` 拿整帧长度去比 `kMatchStateMaxBytes`（那是**载荷**预算），平白砍掉 20 字节包头。
// 这条用例覆盖同一路径的两个真实条件：①**机器人从不发 kJoin**（它只发 Command），行内昵称必须落到
// `player` 兜底值上（0 长度昵称会让客户端整包判 `BadValue`）；②满员 4 行必须真的编码出来。
AC_TEST(runtime_row_without_join_uses_player_fallback) {
  ac::server::RuntimeConfig config = testConfig();
  // 大厅口径（ADR-013 的产品默认）：自动准备开着的话，第一个会话就把对局开了，后到的会被
  // `kMatchInProgress` 挡在门外 —— 那样这条用例测的就不是"满员行表"了。
  config.isAutoReady = false;
  // 羊群会吃掉实体表的名额（`kMaxEntities`）：不关掉的话第 4 个玩家的 `spawnEntity` 会失败，
  // `roomJoin` 把它报成 `kRoomFull`（这正是本用例第一次跑红的原因）。
  config.sheepTarget = 0;
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config, &error));

  // 4 个套接字**同时存活**：UDP 端口会被回收，逐个建/销会让第 4 个套接字拿到刚关掉的那个端口，
  // 于是服务端把它当成"同一次握手的重发"（§5.5 静默去重）—— 本用例第二次跑红就是踩的这个坑。
  std::array<ac::net::UdpSocket, 4> sockets{};
  for (int sessionIndex = 0; sessionIndex < 4; ++sessionIndex) {
    AC_CHECK(sockets[static_cast<std::size_t>(sessionIndex)].bind(0u));
    const std::uint16_t session = handshake(
        runtime, sockets[static_cast<std::size_t>(sessionIndex)],
        0x70000000u + static_cast<std::uint32_t>(sessionIndex), 30000u);
    AC_CHECK(session != 0u);
    // 只握手、**不发 kJoin**（正是 ac_bot 的行为），只把房间推起来。时间戳按会话递增，
    // 否则后一个会话会把房间的时钟往回拨（`elapsed < 0` 虽被夹住，但读起来是坏味道）。
    for (int i = 0; i < 8; ++i) {
      runtime.pollOnce(30000u + static_cast<std::uint64_t>(sessionIndex) * 500u +
                       static_cast<std::uint64_t>(i) * 30u + 30u);
    }

    std::uint8_t scratch[2048] = {};
    ac::net::Endpoint from{};
    int bytes = 0;
    ac::net::MatchState state{};
    bool seen = false;
    std::size_t lastSize = 0u;
    for (int i = 0; i < 4096; ++i) {
      bytes = sockets[static_cast<std::size_t>(sessionIndex)].recvFrom(
          from, std::span<std::uint8_t>(scratch, sizeof(scratch)));
      if (bytes <= 0) break;
      lastSize = static_cast<std::size_t>(bytes);
      const ac::net::DecodeResult<ac::net::MatchState> decoded =
          ac::net::decodeMatchState(scratch, lastSize);
      if (decoded.isOk) {
        state = decoded.value;
        seen = true;
      }
    }
    AC_CHECK(seen);
    // 一帧都没解出来就到此为止：空的行表上取 `players[0]` 是越界读（段错误），不是断言失败。
    if (!seen) return;
    AC_CHECK_EQ(state.players.size(), static_cast<std::size_t>(sessionIndex + 1));
    for (std::size_t i = 0; i < state.players.size(); ++i) {
      const std::size_t nameBytes = state.players[i].name.size();
      AC_CHECK(nameBytes >= 1u && nameBytes <= 12u);
    }
    std::printf("no-join row%d: lastFrame=%zuB rows=%zu name=%s\n", sessionIndex + 1, lastSize,
                state.players.size(), state.players[0].name.c_str());
  }
  runtime.stop();
}

// §5.3 的服务端半边：命令流的 ack 必须随 MatchState 下行（客户端 `UdpTransport.StreamOf` 把
// MatchState 归到命令流，那是它唯一会采纳的 ack 载体）。不回 ack 的后果正是联调实测的：retx 涨到
// 数千、RTT 恒 0（没有 ack 就没有往返样本）、会话被客户端自己判成 Reconnecting。
AC_TEST(runtime_match_state_carries_command_acks) {
  ac::server::RuntimeConfig config = testConfig();
  config.isAutoReady = false;
  config.sheepTarget = 0;
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config, &error));
  const ac::net::Endpoint server{kLoopback, runtime.udpPort()};
  ac::net::UdpSocket socket{};
  AC_CHECK(socket.bind(0u));
  std::uint64_t now = 40000u;
  std::uint32_t serverTick = 0u;
  const std::uint16_t session = handshake(runtime, socket, 0x60000000u, now, &serverTick);
  AC_CHECK(session != 0u);

  // 命令流上的两条可靠包：kJoin（msgId 1）+ Command（msgId 2）。seq 与 msgId 都从 1 起。
  std::uint8_t frame[128] = {};
  std::size_t bytes = encodeJoinFrame(frame, sizeof(frame), session, 1u, "acker");
  AC_CHECK(bytes > 0u);
  AC_CHECK(socket.sendTo(server, std::span<const std::uint8_t>(frame, bytes)) > 0);
  bytes = encodeCommandFrame(frame, sizeof(frame), session, 2u, serverTick, 0u, 0);
  AC_CHECK(bytes > 0u);
  AC_CHECK(socket.sendTo(server, std::span<const std::uint8_t>(frame, bytes)) > 0);

  // 窗口取 1.5s：MatchState 变化即播 + 每 1000ms 一次周期广播，两条都覆盖得到。
  std::uint32_t ackBase = 0u;
  std::uint32_t ackBits = 0u;
  bool sawAck = false;
  // §5.2 的每通道序号：两条流各自必须**连续**（相邻差恒为 1）。共用一个计数器时，客户端按类型
  // 统计的期望包数会把插入的 MatchState 算成丢包（联调里 PacketLossPermille 乱跳的来源）。
  bool sawSnapshotSeq = false;
  bool sawMatchStateSeq = false;
  std::uint16_t lastSnapshotSeq = 0u;
  std::uint16_t lastMatchStateSeq = 0u;
  bool snapshotSeqGap = false;
  bool matchStateSeqGap = false;
  // §5.6 的心跳：`reliable|ackOnly`、载荷 0、每 500ms 一帧；它同时是**最快的 ack 载体** —— 客户端
  // ping 口径（C03 §5.2）与 RTO 表（200ms 起）都指望确认在几十毫秒内回来，只靠 1Hz 的 MatchState
  // 会让 rttMs 读到 344~469ms、每条命令多传 2~4 次。
  bool sawKeepAlive = false;
  bool keepAliveShapeOk = true;
  int firstKeepAlivePoll = -1;
  int keepAliveFrames = 0;
  ac::net::ReliableState keepAlivePeer{};
  std::uint8_t scratch[2048] = {};
  for (int i = 0; i < 30; ++i) {
    runtime.pollOnce(now + static_cast<std::uint64_t>(i) * 50u + 50u);
    ac::net::Endpoint from{};
    for (int k = 0; k < 64; ++k) {
      const int got = socket.recvFrom(from, std::span<std::uint8_t>(scratch, sizeof(scratch)));
      if (got <= 0) break;
      const ac::net::DecodeResult<ac::net::PacketInfo> info =
          ac::net::decodePacket(scratch, static_cast<std::size_t>(got));
      if (!info.isOk) continue;
      const ac::net::PacketType type = static_cast<ac::net::PacketType>(info.value.header.type);
      if (type == ac::net::PacketType::kSnapshot) {
        if (sawSnapshotSeq && static_cast<std::uint16_t>(lastSnapshotSeq + 1u) != info.value.header.seq) {
          snapshotSeqGap = true;
        }
        lastSnapshotSeq = info.value.header.seq;
        sawSnapshotSeq = true;
      } else if (type == ac::net::PacketType::kMatchState) {
        if (sawMatchStateSeq &&
            static_cast<std::uint16_t>(lastMatchStateSeq + 1u) != info.value.header.seq) {
          matchStateSeqGap = true;
        }
        lastMatchStateSeq = info.value.header.seq;
        sawMatchStateSeq = true;
      } else if (type == ac::net::PacketType::kKeepAlive) {
        keepAliveFrames += 1;
        if (!sawKeepAlive) {
          sawKeepAlive = true;
          firstKeepAlivePoll = i;
        }
        // 形状（§5.1/§5.6）：flags 恰好 reliable|ackOnly、载荷 0 字节、必须带可靠扩展头（ack 就装在里面）。
        if (!info.value.header.isAckOnly() || !info.value.header.isReliable() ||
            info.value.payloadBytes != 0u || !info.value.hasReliableExt) {
          keepAliveShapeOk = false;
        }
        keepAlivePeer.ackBase = info.value.reliableExt.ackBase;
        keepAlivePeer.ackBits = info.value.reliableExt.ackBits;
      } else {
        continue;
      }
      if (!info.value.hasReliableExt || type != ac::net::PacketType::kMatchState) continue;
      ackBase = info.value.reliableExt.ackBase;
      ackBits = info.value.reliableExt.ackBits;
      sawAck = true;
    }
  }
  AC_CHECK(sawAck);
  AC_CHECK(sawSnapshotSeq);
  AC_CHECK(sawMatchStateSeq);
  AC_CHECK(!snapshotSeqGap);
  AC_CHECK(!matchStateSeqGap);
  AC_CHECK(sawKeepAlive);
  AC_CHECK(keepAliveShapeOk);
  // 回执必须**及时**：命令在循环之前就发出去了，第一帧心跳应该在第一轮 poll 里就把 ack 带回来
  //（500ms 的周期心跳最坏要等半秒，正是 rttMs 被撑到几百毫秒的原因）。
  AC_CHECK(firstKeepAlivePoll >= 0 && firstKeepAlivePoll <= 2);
  AC_CHECK(ac::net::isAckedBy(keepAlivePeer, 2u));
  AC_CHECK(keepAliveFrames >= 2);  // 1.5s 窗口内至少两帧 = 500ms 周期真的在跑
  ac::net::ReliableState peer{};
  peer.ackBase = ackBase;
  peer.ackBits = ackBits;
  AC_CHECK(ac::net::isAckedBy(peer, 1u));  // kJoin
  AC_CHECK(ac::net::isAckedBy(peer, 2u));  // Command
  std::printf("acks: matchState ackBase=%u ackBits=0x%08X（覆盖 kJoin msgId 1 与 Command msgId 2）；"
              "seq: snapshot 连续=%d matchState 连续=%d；keepAlive frames=%d 首帧 poll=%d 形状=%d "
              "ackBase=%u ackBits=0x%08X\n",
              ackBase, ackBits, snapshotSeqGap ? 0 : 1, matchStateSeqGap ? 0 : 1, keepAliveFrames,
              firstKeepAlivePoll, keepAliveShapeOk ? 1 : 0, keepAlivePeer.ackBase,
              keepAlivePeer.ackBits);
  runtime.stop();
}

// ADR-013：产品默认（`isAutoReady == false`）下，"进大厅 → 看到 ready=false → 按准备 → 全房就绪后开局"
// 必须真的成立。改之前服务端只按无条件 true 调 roomSetReady（客户端命令里的 Ready 位根本没人读），
// 于是"大厅"在真连时不存在：首个会话被自动就绪、立刻单开一局，其余连接全部撞 kMatchInProgress。
AC_TEST(runtime_lobby_ready_bit_starts_match) {
  ac::server::RuntimeConfig config = testConfig();
  config.isAutoReady = false;  // 产品默认：服务端不替玩家按准备
  config.sheepTarget = 0;
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config, &error));

  ac::net::UdpSocket host{};
  ac::net::UdpSocket mate{};
  AC_CHECK(host.bind(0u));
  AC_CHECK(mate.bind(0u));

  std::uint64_t now = 5000u;
  std::uint32_t hostTick = 0u;
  std::uint32_t mateTick = 0u;
  const std::uint16_t hostSession = handshake(runtime, host, 0x0A0B0C0Du, now, &hostTick);
  const std::uint16_t mateSession = handshake(runtime, mate, 0x1A1B1C1Du, now, &mateTick);
  AC_CHECK(hostSession != 0u);
  AC_CHECK(mateSession != 0u);
  std::printf("lobby: helloAck serverTick host=%u mate=%u（命令的 clientTick 必须等于它）\n", hostTick,
              mateTick);

  const auto send = [&](ac::net::UdpSocket& socket, const std::uint8_t* frame, std::size_t bytes) {
    return bytes > 0u && socket.sendTo(ac::net::Endpoint{kLoopback, runtime.udpPort()},
                                       std::span<const std::uint8_t>(frame, bytes)) > 0;
  };
  const auto sendJoin = [&](ac::net::UdpSocket& socket, std::uint16_t session, const char* name) {
    std::uint8_t frame[128] = {};
    return send(socket, frame, encodeJoinFrame(frame, sizeof(frame), session, 1u, name));
  };
  // 每个会话各占一条 seq 流：同 seq 会被 kDuplicateSequence 丢掉。
  const auto sendReady = [&](ac::net::UdpSocket& socket, std::uint16_t session, std::uint16_t seq,
                             std::uint32_t serverTick) {
    std::uint8_t frame[128] = {};
    return send(socket, frame,
                encodeCommandFrame(frame, sizeof(frame), session, seq, serverTick,
                                   ac::config::kButtonReady, /*moveX=*/0));
  };
  // 把该套接字读空，留下最后一次 MatchState（服务端只在变化时广播，所以要读全）。
  const auto latestState = [](ac::net::UdpSocket& socket, ac::net::MatchState& out) {
    bool seen = false;
    std::uint8_t scratch[2048] = {};
    ac::net::Endpoint from{};
    for (int i = 0; i < 4096; ++i) {
      const int got = socket.recvFrom(from, std::span<std::uint8_t>(scratch, sizeof(scratch)));
      if (got <= 0) break;
      const ac::net::DecodeResult<ac::net::MatchState> state =
          ac::net::decodeMatchState(scratch, static_cast<std::size_t>(got));
      if (state.isOk) {
        out = state.value;
        seen = true;
      }
    }
    return seen;
  };
  // 大厅观测：相位 + 每行昵称/ready + 丢弃帧数（红了能一眼看出卡在哪）。
  const auto dumpState = [&](const char* tag, const ac::net::MatchState& seen) {
    std::printf("%s phase=%u rows=%zu dropped=%llu", tag, static_cast<unsigned>(seen.phase),
                seen.players.size(),
                static_cast<unsigned long long>(
                    counterOf(runtime, ac::metrics::CounterId::kDroppedFrames)));
    for (std::size_t i = 0; i < seen.players.size(); ++i) {
      std::printf(" [%s pid=%u ready=%u]", seen.players[i].name.c_str(),
                  static_cast<unsigned>(seen.players[i].pid), static_cast<unsigned>(seen.players[i].ready));
    }
    std::printf("\n");
  };

  AC_CHECK(sendJoin(host, hostSession, "host"));
  AC_CHECK(sendJoin(mate, mateSession, "mate"));
  for (int i = 0; i < 8; ++i) runtime.pollOnce(now += 30u);
  AC_CHECK_EQ(runtime.metrics().players, 2u);

  // ① 大厅：谁都没按准备 ⇒ 相位停在 kLobby（旧默认会在这一拍就替他们 ready 并单开一局）。
  ac::net::MatchState state{};
  AC_CHECK(latestState(host, state));
  dumpState("lobby-after-join", state);
  AC_CHECK_EQ(state.players.size(), static_cast<std::size_t>(2u));
  AC_CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(ac::room::MatchPhase::kLobby));
  bool sawHost = false;
  bool sawMate = false;
  for (std::size_t i = 0; i < state.players.size(); ++i) {
    AC_CHECK_EQ(static_cast<int>(state.players[i].ready), 0);
  }
  // 静置 6 秒（> 200 拍）：自动准备一旦回归，这里就会变成 loading/playing。
  // 昵称在 kJoin（type 11）那一帧才改，MatchState 是变化驱动 + 1s 节拍广播 —— 到这一刻两行都该是昵称。
  for (int i = 0; i < 200; ++i) runtime.pollOnce(now += 30u);
  AC_CHECK(latestState(host, state));
  dumpState("lobby-idle-6s", state);
  AC_CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(ac::room::MatchPhase::kLobby));
  for (std::size_t i = 0; i < state.players.size(); ++i) {
    sawHost = sawHost || state.players[i].name == "host";
    sawMate = sawMate || state.players[i].name == "mate";
  }
  AC_CHECK(sawHost);
  AC_CHECK(sawMate);

  // ② 只有房主按准备：不算全员就绪，仍然不开局，但 ready 位必须广播出去。
  AC_CHECK(sendReady(host, hostSession, 2u, hostTick));
  for (int i = 0; i < 20; ++i) runtime.pollOnce(now += 30u);
  AC_CHECK(latestState(host, state));
  dumpState("host-ready-only", state);
  AC_CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(ac::room::MatchPhase::kLobby));
  // ready 位必须**在首次发送时就生效**：§5.5 的窗口去重方向反了的话（把新 seq 当重复丢），
  // 这里会看到 ready=0 且 dropped 涨 1 —— 联调里就是"按了准备没反应"。
  AC_CHECK_EQ(static_cast<int>(counterOf(runtime, ac::metrics::CounterId::kDroppedFrames)), 0);
  {
    int hostReady = -1;
    int mateReady = -1;
    for (std::size_t i = 0; i < state.players.size(); ++i) {
      if (state.players[i].name == "host") hostReady = static_cast<int>(state.players[i].ready);
      if (state.players[i].name == "mate") mateReady = static_cast<int>(state.players[i].ready);
    }
    AC_CHECK_EQ(hostReady, 1);
    AC_CHECK_EQ(mateReady, 0);
  }

  // ③ 队友也按准备：全房就绪 ⇒ 开局（相位离开大厅）。
  AC_CHECK(sendReady(mate, mateSession, 2u, mateTick));
  bool leftLobby = false;
  for (int i = 0; i < 200 && !leftLobby; ++i) {
    runtime.pollOnce(now += 30u);
    if (latestState(host, state)) {
      leftLobby = static_cast<int>(state.phase) != static_cast<int>(ac::room::MatchPhase::kLobby);
    }
  }
  dumpState("after-both-ready", state);
  AC_CHECK(leftLobby);
  std::printf("lobby: ready=false → hostReady → allReady → phase=%u（kLobby=%d kLoading=%d kPlaying=%d）\n",
              static_cast<unsigned>(state.phase), static_cast<int>(ac::room::MatchPhase::kLobby),
              static_cast<int>(ac::room::MatchPhase::kLoading),
              static_cast<int>(ac::room::MatchPhase::kPlaying));
  runtime.stop();
}
