#include "perf/thresholds.hpp"

#include <cmath>
#include <cstdio>

#include "core/json_text.hpp"
#include "core/version.hpp"

namespace ac::perf {
namespace {

constexpr ThresholdDef kThresholds[kThresholdCount] = {
    {ThresholdId::kCpu, "G1", "4 人 + 60 羊单核占用", "cpuMeanPct", 30.0, "%", true},
    {ThresholdId::kClientBandwidth, "G2", "每客户端出站带宽", "bytesPerClientMaxKbps", 40.0,
     "KB/s", false},
    {ThresholdId::kSnapshotBytesP95, "G3", "快照帧字节 P95", "snapshotBytesP95", 1228.0, "B", false},
    {ThresholdId::kSnapshotBytesMax, "G4", "单快照帧字节上限", "snapshotBytesMax", 2048.0, "B",
     false},
    {ThresholdId::kHardCorrect, "G5", "硬纠正", "hardCorrectPerMinPerClient", 5.0, "次/分钟/人",
     false},
    {ThresholdId::kScheduleErrorP95, "G6", "tick 调度误差 P95", "scheduleErrorP95Ms", 8.0, "ms",
     false},
    {ThresholdId::kRssSlope, "G7", "RSS 增长斜率", "rssSlopeMbPerMin", 1.0, "MB/分钟", true},
    {ThresholdId::kZeroCounters, "G8", "事件丢弃 / 未捕获异常 / tick 跳过", "zeroCounters", 0.0, "次",
     false},
};

double measuredValue(const PerfSample& sample, ThresholdId id) noexcept {
  const PerfMetrics& metrics = sample.metrics;
  switch (id) {
    case ThresholdId::kCpu: return metrics.cpuMeanPct;
    case ThresholdId::kClientBandwidth: return metrics.bytesPerClientMaxKbps;
    case ThresholdId::kSnapshotBytesP95: return metrics.snapshotBytesP95;
    case ThresholdId::kSnapshotBytesMax: return metrics.snapshotBytesMax;
    case ThresholdId::kHardCorrect: return metrics.hardCorrectPerMinPerClient;
    case ThresholdId::kScheduleErrorP95: return metrics.scheduleErrorP95Ms;
    case ThresholdId::kRssSlope: return metrics.rssSlopeMbPerMin;
    case ThresholdId::kZeroCounters: {
      const double dropped = static_cast<double>(metrics.eventsDropped);
      const double uncaught = static_cast<double>(metrics.uncaughtExceptions);
      const double skips = static_cast<double>(metrics.tickSkips);
      const double worst = dropped > uncaught ? dropped : uncaught;
      return worst > skips ? worst : skips;
    }
  }
  return 0.0;
}

bool isWithin(const ThresholdDef& def, double measured, double limit) noexcept {
  return def.isStrictlyBelow ? measured < limit : measured <= limit;
}

void appendKey(std::string& out, const char* key) {
  out += '"';
  out += key;
  out += "\":";
}

void appendU64(std::string& out, std::uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  out += buffer;
}

void appendDouble(std::string& out, double value) {
  char buffer[32];
  // 三态量值都保留 3 位小数：门槛量级从 0 到 1228，3 位足够分辨且便于逐字比对。
  std::snprintf(buffer, sizeof(buffer), "%.3f", value);
  out += buffer;
}

}  // namespace

const char* thresholdStatusName(ThresholdStatus status) noexcept {
  switch (status) {
    case ThresholdStatus::kPass: return "pass";
    case ThresholdStatus::kFail: return "fail";
    case ThresholdStatus::kNotMeasured: return "not-measured";
  }
  return "not-measured";
}

const ThresholdDef& thresholdDef(ThresholdId id) noexcept {
  return kThresholds[static_cast<std::size_t>(id)];
}

const ThresholdDef* thresholdDefs() noexcept { return kThresholds; }

const ThresholdDef* findThresholdByName(std::string_view name) noexcept {
  for (std::size_t i = 0u; i < kThresholdCount; ++i) {
    const ThresholdDef& def = kThresholds[i];
    if (name == def.name || name == def.label || name == def.alias) return &def;
    // 大小写不敏感只对两位的 G 编号有意义（"g3" 与 "G3" 等价）。
    if (name.size() == 2u && (name[0] == 'g' || name[0] == 'G') && name[1] == def.name[1]) {
      return &def;
    }
  }
  return nullptr;
}

PerfVerdict evaluateThresholds(const PerfSample& sample, const double* overrides) {
  PerfVerdict verdict{};
  std::size_t failCount = 0u;
  std::size_t measuredCount = 0u;
  bool isScheduleFallbackUsed = false;

  for (std::size_t i = 0u; i < kThresholdCount; ++i) {
    const ThresholdDef& def = kThresholds[i];
    const double override = overrides == nullptr ? kNoOverride : overrides[i];
    const double limit = override == kNoOverride || override < 0.0 ? def.limit : override;
    double measured = measuredValue(sample, def.id);
    ThresholdResult& result = verdict.results[i];
    result.id = def.id;
    result.measured = measured;
    result.limit = limit;
    result.isScheduleFallbackUsed = false;

    if (!sample.isMeasuredId(def.id)) {
      result.status = ThresholdStatus::kNotMeasured;
      continue;
    }
    ++measuredCount;
    bool isWithinLimit = isWithin(def, measured, limit);
    if (!isWithinLimit && def.id == ThresholdId::kScheduleErrorP95 && sample.isScheduleFallbackEnabled) {
      // §5 的替代判据：前/后 1/3 的 P95 差 ≤ 2 ms 且 |漂移| ≤ 50 ms。
      const double gap = sample.scheduleHeadTailGapMs;
      const double drift = sample.metrics.simDriftMsMax < 0.0 ? -sample.metrics.simDriftMsMax
                                                             : sample.metrics.simDriftMsMax;
      isWithinLimit = gap <= kScheduleFallbackGapLimitMs && drift <= kScheduleFallbackDriftLimitMs;
      if (isWithinLimit) {
        result.isScheduleFallbackUsed = true;
        result.measured = gap;
        result.limit = kScheduleFallbackGapLimitMs;
        isScheduleFallbackUsed = true;
      }
    }
    result.status = isWithinLimit ? ThresholdStatus::kPass : ThresholdStatus::kFail;
    if (!isWithinLimit) ++failCount;
  }

  verdict.isScenarioFailed = sample.isScenarioFailed;
  if (failCount > 0u || sample.isScenarioFailed) {
    verdict.overall = ThresholdStatus::kFail;
  } else if (measuredCount == 0u) {
    verdict.overall = ThresholdStatus::kNotMeasured;
  } else {
    verdict.overall = ThresholdStatus::kPass;
  }
  // §5：任一 fail → 1；只有 not-measured → 0（并在报告头标注不拦人）。
  verdict.exitCode = verdict.overall == ThresholdStatus::kFail ? 1 : 0;
  if (isScheduleFallbackUsed) verdict.note = "G6 用替代判据：前后 1/3 P95 差与 sim_drift 均在限内";
  if (verdict.overall == ThresholdStatus::kNotMeasured) {
    verdict.note = verdict.note.empty() ? "全部门槛未采集：本跑次不拦人"
                                        : verdict.note + "；全部门槛未采集：本跑次不拦人";
  }
  return verdict;
}

std::string encodePerfReport(const PerfReportHeader& header, const PerfSample& sample,
                             const PerfVerdict& verdict) {
  const PerfMetrics& metrics = sample.metrics;
  std::string out;
  out.reserve(2048u);
  out += '{';
  appendKey(out, "tool");
  out += ac::core::json::quote(header.tool);
  out += ',';
  appendKey(out, "version");
  out += ac::core::json::quote(ac::version::kVersion);
  out += ',';
  appendKey(out, "scenario");
  out += ac::core::json::quote(header.scenario);
  out += ',';
  appendKey(out, "seed");
  appendU64(out, header.seed);
  out += ',';
  appendKey(out, "buildType");
  out += ac::core::json::quote(header.buildType);
  out += ',';
  appendKey(out, "compiler");
  out += ac::core::json::quote(header.compiler);
  out += ',';
  appendKey(out, "host");
  out += ac::core::json::quote(header.host);
  out += ',';
  appendKey(out, "startedAt");
  out += ac::core::json::quote(header.startedAt);
  out += ',';
  appendKey(out, "durationSec");
  appendDouble(out, header.durationSec);
  out += ',';
  appendKey(out, "verdict");
  out += ac::core::json::quote(thresholdStatusName(verdict.overall));
  out += ',';
  appendKey(out, "exitCode");
  appendU64(out, static_cast<std::uint64_t>(verdict.exitCode));
  out += ',';
  appendKey(out, "note");
  out += ac::core::json::quote(verdict.note);

  out += ',';
  appendKey(out, "thresholds");
  out += '[';
  for (std::size_t i = 0u; i < kThresholdCount; ++i) {
    const ThresholdDef& def = kThresholds[i];
    const ThresholdResult& result = verdict.results[i];
    if (i != 0u) out += ',';
    out += '{';
    appendKey(out, "id");
    out += ac::core::json::quote(def.name);
    out += ',';
    appendKey(out, "label");
    out += ac::core::json::quote(def.label);
    out += ',';
    appendKey(out, "measured");
    appendDouble(out, result.measured);
    out += ',';
    appendKey(out, "limit");
    appendDouble(out, result.limit);
    out += ',';
    appendKey(out, "unit");
    out += ac::core::json::quote(def.unit);
    out += ',';
    appendKey(out, "status");
    out += ac::core::json::quote(thresholdStatusName(result.status));
    out += '}';
  }
  out += ']';

  out += ',';
  appendKey(out, "metrics");
  out += '{';
  appendKey(out, "cpuMeanPct");
  appendDouble(out, metrics.cpuMeanPct);
  out += ',';
  appendKey(out, "cpuP95Pct");
  appendDouble(out, metrics.cpuP95Pct);
  out += ',';
  appendKey(out, "bytesPerClientMaxKbps");
  appendDouble(out, metrics.bytesPerClientMaxKbps);
  out += ',';
  appendKey(out, "snapshotBytesP95");
  appendDouble(out, metrics.snapshotBytesP95);
  out += ',';
  appendKey(out, "snapshotBytesMax");
  appendDouble(out, metrics.snapshotBytesMax);
  out += ',';
  appendKey(out, "hardCorrectPerMinPerClient");
  appendDouble(out, metrics.hardCorrectPerMinPerClient);
  out += ',';
  appendKey(out, "scheduleErrorP95Ms");
  appendDouble(out, metrics.scheduleErrorP95Ms);
  out += ',';
  appendKey(out, "simDriftMsMax");
  appendDouble(out, metrics.simDriftMsMax);
  out += ',';
  appendKey(out, "rssSlopeMbPerMin");
  appendDouble(out, metrics.rssSlopeMbPerMin);
  out += ',';
  appendKey(out, "eventsDropped");
  appendU64(out, metrics.eventsDropped);
  out += ',';
  appendKey(out, "tickSkips");
  appendU64(out, metrics.tickSkips);
  out += ',';
  appendKey(out, "uncaughtExceptions");
  appendU64(out, metrics.uncaughtExceptions);
  out += '}';
  out += '}';
  return out;
}

}  // namespace ac::perf
