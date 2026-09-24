#include "report.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <utility>
#include <vector>

#include "core/json_text.hpp"
#include "core/log.hpp"
#include "persist/match_store.hpp"

namespace ac::report {
namespace {

namespace fs = std::filesystem;

std::string joinPath(std::string_view directory, std::string_view name) {
  std::string out(directory);
  if (!out.empty() && out.back() != '/' && out.back() != '\\') out.push_back('/');
  out.append(name);
  return out;
}

// 每个类型只有一个格式化点，键的写法只有一个（appendKeyValue）。
void appendRawU64(std::string& out, std::uint64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  out += buffer;
}

// §5：jitterMsP50 / jitterMsP95 保留 3 位小数；其余量值同口径。
void appendRawDouble(std::string& out, double value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.3f", value);
  out += buffer;
}

void appendRawI64(std::string& out, std::int64_t value) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
  out += buffer;
}

void appendKey(std::string& out, const char* key) {
  out += '"';
  out += key;
  out += "\":";
}

void appendNumber(std::string& out, const char* key, std::uint64_t value, bool isLast) {
  appendKey(out, key);
  appendRawU64(out, value);
  if (!isLast) out.push_back(',');
}

void appendDouble(std::string& out, const char* key, double value, bool isLast) {
  appendKey(out, key);
  appendRawDouble(out, value);
  if (!isLast) out.push_back(',');
}

void appendSigned(std::string& out, const char* key, std::int64_t value, bool isLast) {
  appendKey(out, key);
  appendRawI64(out, value);
  if (!isLast) out.push_back(',');
}

struct ReportFile {
  fs::file_time_type writtenAt{};
  std::string path;
};

}  // namespace

MatchDiagnostics buildMatchDiagnostics(const MatchRunSummary& summary,
                                       const ac::metrics::CounterRegistry& counters,
                                       const ac::metrics::GaugeRegistry& gauges,
                                       const ac::core::TickScheduler& scheduler) {
  using ac::metrics::CounterId;
  using ac::metrics::GaugeId;
  MatchDiagnostics diagnostics;
  diagnostics.matchId = summary.matchId;
  diagnostics.durationMs = summary.durationMs;

  diagnostics.ticks.total = scheduler.tickIndex;
  diagnostics.ticks.skipped = scheduler.tickSkips;
  diagnostics.ticks.jitterMsP50 = ac::core::tickJitterP50Ms(scheduler);
  diagnostics.ticks.jitterMsP95 = ac::core::tickJitterP95Ms(scheduler);
  diagnostics.ticks.scheduleErrorMsP95 = ac::core::tickScheduleErrorP95Ms(scheduler);
  diagnostics.ticks.workMsP95 = ac::core::tickWorkP95Ms(scheduler);
  diagnostics.ticks.workMsP99 = ac::core::tickWorkP99Ms(scheduler);
  diagnostics.ticks.simDriftMsMax = summary.simDriftMsMax;

  diagnostics.net.snapshotBytesAvg = ac::metrics::gaugeValue(gauges, GaugeId::kSnapshotBytesAvg);
  diagnostics.net.snapshotBytesMax =
      static_cast<std::uint64_t>(ac::metrics::gaugeValue(gauges, GaugeId::kSnapshotBytesMax));
  diagnostics.net.bytesOutTotal = ac::metrics::counterValue(counters, CounterId::kBytesOut);
  diagnostics.net.messagesInTotal = ac::metrics::counterValue(counters, CounterId::kFramesIn);
  diagnostics.net.malformedInTotal = ac::metrics::counterValue(counters, CounterId::kMalformedFrames);
  diagnostics.net.rateLimitedTotal = ac::metrics::counterValue(counters, CounterId::kRateLimitedFrames);

  diagnostics.fair.hardCorrectTotal = ac::metrics::counterValue(counters, CounterId::kHardCorrect);
  diagnostics.fair.poseSuspectTotal = ac::metrics::counterValue(counters, CounterId::kPoseSuspect);
  diagnostics.fair.poseRejectedTotal = ac::metrics::counterValue(counters, CounterId::kPoseRejected);
  diagnostics.fair.rewindClampedTotal = ac::metrics::counterValue(counters, CounterId::kRewindClamped);
  diagnostics.fair.speedViolationsTotal = ac::metrics::counterValue(counters, CounterId::kSpeedViolations);
  diagnostics.fair.shotsFiredTotal = summary.shotsFiredTotal;
  diagnostics.fair.hitsTotal = summary.hitsTotal;

  diagnostics.grace.starts =
      static_cast<std::uint32_t>(ac::metrics::counterValue(counters, CounterId::kGraceStarts));
  diagnostics.grace.reconnects =
      static_cast<std::uint32_t>(ac::metrics::counterValue(counters, CounterId::kGraceReconnects));
  diagnostics.grace.timeouts =
      static_cast<std::uint32_t>(ac::metrics::counterValue(counters, CounterId::kGraceTimeouts));

  diagnostics.peak.entities = summary.peakEntities;
  diagnostics.peak.players = summary.peakPlayers;
  return diagnostics;
}

std::string encodeDiagnosticsJson(const MatchDiagnostics& diagnostics) {
  std::string out;
  out.reserve(768u);
  out += "{\"matchId\":";
  out += ac::core::json::quote(diagnostics.matchId);
  out += ",\"durationMs\":";
  appendRawU64(out, diagnostics.durationMs);

  out += ",\"ticks\":{";
  appendNumber(out, "total", diagnostics.ticks.total, false);
  appendNumber(out, "skipped", diagnostics.ticks.skipped, false);
  appendDouble(out, "jitterMsP50", diagnostics.ticks.jitterMsP50, false);
  appendDouble(out, "jitterMsP95", diagnostics.ticks.jitterMsP95, false);
  appendDouble(out, "scheduleErrorMsP95", diagnostics.ticks.scheduleErrorMsP95, false);
  appendDouble(out, "workMsP95", diagnostics.ticks.workMsP95, false);
  appendDouble(out, "workMsP99", diagnostics.ticks.workMsP99, false);
  appendSigned(out, "simDriftMsMax", diagnostics.ticks.simDriftMsMax, true);
  out += '}';

  out += ",\"net\":{";
  appendDouble(out, "snapshotBytesAvg", diagnostics.net.snapshotBytesAvg, false);
  appendNumber(out, "snapshotBytesMax", diagnostics.net.snapshotBytesMax, false);
  appendNumber(out, "bytesOutTotal", diagnostics.net.bytesOutTotal, false);
  appendNumber(out, "messagesInTotal", diagnostics.net.messagesInTotal, false);
  appendNumber(out, "malformedInTotal", diagnostics.net.malformedInTotal, false);
  appendNumber(out, "rateLimitedTotal", diagnostics.net.rateLimitedTotal, true);
  out += '}';

  out += ",\"fair\":{";
  appendNumber(out, "hardCorrectTotal", diagnostics.fair.hardCorrectTotal, false);
  appendNumber(out, "poseSuspectTotal", diagnostics.fair.poseSuspectTotal, false);
  appendNumber(out, "poseRejectedTotal", diagnostics.fair.poseRejectedTotal, false);
  appendNumber(out, "rewindClampedTotal", diagnostics.fair.rewindClampedTotal, false);
  appendNumber(out, "speedViolationsTotal", diagnostics.fair.speedViolationsTotal, false);
  appendNumber(out, "shotsFiredTotal", diagnostics.fair.shotsFiredTotal, false);
  appendNumber(out, "hitsTotal", diagnostics.fair.hitsTotal, true);
  out += '}';

  out += ",\"grace\":{";
  appendNumber(out, "starts", diagnostics.grace.starts, false);
  appendNumber(out, "reconnects", diagnostics.grace.reconnects, false);
  appendNumber(out, "timeouts", diagnostics.grace.timeouts, true);
  out += '}';

  out += ",\"peak\":{";
  appendNumber(out, "entities", diagnostics.peak.entities, false);
  appendNumber(out, "players", diagnostics.peak.players, true);
  out += "}}";
  return out;
}

std::string reportDirOf(std::string_view dataDir) {
  return joinPath(dataDir, ac::persist::kReportDirName);
}

// 不做 noexcept：修剪要建列表并 push_back，坏分配应表现为「修剪失败」而不是 terminate。
std::size_t pruneReports(std::string_view reportDir, std::size_t keep, std::string* error) {
  std::error_code code;
  std::vector<ReportFile> files;
  fs::directory_iterator iterator(fs::path(std::string(reportDir)), code);
  if (code) {
    if (error != nullptr) *error = "cannot list reports: " + code.message();
    return 0u;
  }
  for (const fs::directory_entry& entry : iterator) {
    std::error_code entryCode;
    if (!entry.is_regular_file(entryCode) || entryCode) continue;
    ReportFile file;
    file.writtenAt = entry.last_write_time(entryCode);
    if (entryCode) continue;
    file.path = entry.path().string();
    files.push_back(std::move(file));
  }
  if (files.size() <= keep) return 0u;
  std::sort(files.begin(), files.end(), [](const ReportFile& a, const ReportFile& b) {
    if (a.writtenAt != b.writtenAt) return a.writtenAt > b.writtenAt;  // 新的在前
    return a.path > b.path;                                             // 同 mtime 用路径降序决胜
  });
  std::size_t removed = 0u;
  for (std::size_t i = keep; i < files.size(); ++i) {
    std::error_code removeCode;
    if (fs::remove(fs::path(files[i].path), removeCode) && !removeCode) ++removed;
  }
  return removed;
}

ReportWrite writeReport(std::string_view dataDir, const MatchDiagnostics& diagnostics,
                        std::string* error) {
  // 失败只记日志（§8 风险表：清理/落盘失败不抛异常），并把原因回传给调用方。
  const auto fail = [&diagnostics, error](std::string message) {
    ac::log::event(ac::log::Level::warn, "report.write_failed", {},
                   {ac::log::DetailField("matchId", std::string_view(diagnostics.matchId)),
                    ac::log::DetailField("error", std::string_view(message))});
    ReportWrite outcome;
    outcome.error = std::move(message);
    if (error != nullptr) *error = outcome.error;
    return outcome;
  };
  if (!ac::persist::isSafeMatchId(diagnostics.matchId)) return fail("unsafe matchId");
  const std::string directory = reportDirOf(dataDir);
  std::error_code code;
  fs::create_directories(fs::path(directory), code);
  if (code) return fail("cannot create report dir: " + code.message());
  ReportWrite outcome;
  const std::string path = joinPath(directory, diagnostics.matchId + ".json");
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) return fail("cannot open report file");
  const std::string text = encodeDiagnosticsJson(diagnostics);
  const std::size_t written = std::fwrite(text.data(), 1u, text.size(), file);
  const bool isFlushed = std::fflush(file) == 0;
  std::fclose(file);
  if (written != text.size() || !isFlushed) return fail("short write");
  outcome.isOk = true;
  outcome.path = path;
  std::string pruneError;
  outcome.removedOldReports = pruneReports(directory, kMaxReports, &pruneError);
  if (!pruneError.empty()) {  // 写成功但清理失败：记日志，不算本次写失败
    ac::log::event(ac::log::Level::warn, "report.write_failed", {},
                   {ac::log::DetailField("matchId", std::string_view(diagnostics.matchId)),
                    ac::log::DetailField("error", std::string_view(pruneError)),
                    ac::log::DetailField("removed", outcome.removedOldReports)});
    outcome.error = pruneError;
  }
  if (error != nullptr) *error = outcome.error;
  return outcome;
}

}  // namespace ac::report
