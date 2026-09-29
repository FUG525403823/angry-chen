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
#include <vector>

#include "core/scheduler.hpp"
#include "http/listener.hpp"
#include "http/server.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "metrics/metrics.hpp"
#include "net/codec.hpp"
#include "net/fragment.hpp"
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
  // ADR-013：产品默认走大厅（S10 §5.4）——会话只有收到带 Ready 位的命令才就绪，房主在全房就绪时开局。
  // 装载/门禁/压测场景（ac_bot 不发 Ready 位）用 `--serve --auto-ready` 显式开启（gate.cpp 亦显式设置）。
  bool isAutoReady = false;
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
  // 战绩存储的只读入口（/api/* 读端点用它；用例用它断言「结束即入库」）。
  const ac::persist::MatchStore* store() const noexcept { return store_.get(); }

 private:
  struct Client {
    bool isUsed = false;
    ac::net::Endpoint endpoint{};
    std::uint16_t transportId = 0u;
    ac::room::Session session{};
    // §5.2 的**每通道各一条发送序号**。快照通道与命令/控制通道必须分开：共用一个计数器时，
    // 客户端按类型统计的期望包数（NetStats.OnInbound 的相邻序号差）会把「中间插了一条 MatchState」
    // 算成丢包 —— 联调里 PacketLossPermille 因此在 0‰/50‰/500‰ 之间乱跳（500 是窗口只有两三帧时
    // 的假读数，50 是稳态下 MatchState 的插入率）。分开之后两条流各自连续，读数才是网络真值。
    std::uint16_t seq = 0u;          // 命令/控制通道的 **msgId 流**（MatchState 与 KeepAlive 共用）
    std::uint16_t snapshotSeq = 0u;  // 快照通道（type 5，不可靠）
    std::uint16_t matchStateSeq = 0u;  // MatchState 的包头 seq（客户端按类型统计，必须自己一条）
    std::uint16_t keepAliveSeq = 0u;   // 心跳（type 7，ackOnly）的包头 seq：同上，另行一条
    ac::replication::ClientBaseline baseline{};
    ac::replication::OutboundBudget budget{};
    ac::replication::SnapshotRateState rate{};
    // 降档信号的上一拍读数（§5 的「tickSkips / 房间预算超限增长」，评审 #4）。
    std::uint64_t lastTickSkips = 0u;
    std::uint64_t lastBudgetExceeded = 0u;
    std::size_t bytesOut = 0u;  // G2 的分子：本客户端实际写出的 UDP 载荷字节
    bool isFullSnapshotDue = false;  // Resume 成功补一次全量（§5.5）
    ac::security::CommandDedup dedup{};
    // §5.3：客户端命令流的**接收**窗口（ackBase/ackBits 的来源）。回执位图随 MatchState 下行。
    ac::net::ReliableState commandRecv{};
    // §5.6：500ms 心跳计时（服务端→客户端方向）。`ackDue` = 本 poll 收到过可靠包、需要立刻回执 ——
    // 服务端的 ack 只能靠 KeepAlive / MatchState 带出去，而 MatchState 只有 1Hz，光靠它客户端要等
    // 0.3~0.5s 才拿到确认（联调实测 rttMs 344~469ms、每条命令重传 2~4 次）。
    ac::net::KeepAliveTimer heartbeat{};
    bool ackDue = false;
  };

  void receivePackets(std::uint64_t nowMs);
  // isReassembled = 本帧来自分片重组（重走分派时置位）：分片里再套分片一律判坏包，
  // 避免「重组 → 分派 → 再重组」的自递归。
  void handlePacket(const ac::net::Endpoint& from, const std::uint8_t* bytes, std::size_t size,
                    std::uint64_t nowMs, bool isReassembled = false);
  // 注意：decode* 系列吃的是**整帧**（内部自己重解包头），不是载荷切片。
  void handleHello(const ac::net::Endpoint& from, const std::uint8_t* frame, std::size_t size,
                   std::uint64_t nowMs);
  // ADR-012：房间拒绝准入（满员 / 对局进行中）时的唯一回执。准入结果不得静默丢弃。
  void sendRoomUnavailable(const ac::net::Endpoint& to, std::uint16_t session);
  void handleResume(const ac::net::Endpoint& from, std::uint16_t session,
                    const std::uint8_t* frame, std::size_t size, std::uint64_t nowMs);
  // §5.1 方向表把 Fragment（type 9）列为合法的客户端消息：按 S04 §5.4 的既有 Reassembler 路径
  // 重组，收齐后把「重组出的完整逻辑消息」重新走一遍分派（不新建机制、不新建类型码）。
  void handleFragment(const ac::net::Endpoint& from, const ac::net::PacketInfo& info,
                      const std::uint8_t* payload, std::size_t payloadBytes);
  void handleCommand(std::uint16_t session, const std::uint8_t* frame, std::size_t size,
                     std::size_t payloadBytes, std::uint64_t nowMs);
  // type 11 Join：昵称上报（ADR-009「握手时序」）。无回复；pid 由 MatchState 的下行认领。
  void handleJoin(std::uint16_t session, const std::uint8_t* frame, std::size_t size);
  void purgeReleasedClients();
  void ensureMatchRunning(std::uint64_t nowMs);
  void topUpSheep() noexcept;
  void onReplicate(ac::room::Room& room) noexcept;
  void onSendMatchState(ac::room::Session& session, const ac::net::MatchState& state) noexcept;
  // §5.6：KeepAlive（type 7，`reliable|ackOnly`，载荷 0）—— 到点发心跳，顺带把命令流的 ack 带回去。
  void sendKeepAlive(Client& client) noexcept;
  void flushHeartbeats(std::uint64_t nowMs) noexcept;
  void publishMetrics(std::uint64_t nowMs);
  void recordSnapshotSize(std::size_t bytes) noexcept;
  double snapshotP95() const noexcept;
  double simDriftMs() const noexcept;
  void updateRates(std::uint64_t nowMs) noexcept;
  void httpFill(ac::http::HttpDeps& out);
  // 对局结束的落盘出口：结算记录 append 进 MatchStore + 写 reports/<matchId>.json（S13 §5）。
  void flushMatchOutcome();

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
  // 最近一次已落盘的战绩 matchId：同一局只入库一次（结束 → 重置之间会被反复观察）。
  std::string lastStoredMatchId_{};
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
  // tick 记账基准：房间的 tick 计数每局从 0 起（tryStartMatch 清 MatchCounters），这里只按增量记账。
  std::uint32_t countedTicks_ = 0u;
  // 本局 |simDrift| 的最大值（毫秒）：pollOnce 在 loading→playing 那一拍复位，此后只增不减；
  // 报告 ticks.simDriftMsMax 取它，而不是落盘瞬间的瞬时值。
  double simDriftAbsMaxMs_ = 0.0;
  std::uint64_t lastPollMs_ = 0u;  // 最近一次 pollOnce 的墙上毫秒（漂移计算的「现在」）
  std::uint8_t sendBuffer_[ac::net::kMaxSnapshotBytes] = {};
  std::uint8_t recvBuffer_[ac::net::kMaxPacketBytes] = {};
  // Fragment（type 9）的接收侧重组：键 = (session, kFragment, fragId)，60 tick 超时回收。
  ac::net::Reassembler reassembler_{};
  std::vector<std::uint8_t> reassemblyBuffer_{};
};

}  // namespace ac::server
