// S13 §5 的单局诊断报告：8 组字段的点路径齐全、字段只有一个来源、落盘路径与保留策略（200 份）、
// 非法 matchId 与写失败的处理（记 report.write_failed 且不抛异常）。
#include "tiny_test.hpp"
#include "log_guard.hpp"
#include "test_io.hpp"
#include "tmp_workdir.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>

#include "core/log.hpp"
#include "core/scheduler.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"
#include "persist/match_store.hpp"
#include "replication/backpressure.hpp"
#include "report.hpp"

namespace {

namespace fs = std::filesystem;

ac::report::MatchDiagnostics sampleDiagnostics(const char* id) {
  ac::report::MatchDiagnostics diagnostics;
  diagnostics.matchId = id;
  diagnostics.durationMs = 60000u;
  diagnostics.ticks.total = 1200u;
  diagnostics.ticks.skipped = 2u;
  diagnostics.ticks.jitterMsP50 = 0.5;
  diagnostics.ticks.jitterMsP95 = 4.0;
  diagnostics.ticks.scheduleErrorMsP95 = 4.0;
  diagnostics.ticks.workMsP95 = 2.0;
  diagnostics.ticks.workMsP99 = 3.5;
  diagnostics.ticks.simDriftMsMax = 12;
  diagnostics.net.snapshotBytesAvg = 442.4;
  diagnostics.net.snapshotBytesMax = 473u;
  diagnostics.net.bytesOutTotal = 900u;
  diagnostics.net.messagesInTotal = 120u;
  diagnostics.net.malformedInTotal = 1u;
  diagnostics.net.rateLimitedTotal = 2u;
  diagnostics.fair.hardCorrectTotal = 3u;
  diagnostics.fair.poseSuspectTotal = 4u;
  diagnostics.fair.poseRejectedTotal = 1u;
  diagnostics.fair.rewindClampedTotal = 5u;
  diagnostics.fair.speedViolationsTotal = 6u;
  diagnostics.fair.shotsFiredTotal = 40u;
  diagnostics.fair.hitsTotal = 17u;
  diagnostics.grace.starts = 1u;
  diagnostics.grace.reconnects = 1u;
  diagnostics.grace.timeouts = 0u;
  diagnostics.peak.entities = 64u;
  diagnostics.peak.players = 4u;
  return diagnostics;
}

}  // namespace

AC_TEST(report_json_has_every_frozen_field_path) {
  const std::string json = ac::report::encodeDiagnosticsJson(sampleDiagnostics("ABCD-1700000000000"));
  const char* keys[] = {
      "\"matchId\":\"ABCD-1700000000000\"",
      "\"durationMs\":60000",
      "\"ticks\":{\"total\":1200,\"skipped\":2",
      "\"jitterMsP50\":0.500",
      "\"jitterMsP95\":4.000",
      "\"scheduleErrorMsP95\":4.000",
      "\"workMsP95\":2.000",
      "\"workMsP99\":3.500",
      "\"simDriftMsMax\":12",
      "\"net\":{\"snapshotBytesAvg\":442.400,\"snapshotBytesMax\":473",
      "\"bytesOutTotal\":900",
      "\"messagesInTotal\":120",
      "\"malformedInTotal\":1",
      "\"rateLimitedTotal\":2",
      "\"fair\":{\"hardCorrectTotal\":3",
      "\"poseSuspectTotal\":4",
      "\"poseRejectedTotal\":1",
      "\"rewindClampedTotal\":5",
      "\"speedViolationsTotal\":6",
      "\"shotsFiredTotal\":40",
      "\"hitsTotal\":17",
      "\"grace\":{\"starts\":1,\"reconnects\":1,\"timeouts\":0}",
      "\"peak\":{\"entities\":64,\"players\":4}",
  };
  for (const char* key : keys) {
    AC_CHECK(json.find(key) != std::string::npos);
  }
  AC_CHECK(json.rfind("{\"matchId\":", 0) == 0u);
  AC_CHECK_EQ(json.back(), '}');
}

AC_TEST(report_writes_file_named_after_record) {
  ac::test::TempDir dir("report");
  AC_CHECK(dir.isReady());
  const std::string dataDir = dir.file("data");
  AC_CHECK_EQ(ac::report::reportDirOf(dataDir), dataDir + "/reports");
  const ac::report::ReportWrite outcome =
      ac::report::writeReport(dataDir, sampleDiagnostics("ABCD-1700000000000"), nullptr);
  AC_CHECK(outcome.isOk);
  AC_CHECK_EQ(outcome.path, dataDir + "/reports/ABCD-1700000000000.json");
  AC_CHECK_EQ(std::filesystem::exists(outcome.path), true);
  AC_CHECK(ac::test::readTextFile(outcome.path).find("\"matchId\":\"ABCD-1700000000000\"") != std::string::npos);
  AC_CHECK_EQ(ac::report::kMaxReports, static_cast<std::size_t>(200));
}

AC_TEST(report_rejects_unsafe_file_name) {
  ac::test::TempDir dir("report");
  const std::string dataDir = dir.file("data");
  ac::report::MatchDiagnostics diagnostics = sampleDiagnostics("../escape");
  std::string error;
  const ac::report::ReportWrite outcome = ac::report::writeReport(dataDir, diagnostics, &error);
  AC_CHECK(!outcome.isOk);
  AC_CHECK_EQ(error.empty(), false);
  AC_CHECK_EQ(ac::test::countFiles(dataDir + "/reports"), static_cast<std::size_t>(0));
}

AC_TEST(report_keeps_only_latest_two_hundred) {
  ac::test::TempDir dir("report");
  const std::string dataDir = dir.file("data");
  std::size_t removedTotal = 0u;
  for (std::size_t i = 0u; i < ac::report::kMaxReports + 5u; ++i) {
    char id[32];
    std::snprintf(id, sizeof(id), "r-%03zu", i);
    const ac::report::ReportWrite outcome = ac::report::writeReport(dataDir, sampleDiagnostics(id), nullptr);
    AC_CHECK(outcome.isOk);
    removedTotal += outcome.removedOldReports;
  }
  AC_CHECK_EQ(ac::test::countFiles(dataDir + "/reports"), ac::report::kMaxReports);
  AC_CHECK_EQ(removedTotal, static_cast<std::size_t>(5));
  AC_CHECK_EQ(std::filesystem::exists(dataDir + "/reports/r-204.json"), true);
  AC_CHECK_EQ(std::filesystem::exists(dataDir + "/reports/r-000.json"), false);
}

AC_TEST(report_write_error_is_logged_and_reported) {
  ac::test::LogGuard guard;
  ac::test::TempDir dir("report");
  const std::string logPath = dir.file("report.log");
  AC_CHECK(ac::log::useFile(logPath));
  const std::string blocker = dir.file("blocker");
  std::FILE* file = std::fopen(blocker.c_str(), "wb");
  AC_CHECK(file != nullptr);
  if (file == nullptr) return;
  std::fputs("x", file);
  std::fclose(file);
  std::string error;
  const ac::report::ReportWrite outcome =
      ac::report::writeReport(blocker + "/sub", sampleDiagnostics("ABCD-1"), &error);
  ac::log::close();
  AC_CHECK(!outcome.isOk);
  AC_CHECK_EQ(error.empty(), false);
  const std::string log = ac::test::readTextFile(logPath);
  AC_CHECK(log.find("\"evt\":\"report.write_failed\"") != std::string::npos);
  AC_CHECK(log.find("ABCD-1") != std::string::npos);
}

AC_TEST(report_relations_from_live_counters_hold) {
  ac::core::TickScheduler scheduler{};
  ac::metrics::CounterRegistry counters{};
  ac::metrics::GaugeRegistry gauges{};
  ac::core::startScheduler(scheduler, 1000u, 0u);
  ac::replication::OutboundBudget budget{};
  std::uint64_t now = 1000u;
  for (std::size_t i = 0u; i < 120u; ++i) {
    now += 50u + (i % 3u);
    const std::uint32_t work = static_cast<std::uint32_t>(1u + (i % 5u));
    ac::core::noteTickRun(scheduler, now, work, &counters, &gauges);
    ac::replication::enqueueSnapshot(budget, 200u + (i % 7u) * 50u, &counters, &gauges, 8u + (i % 5u));
    ac::metrics::addCounter(counters, ac::metrics::CounterId::kBytesOut, 300u);
    ac::metrics::addCounter(counters, ac::metrics::CounterId::kFramesIn, 2u);
    ac::metrics::addCounter(counters, ac::metrics::CounterId::kMalformedFrames, 1u);
    ac::metrics::addCounter(counters, ac::metrics::CounterId::kRateLimitedFrames, 1u);
    ac::metrics::addCounter(counters, ac::metrics::CounterId::kGraceStarts, 1u);
  }
  ac::report::MatchRunSummary summary;
  summary.matchId = "LIVE-1700000000000";
  summary.durationMs = now - 1000u;
  summary.simDriftMsMax = 12;
  summary.shotsFiredTotal = 40u;
  summary.hitsTotal = 17u;
  summary.peakEntities = 64u;
  summary.peakPlayers = 4u;

  const ac::report::MatchDiagnostics diagnostics =
      ac::report::buildMatchDiagnostics(summary, counters, gauges, scheduler);
  AC_CHECK_EQ(diagnostics.ticks.total, static_cast<std::uint32_t>(120));
  AC_CHECK_EQ(diagnostics.ticks.skipped, static_cast<std::uint32_t>(0));
  AC_CHECK(diagnostics.ticks.jitterMsP50 <= diagnostics.ticks.jitterMsP95);
  AC_CHECK(diagnostics.ticks.workMsP95 <= diagnostics.ticks.workMsP99);
  AC_CHECK(diagnostics.net.snapshotBytesMax >= diagnostics.net.snapshotBytesAvg);
  AC_CHECK_EQ(diagnostics.net.malformedInTotal, static_cast<std::uint64_t>(120));
  AC_CHECK_EQ(diagnostics.net.rateLimitedTotal, static_cast<std::uint64_t>(120));
  AC_CHECK_EQ(diagnostics.net.messagesInTotal, static_cast<std::uint64_t>(240));
  AC_CHECK_EQ(diagnostics.net.bytesOutTotal, static_cast<std::uint64_t>(36000));
  AC_CHECK_EQ(diagnostics.grace.starts, static_cast<std::uint32_t>(120));
  AC_CHECK_EQ(diagnostics.fair.shotsFiredTotal, static_cast<std::uint64_t>(40));
  AC_CHECK_EQ(diagnostics.fair.hitsTotal, static_cast<std::uint64_t>(17));
  AC_CHECK_EQ(diagnostics.peak.entities, static_cast<std::uint32_t>(64));
  AC_CHECK_EQ(diagnostics.ticks.simDriftMsMax, static_cast<std::int64_t>(12));

  const std::string json = ac::report::encodeDiagnosticsJson(diagnostics);
  AC_CHECK(json.find("\"ticks\":{\"total\":120") != std::string::npos);
  if (ac::test::reportPath()[0] != '\0') {
    AC_CHECK(ac::test::writeReportFile(json.c_str()));
  }
}
