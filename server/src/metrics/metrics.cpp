#include "metrics/metrics.hpp"

#include <cstdio>

namespace ac::metrics {
namespace {

enum class Origin : std::uint8_t { kCounter = 0u, kGauge, kProcess };

// ProcessSnapshot 的字段序号（Origin::kProcess 的 source）。
enum class ProcessField : std::uint16_t {
  kVersion = 0u,
  kRooms,
  kConnections,
  kPlayers,
  kGraceActive,
  kRecordsRetained,
  kUptimeSeconds,
};

struct MetricDef {
  const char* name;
  Origin origin;
  std::uint16_t source;
  const char* help;
};

// S13 §5 的名字清单，按表中六类的顺序排列。
constexpr MetricDef kMetricDefs[] = {
    {"ac_server_version", Origin::kProcess, static_cast<std::uint16_t>(ProcessField::kVersion),
     "服务器版本/协议/tick（标签与 --version 行同源）"},
    // 调度
    {"ac_tick_schedule_error_ms_p95", Origin::kGauge,
     static_cast<std::uint16_t>(GaugeId::kTickScheduleErrorMsP95), "tick 绝对时刻误差 P95（毫秒，取幅值）"},
    {"ac_tick_interval_error_ms_p95", Origin::kGauge,
     static_cast<std::uint16_t>(GaugeId::kTickIntervalErrorMsP95), "相邻 tick 间隔误差 P95（毫秒，取幅值）"},
    {"ac_tick_jitter_ms_p50", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kTickJitterMsP50),
     "tick 抖动 P50（毫秒）"},
    {"ac_tick_jitter_ms_p95", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kTickJitterMsP95),
     "tick 抖动 P95（毫秒）"},
    {"ac_tick_work_ms_p95", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kTickWorkMsP95),
     "单 tick 工作量 P95（毫秒）"},
    {"ac_tick_work_ms_p99", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kTickWorkMsP99),
     "单 tick 工作量 P99（毫秒）"},
    {"ac_sim_drift_ms", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kSimDriftMs),
     "模拟时间与墙上时间的漂移（毫秒）"},
    {"ac_room_budget_exceeded_total", Origin::kCounter,
     static_cast<std::uint16_t>(CounterId::kRoomBudgetExceeded), "8 ms 工作量预算被突破的次数"},
    // 复制与背压
    {"ac_snapshots_sent_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kSnapshotsSent),
     "入队的快照帧数"},
    {"ac_snapshot_bytes_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kSnapshotBytesTotal),
     "入队的快照字节数"},
    {"ac_snapshot_bytes_avg", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kSnapshotBytesAvg),
     "快照帧字节均值"},
    {"ac_snapshot_bytes_max", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kSnapshotBytesMax),
     "快照帧字节峰值"},
    {"ac_snapshot_records_avg", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kSnapshotRecordsAvg),
     "快照帧实体记录数均值"},
    {"ac_snapshot_rate_x10", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kSnapshotRateX10),
     "当前快照发送频率 ×10（200/150/100）"},
    {"ac_snapshot_rate_downshifts_total", Origin::kCounter,
     static_cast<std::uint16_t>(CounterId::kSnapshotRateDownshifts), "降档次数"},
    {"ac_send_queue_bytes", Origin::kGauge, static_cast<std::uint16_t>(GaugeId::kSendQueueBytes),
     "出站队列已排队字节"},
    {"ac_slow_client_drops_total", Origin::kCounter,
     static_cast<std::uint16_t>(CounterId::kSlowClientDrops), "慢客户端丢弃的快照帧数"},
    // S12 §13.1-2 的移交项：S12 §5/§8 与 S14 的 G8 都要这一条，而 S13 §5 的名单漏了它。
    {"ac_tick_skips_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kTickSkips),
     "因工作量预算让出而被跳过的 tick 数"},
    // 校验
    {"ac_malformed_frames_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kMalformedFrames),
     "畸形入站包数"},
    {"ac_oversized_frames_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kOversizedFrames),
     "超长入站包数"},
    {"ac_rate_limited_frames_total", Origin::kCounter,
     static_cast<std::uint16_t>(CounterId::kRateLimitedFrames), "被限流的入站包数"},
    {"ac_dropped_frames_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kDroppedFrames),
     "因越限被丢弃的入站包数"},
    {"ac_pose_suspect_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kPoseSuspect),
     "姿态可疑判定次数"},
    {"ac_pose_rejected_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kPoseRejected),
     "姿态被拒绝次数"},
    {"ac_hard_correct_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kHardCorrect),
     "硬纠正次数（唯一写入点见 S11 §6）"},
    {"ac_rewind_clamped_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kRewindClamped),
     "回退上限被夹取的次数"},
    {"ac_speed_violations_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kSpeedViolations),
     "速度越限次数"},
    // 会话与房间
    {"ac_rooms", Origin::kProcess, static_cast<std::uint16_t>(ProcessField::kRooms), "房间数"},
    {"ac_connections", Origin::kProcess, static_cast<std::uint16_t>(ProcessField::kConnections),
     "连接数（含宽限期）"},
    {"ac_players", Origin::kProcess, static_cast<std::uint16_t>(ProcessField::kPlayers), "在线玩家数"},
    {"ac_grace_active", Origin::kProcess, static_cast<std::uint16_t>(ProcessField::kGraceActive),
     "宽限期内待重连的会话数"},
    {"ac_grace_starts_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kGraceStarts),
     "进入宽限期的次数"},
    {"ac_grace_reconnects_total", Origin::kCounter,
     static_cast<std::uint16_t>(CounterId::kGraceReconnects), "宽限期内重连成功次数"},
    {"ac_grace_timeouts_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kGraceTimeouts),
     "宽限期超时次数"},
    // 流量与存储
    {"ac_bytes_out_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kBytesOut),
     "出站字节数"},
    {"ac_bytes_in_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kBytesIn), "入站字节数"},
    {"ac_frames_out_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kFramesOut),
     "出站帧数"},
    {"ac_frames_in_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kFramesIn), "入站帧数"},
    {"ac_events_sent_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kEventsSent),
     "入队的可靠事件数"},
    {"ac_events_dropped_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kEventsDropped),
     "被丢弃的可靠事件数（永不发生，见 S12 §5）"},
    {"ac_records_retained", Origin::kProcess, static_cast<std::uint16_t>(ProcessField::kRecordsRetained),
     "战绩常驻记录数"},
    {"ac_corrupt_lines_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kCorruptLines),
     "战绩文件坏行数"},
    {"ac_http_rate_limited_total", Origin::kCounter,
     static_cast<std::uint16_t>(CounterId::kHttpRateLimited), "被 429 拒绝的 HTTP 读请求数"},
    {"ac_http_cache_hits_total", Origin::kCounter, static_cast<std::uint16_t>(CounterId::kHttpCacheHits),
     "HTTP 读缓存命中数"},
    {"ac_uptime_seconds", Origin::kProcess, static_cast<std::uint16_t>(ProcessField::kUptimeSeconds),
     "进程运行秒数"},
};
constexpr std::size_t kMetricDefCount = sizeof(kMetricDefs) / sizeof(kMetricDefs[0]);
static_assert(kMetricDefCount == kMetricNameCount, "名字表条数必须等于名字总数（S13 §5 的 45 条 + S12 移交的 1 条）");

// 反向护栏：注册表里每一项都必须有名字与 HELP，否则 /metrics 会静默少一行（S13 自审发现）。
constexpr bool counterDefsAreComplete() {
  bool seen[kCounterCount] = {};
  for (const MetricDef& def : kMetricDefs) {
    if (def.origin == Origin::kCounter && def.source < kCounterCount) seen[def.source] = true;
  }
  for (std::size_t i = 0u; i < kCounterCount; ++i) {
    if (!seen[i]) return false;
  }
  return true;
}

constexpr bool gaugeDefsAreComplete() {
  bool seen[kGaugeCount] = {};
  for (const MetricDef& def : kMetricDefs) {
    if (def.origin == Origin::kGauge && def.source < kGaugeCount) seen[def.source] = true;
  }
  for (std::size_t i = 0u; i < kGaugeCount; ++i) {
    if (!seen[i]) return false;
  }
  return true;
}

static_assert(counterDefsAreComplete(), "每个计数都必须有 /metrics 名字");
static_assert(gaugeDefsAreComplete(), "每个量值都必须有 /metrics 名字");

std::string formatProcessValue(const MetricDef& def, const ProcessSnapshot& process) {
  char buf[128];
  switch (static_cast<ProcessField>(def.source)) {
    case ProcessField::kVersion:
      std::snprintf(buf, sizeof(buf), "{version=\"%.*s\",protocol=\"%u\",tick_ms=\"%u\"} 1",
                    static_cast<int>(process.version.size()), process.version.data(),
                    static_cast<unsigned>(process.protocol), static_cast<unsigned>(process.tickMs));
      return std::string(buf);
    case ProcessField::kRooms:
      std::snprintf(buf, sizeof(buf), " %u", static_cast<unsigned>(process.rooms));
      return std::string(buf);
    case ProcessField::kConnections:
      std::snprintf(buf, sizeof(buf), " %u", static_cast<unsigned>(process.connections));
      return std::string(buf);
    case ProcessField::kPlayers:
      std::snprintf(buf, sizeof(buf), " %u", static_cast<unsigned>(process.players));
      return std::string(buf);
    case ProcessField::kGraceActive:
      std::snprintf(buf, sizeof(buf), " %u", static_cast<unsigned>(process.graceActive));
      return std::string(buf);
    case ProcessField::kRecordsRetained:
      std::snprintf(buf, sizeof(buf), " %llu",
                    static_cast<unsigned long long>(process.recordsRetained));
      return std::string(buf);
    case ProcessField::kUptimeSeconds:
      std::snprintf(buf, sizeof(buf), " %llu", static_cast<unsigned long long>(process.uptimeSeconds));
      return std::string(buf);
  }
  return std::string(" 0");
}

}  // namespace

std::string renderMetrics(const MetricsInput& input) {
  std::string out;
  out.reserve(4096u);
  for (std::size_t index = 0u; index < kMetricDefCount; ++index) {
    const MetricDef& def = kMetricDefs[index];
    out += "# HELP ";
    out += def.name;
    out += ' ';
    out += def.help;
    out += "\n# TYPE ";
    out += def.name;
    out += metricKind(index) == MetricKind::kCounter ? " counter\n" : " gauge\n";
    out += def.name;
    switch (def.origin) {
      case Origin::kCounter: {
        const std::uint64_t value = input.counters != nullptr
                                        ? counterValue(*input.counters, static_cast<CounterId>(def.source))
                                        : 0u;
        char buf[32];
        std::snprintf(buf, sizeof(buf), " %llu\n", static_cast<unsigned long long>(value));
        out += buf;
        break;
      }
      case Origin::kGauge: {
        const double value = input.gauges != nullptr
                                 ? gaugeValue(*input.gauges, static_cast<GaugeId>(def.source))
                                 : 0.0;
        char buf[32];
        std::snprintf(buf, sizeof(buf), " %.3f\n", value);
        out += buf;
        break;
      }
      case Origin::kProcess:
        out += formatProcessValue(def, input.process);
        out += '\n';
        break;
    }
  }
  return out;
}

std::size_t metricNameCount() noexcept { return kMetricDefCount; }

std::string_view metricName(std::size_t index) noexcept {
  return index < kMetricDefCount ? std::string_view(kMetricDefs[index].name) : std::string_view{};
}

MetricKind metricKind(std::size_t index) noexcept {
  if (index >= kMetricDefCount) return MetricKind::kCounter;
  return kMetricDefs[index].origin == Origin::kCounter ? MetricKind::kCounter : MetricKind::kGauge;
}

bool isMetricNameRegistered(std::string_view name) noexcept {
  for (const MetricDef& def : kMetricDefs) {
    if (name == def.name) return true;
  }
  return false;
}

}  // namespace ac::metrics
