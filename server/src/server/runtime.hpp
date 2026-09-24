#pragma once
// S14 §2-3/§5：无头服务器运行时 —— UDP 游戏面（握手 / 命令 / 快照 / MatchState）与 HTTP 面
// （/health、/metrics、两个读端点）。房间、对局与复制三层在这里接线（S12/S13 留下的移交项，
// 见 server/README.md 的 §15.2 待裁决记录）。
//
// 定位：**测量用运行时**。它足够真（真 socket、真房间循环、真差分编码、真背压账），
// 但大厅流程（建房/加入/准备）由运行时自动驱动，命令通道不做重传（见 §15.1 的偏差登记）。
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "core/scheduler.hpp"
#include "http/listener.hpp"
#include "http/server.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "metrics/metrics.hpp"
#include "net/codec.hpp"
#include "net/handshake.hpp"
#include "net/udp_socket.hpp"
#include "persist/match_store.hpp"
#include "replication/backpressure.hpp"
#include "replication/baseline.hpp"
#include "replication/snapshot_rate.hpp"
#include "room/room.hpp"
#include "room/rooms.hpp"
#include "security/validate.hpp"

namespace ac::server {

inline constexpr std::size_t kMaxClients = 8u;          // 门禁场景 4 人，留一倍余量
inline constexpr std::size_t kMaxPacketsPerPoll = 64u;  // 单次轮询的收包上限（避免饿死 tick）
inline constexpr std::size_t kSnapshotRingSize = 4096u;  // G3 的最近 4096 个快照帧样本

struct RuntimeConfig {
  std::uint16_t udpPort = 8788u;
  std::uint16_t httpPort = 8787u;
  std::uint32_t seed = 20260101u;
  std::int32_t sheepTarget = 60;  // 合成负载：常驻羊数（0 = 不补）
  bool isHttpEnabled = true;
  // S15 §6-2 的 `--data-dir`：留空则回落到 `AC_DATA_DIR` 的环境默认值（见 persist::dataDirFromEnv）。
  std::string dataDir{};
  bool isAutoReady = true;  // 机器人不走大厅：连上即 ready 并开局
};

// G2 的按客户端账（gate 每秒取一次增量，避免把整跑次平均掉）。
struct ClientStat {
  std::uint16_t transportId = 0u;
  std::size_t bytesOut = 0u;
  std::size_t snapshotCount = 0u;
  std::size_t queuedBytes = 0u;
};

struct RuntimeMetrics {
  std::uint64_t ticks = 0u;
  std::size_t clients = 0u;
  std::size_t rooms = 0u;
  std::size_t players = 0u;
  std::size_t graceActive = 0u;
  std::size_t aliveSheep = 0u;
  std::size_t snapshotSamples = 0u;
  std::size_t snapshotBytesMax = 0u;
  double snapshotBytesP95 = 0.0;
  double scheduleErrorP95Ms = 0.0;
  double scheduleHeadTailGapMs = 0.0;  // §5 G6 的替代判据输入（前 1/3 与后 1/3 的 P95 差）
  double scheduleHeadThirdP95Ms = 0.0;
  double scheduleTailThirdP95Ms = 0.0;
  double tickIntervalErrorP95Ms = 0.0;
  double tickWorkP95Ms = 0.0;
  double simDriftMs = 0.0;
  std::size_t eventsDropped = 0u;
  std::size_t udpBytesIn = 0u;
  std::size_t udpBytesOut = 0u;
  std::size_t framesIn = 0u;
  std::size_t framesOut = 0u;
  std::size_t hardCorrect = 0u;
  std::size_t tickSkips = 0u;
};

class Runtime {
 public:
  Runtime();
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // 打开存储、绑定 UDP/HTTP 端口并建房；失败原因写进 *error（真实端口写回 config 外的成员）。
  bool start(const RuntimeConfig& config, std::string* error);
  // 收包 → 会话/宽限 → 房间推进 → HTTP 轮询 → 指标发布。nowMs 是单调墙上毫秒。
  void pollOnce(std::uint64_t nowMs);
  void stop();

  bool isRunning() const noexcept { return isRunning_; }
  std::uint16_t udpPort() const noexcept { return udpPort_; }
  std::uint16_t httpPort() const noexcept { return httpPort_; }
  const RuntimeConfig& config() const noexcept { return config_; }
  RuntimeMetrics metrics() const;
  std::size_t clientStats(ClientStat* out, std::size_t capacity) const noexcept;
  const ac::metrics::CounterRegistry& counters() const noexcept { return counters_; }
  const ac::metrics::GaugeRegistry& gauges() const noexcept { return gauges_; }
  std::size_t roomCount() const noexcept;
  std::size_t clientCount() const noexcept;
  const std::string& dataDir() const noexcept { return dataDir_; }

 private:
  struct Client {
    bool isUsed = false;
    ac::net::Endpoint endpoint{};
    std::uint16_t transportId = 0u;
    ac::room::Session session{};
    std::uint16_t seq = 0u;
    ac::replication::ClientBaseline baseline{};
    ac::replication::OutboundBudget budget{};
    ac::replication::SnapshotRateState rate{};
    // 降档信号的上一拍读数（§5 的「tickSkips / 房间预算超限增长」，评审 #4）。
    std::uint64_t lastTickSkips = 0u;
    std::uint64_t lastBudgetExceeded = 0u;
    std::size_t bytesOut = 0u;  // G2 的分子：本客户端实际写出的 UDP 载荷字节
    bool isFullSnapshotDue = false;  // Resume 成功补一次全量（§5.5）
    ac::security::CommandDedup dedup{};
  };

  void receivePackets(std::uint64_t nowMs);
  void handlePacket(const ac::net::Endpoint& from, const std::uint8_t* bytes, std::size_t size,
                    std::uint64_t nowMs);
  // 注意：decode* 系列吃的是**整帧**（内部自己重解包头），不是载荷切片。
  void handleHello(const ac::net::Endpoint& from, const std::uint8_t* frame, std::size_t size,
                   std::uint64_t nowMs);
  void handleResume(const ac::net::Endpoint& from, std::uint16_t session,
                    const std::uint8_t* frame, std::size_t size, std::uint64_t nowMs);
  void handleCommand(std::uint16_t session, const std::uint8_t* frame, std::size_t size,
                     std::size_t payloadBytes, std::uint64_t nowMs);
  void purgeReleasedClients();
  void ensureMatchRunning(std::uint64_t nowMs);
  void topUpSheep() noexcept;
  void onReplicate(ac::room::Room& room) noexcept;
  void onSendMatchState(ac::room::Session& session, const ac::net::MatchState& state) noexcept;
  void publishMetrics(std::uint64_t nowMs);
  void recordSnapshotSize(std::size_t bytes) noexcept;
  double snapshotP95() const noexcept;
  double simDriftMs() const noexcept;
  void updateRates(std::uint64_t nowMs) noexcept;
  void httpFill(ac::http::HttpDeps& out);

  static void replicateThunk(void* user, ac::room::Room& room);
  static void matchStateThunk(void* user, const ac::room::Room& room, ac::room::Session& session,
                              const ac::net::MatchState& state);
  static void httpFillThunk(void* user, ac::http::HttpDeps& out);

  Client* findClientByTransport(std::uint16_t transportId) noexcept;
  Client* claimClient(std::uint16_t transportId, const ac::net::Endpoint& endpoint,
                      std::uint64_t nowMs) noexcept;
  void releaseClient(Client& client) noexcept;

  RuntimeConfig config_{};
  bool isRunning_ = false;
  std::string dataDir_{};
  std::uint16_t udpPort_ = 0u;
  std::uint16_t httpPort_ = 0u;
  std::uint64_t startMs_ = 0u;
  ac::net::UdpSocket udp_{};
  ac::net::HandshakeServer handshake_{};
  ac::room::RoomRegistry registry_{};
  ac::room::Room* room_ = nullptr;
  ac::room::RoomDeps deps_{};
  Client clients_[kMaxClients] = {};
  ac::core::TickScheduler scheduler_{};
  ac::metrics::CounterRegistry counters_{};
  ac::metrics::GaugeRegistry gauges_{};
  ac::metrics::ProcessSnapshot process_{};
  std::unique_ptr<ac::persist::MatchStore> store_{};
  ac::http::HttpState httpState_{};
  ac::http::HttpListener http_{};
  std::string metricsBody_{};
  std::uint32_t snapshotRing_[kSnapshotRingSize] = {};
  std::size_t snapshotCount_ = 0u;
  std::size_t snapshotNext_ = 0u;
  std::size_t snapshotMax_ = 0u;
  std::size_t eventsDroppedSeen_ = 0u;
  // 房间内建 tick 跳过的上一拍读数（§5 G8 的 tick 跳过项，评审：以前没人喂这个计数）。
  std::uint32_t skippedSeen_ = 0u;
  // tick 记账基准：开球那一刻对齐到房间的 tick 计数，开球前的 tick 一律不算进调度器。
  std::uint32_t countedTicks_ = 0u;
  // 漂移的墙钟基准：必须与 countedTicks_ 同一时刻取（开球），否则「等玩家/等 loading」的那段
  // 会整段算成模拟落后（实测 200ms RTT 场景是 135ms，低延迟场景是 22ms —— 正好等于 loading 时长）。
  std::uint64_t tickBaseMs_ = 0u;
  std::uint64_t lastPollMs_ = 0u;  // 最近一次 pollOnce 的墙上毫秒（漂移计算用）
  std::uint8_t sendBuffer_[ac::net::kMaxSnapshotBytes] = {};
  std::uint8_t recvBuffer_[ac::net::kMaxPacketBytes] = {};
};

}  // namespace ac::server
