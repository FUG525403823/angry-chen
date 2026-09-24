#pragma once
// S13 §5「单局诊断报告 reports/<matchId>.json」：8 组字段的聚合形状、单行 JSON 编码与落盘。
//
// 字段名即 §5 的点路径（ticks.jitterMsP50 等），报告与 /metrics 两侧引用的是同一批计数器与量值；
// 落盘按 mtime 保留最近 kMaxReports 份，清理失败只记日志、不抛异常（§8 风险表）。
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/scheduler.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"

namespace ac::report {

inline constexpr std::size_t kMaxReports = 200u;  // §5：按 mtime 保留最近 200 份

struct TickSeries {
  std::uint32_t total = 0u;    // 执行 tick 数
  std::uint32_t skipped = 0u;  // 真正丢掉的 tick 数
  double jitterMsP50 = 0.0;
  double jitterMsP95 = 0.0;
  double scheduleErrorMsP95 = 0.0;
  double workMsP95 = 0.0;
  double workMsP99 = 0.0;
  std::int64_t simDriftMsMax = 0;
};

struct NetSeries {
  double snapshotBytesAvg = 0.0;
  std::uint64_t snapshotBytesMax = 0u;
  std::uint64_t bytesOutTotal = 0u;
  std::uint64_t messagesInTotal = 0u;
  std::uint64_t malformedInTotal = 0u;
  std::uint64_t rateLimitedTotal = 0u;
};

struct FairSeries {
  std::uint64_t hardCorrectTotal = 0u;
  std::uint64_t poseSuspectTotal = 0u;
  std::uint64_t poseRejectedTotal = 0u;
  std::uint64_t rewindClampedTotal = 0u;
  std::uint64_t speedViolationsTotal = 0u;
  std::uint64_t shotsFiredTotal = 0u;
  std::uint64_t hitsTotal = 0u;
};

struct GraceSeries {
  std::uint32_t starts = 0u;
  std::uint32_t reconnects = 0u;
  std::uint32_t timeouts = 0u;
};

struct PeakSeries {
  std::uint32_t entities = 0u;
  std::uint32_t players = 0u;
};

struct MatchDiagnostics {
  std::string matchId;
  std::uint64_t durationMs = 0u;
  TickSeries ticks{};
  NetSeries net{};
  FairSeries fair{};
  GraceSeries grace{};
  PeakSeries peak{};
};

// §9 的 `build_match_diagnostics`：调用方手里有、而两张注册表里没有的量（对局标识、峰值、命中数）。
struct MatchRunSummary {
  std::string matchId;
  std::uint64_t durationMs = 0u;
  std::int64_t simDriftMsMax = 0;
  std::uint64_t shotsFiredTotal = 0u;
  std::uint64_t hitsTotal = 0u;
  std::uint32_t peakEntities = 0u;
  std::uint32_t peakPlayers = 0u;
};

// 从两张注册表 + 调度器采样组装诊断：一个字段只有一个来源（grace.* 取自计数、ticks.* 取自调度器），
// 报告与 /metrics 因此不可能对同一件事给出两个数。
MatchDiagnostics buildMatchDiagnostics(const MatchRunSummary& summary,
                                       const ac::metrics::CounterRegistry& counters,
                                       const ac::metrics::GaugeRegistry& gauges,
                                       const ac::core::TickScheduler& scheduler);

// 单行 JSON：matchId,durationMs,ticks{...},net{...},fair{...},grace{...},peak{...}（键名即 §5 的点路径）。
std::string encodeDiagnosticsJson(const MatchDiagnostics& diagnostics);

// 报告目录：<dataDir>/reports（目录名与战绩文件同源，见 persist/match_store.hpp）。
std::string reportDirOf(std::string_view dataDir);

struct ReportWrite {
  bool isOk = false;
  std::string path;
  std::size_t removedOldReports = 0u;
  std::string error;
};

// 写 <dataDir>/reports/<matchId>.json，随后按 mtime 保留最近 kMaxReports 份。
// matchId 必须是文件名安全字符集（persist::isSafeMatchId），否则直接失败并写 *error。
ReportWrite writeReport(std::string_view dataDir, const MatchDiagnostics& diagnostics,
                        std::string* error = nullptr);

// 只保留最近 keep 份（按 mtime 降序，同 mtime 用路径降序决胜）；返回删除份数。
std::size_t pruneReports(std::string_view reportDir, std::size_t keep = kMaxReports,
                         std::string* error = nullptr);

}  // namespace ac::report
