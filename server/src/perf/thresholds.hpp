#pragma once
// S14 §5 冻结门槛表（G1–G8）的**唯一**判定实现：工具（ac_gate）与用例（threshold_test）共用同一份表。
// 改表必须先改计划 §5；本文件的 limit/label/unit 与 §5 表格逐字对应。
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ac::perf {

inline constexpr std::size_t kThresholdCount = 8u;
inline constexpr double kNoOverride = -1.0;  // overrides 里表示「用默认 limit」

enum class ThresholdId : std::uint8_t {
  kCpu = 0,             // G1 4 人 + 60 羊单核占用
  kClientBandwidth,     // G2 每客户端出站带宽
  kSnapshotBytesP95,    // G3 快照帧字节 P95
  kSnapshotBytesMax,    // G4 单快照帧字节上限
  kHardCorrect,         // G5 硬纠正次数
  kScheduleErrorP95,    // G6 tick 调度误差 P95
  kRssSlope,            // G7 RSS 增长斜率
  kZeroCounters,        // G8 事件丢弃 / 未捕获异常 / tick 跳过
};

enum class ThresholdStatus : std::uint8_t { kPass = 0u, kFail = 1u, kNotMeasured = 2u };

const char* thresholdStatusName(ThresholdStatus status) noexcept;

struct ThresholdDef {
  ThresholdId id;
  const char* name;   // "G1".."G8"
  const char* label;  // 报告里的可读名
  const char* alias;  // --break 的别名（= 报告 metrics 字段名）
  double limit;
  const char* unit;
  bool isStrictlyBelow;  // true: measured < limit；false: measured <= limit
};

const ThresholdDef& thresholdDef(ThresholdId id) noexcept;
const ThresholdDef* findThresholdByName(std::string_view name) noexcept;  // "G3" / "g3" / alias
const ThresholdDef* thresholdDefs() noexcept;                             // 长度 kThresholdCount

// 报告 metrics 段的 12 个字段（§5 字段名逐字一致）。
struct PerfMetrics {
  double cpuMeanPct = 0.0;
  double cpuP95Pct = 0.0;
  double bytesPerClientMaxKbps = 0.0;
  double snapshotBytesP95 = 0.0;
  double snapshotBytesMax = 0.0;
  double hardCorrectPerMinPerClient = 0.0;
  double scheduleErrorP95Ms = 0.0;
  double simDriftMsMax = 0.0;
  double rssSlopeMbPerMin = 0.0;
  std::uint64_t eventsDropped = 0u;
  std::uint64_t tickSkips = 0u;
  std::uint64_t uncaughtExceptions = 0u;
};

// G6 的替代判据（§5 风险表）：本机定时器粒度 > 8 ms 时，前/后 1/3 的 P95 差 ≤ 2 ms 且 |漂移| ≤ 50 ms。
inline constexpr double kScheduleFallbackGapLimitMs = 2.0;
inline constexpr double kScheduleFallbackDriftLimitMs = 50.0;

struct PerfSample {
  PerfMetrics metrics{};
  bool isMeasured[kThresholdCount] = {true, true, true, true, true, true, true, true};
  // G6：直接判据失败时是否用替代判据（调用方按本机定时器粒度决定）。
  // 前后段差由 core::scheduleHeadTailGapMs 直接给出（S12 已定口径），本模块不重算。
  bool isScheduleFallbackEnabled = false;
  double scheduleHeadTailGapMs = 0.0;
  // 场景级附加判据失败（soak 房间未回落等）：整跑次判红、退出码 1（评审 #1 的入口）。
  bool isScenarioFailed = false;

  bool isMeasuredId(ThresholdId id) const noexcept {
    return isMeasured[static_cast<std::size_t>(id)];
  }
};

struct ThresholdResult {
  ThresholdId id;
  double measured;
  double limit;
  ThresholdStatus status;
  bool isScheduleFallbackUsed;
};

struct PerfVerdict {
  ThresholdResult results[kThresholdCount];
  ThresholdStatus overall = ThresholdStatus::kPass;
  int exitCode = 0;
  // 场景级附加判据（如 soak 结束后的房间回落）失败时置位：退出码同样为 1。
  bool isScenarioFailed = false;
  std::string note;
};

// overrides（可空）= 每个 id 的临时 limit；kNoOverride 表示用默认值（§5 的 --break）。
PerfVerdict evaluateThresholds(const PerfSample& sample, const double* overrides = nullptr);

struct PerfReportHeader {
  std::string_view tool = "ac_gate";
  std::string_view scenario = "gate-4p2min";
  std::string_view buildType = "Release";
  std::string_view compiler = "";
  std::string_view host = "";
  std::string_view startedAt = "";
  std::uint32_t seed = 0u;
  double durationSec = 0.0;
};

// §5 冻结的报告 JSON（字段名逐字一致；note 为 §8 风险表要求的补充字段）。
std::string encodePerfReport(const PerfReportHeader& header, const PerfSample& sample,
                             const PerfVerdict& verdict);

}  // namespace ac::perf
