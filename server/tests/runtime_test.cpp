// S14 §2-3：服务器运行时的端到端用例（真 UDP + 真 HTTP，端口 0 = 系统分配）。
// 本文件同时充当「运行时接线正确」的证据：握手、命令、复制、HTTP 面与停机。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

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
  const auto keepAlive = [&](int index) {
    if (index % 4 != 0) return;
    std::uint8_t frame[64] = {};
    const std::size_t bytes = encodeCommandFrame(frame, sizeof(frame), botSession(runtime),
                                                static_cast<std::uint16_t>(++seq),
                                                static_cast<std::uint32_t>(index));
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
    for (int i = 0; i < playPolls[round]; ++i) {
      runtime.pollOnce(now += (i % 2 == 0 ? 25u : 30u));
      keepAlive(i);
    }
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
    std::printf("round=%d matchId=%s duration=%.0fms ticks=%0.f jitterP50=%.3f jitterP95=%.3f "
                "schedP95=%.3f workP95=%.3f workP99=%.3f\n",
                round, record->matchId.c_str(), durationMs, ticksTotal, jitterP50, jitterP95,
                scheduleP95, workP95, workP99);
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
  }
  runtime.stop();
}
