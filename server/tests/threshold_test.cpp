// S14 §5/§6-1/§5「反向自检」：门槛表与判定方向的用例（与 ac_gate 共用同一份实现）。
#include <cstdint>
#include <string>

#include "perf/thresholds.hpp"
#include "tiny_test.hpp"

namespace {

ac::perf::PerfSample passingSample() {
  ac::perf::PerfSample sample{};
  sample.metrics.cpuMeanPct = 12.5;
  sample.metrics.cpuP95Pct = 18.0;
  sample.metrics.bytesPerClientMaxKbps = 31.5;
  sample.metrics.snapshotBytesP95 = 900.0;
  sample.metrics.snapshotBytesMax = 1100.0;
  sample.metrics.hardCorrectPerMinPerClient = 0.0;
  sample.metrics.scheduleErrorP95Ms = 6.0;
  sample.metrics.simDriftMsMax = 10.0;
  sample.metrics.rssSlopeMbPerMin = 0.2;
  return sample;
}

double overridesWith(std::size_t index, double value, double* out) {
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) out[i] = ac::perf::kNoOverride;
  out[index] = value;
  return value;
}

}  // namespace

AC_TEST(threshold_all_metrics_within_limit_yields_pass) {
  const ac::perf::PerfSample sample = passingSample();
  const ac::perf::PerfVerdict verdict = ac::perf::evaluateThresholds(sample);
  AC_CHECK(verdict.overall == ac::perf::ThresholdStatus::kPass);
  AC_CHECK_EQ(verdict.exitCode, 0);
  AC_CHECK(verdict.note.empty());
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) {
    AC_CHECK(verdict.results[i].status == ac::perf::ThresholdStatus::kPass);
  }
  AC_CHECK(verdict.results[0].limit == 30.0);
  AC_CHECK(verdict.results[6].limit == 1.0);
}

AC_TEST(threshold_boundary_values_follow_inclusive_contract) {
  // G1/G7 是严格小于（等于上限即失败），其余是「不大于」。
  ac::perf::PerfSample sample = passingSample();
  sample.metrics.cpuMeanPct = 30.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);
  sample.metrics.cpuMeanPct = 29.999;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kPass);

  sample = passingSample();
  sample.metrics.bytesPerClientMaxKbps = 40.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kPass);
  sample.metrics.bytesPerClientMaxKbps = 40.001;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);

  sample = passingSample();
  sample.metrics.snapshotBytesP95 = 1228.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kPass);
  sample.metrics.snapshotBytesP95 = 1228.5;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);

  sample = passingSample();
  sample.metrics.snapshotBytesMax = 2048.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kPass);
  sample.metrics.snapshotBytesMax = 2048.5;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);

  sample = passingSample();
  sample.metrics.hardCorrectPerMinPerClient = 5.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kPass);
  sample.metrics.hardCorrectPerMinPerClient = 5.25;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);

  sample = passingSample();
  sample.metrics.scheduleErrorP95Ms = 8.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kPass);
  sample.metrics.scheduleErrorP95Ms = 8.5;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);

  sample = passingSample();
  sample.metrics.rssSlopeMbPerMin = 1.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);
  sample.metrics.rssSlopeMbPerMin = 0.999;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kPass);

  // G8 = 0：三个计数里任一个非零都拦人。
  sample = passingSample();
  sample.metrics.eventsDropped = 1u;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);
  sample = passingSample();
  sample.metrics.tickSkips = 3u;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);
  sample = passingSample();
  sample.metrics.uncaughtExceptions = 1u;
  AC_CHECK(ac::perf::evaluateThresholds(sample).overall == ac::perf::ThresholdStatus::kFail);
}

AC_TEST(threshold_unmeasured_id_stays_not_measured_and_passes) {
  ac::perf::PerfSample sample = passingSample();
  sample.isMeasured[static_cast<std::size_t>(ac::perf::ThresholdId::kRssSlope)] = false;
  const ac::perf::PerfVerdict verdict = ac::perf::evaluateThresholds(sample);
  AC_CHECK(verdict.results[6].status == ac::perf::ThresholdStatus::kNotMeasured);
  AC_CHECK(verdict.overall == ac::perf::ThresholdStatus::kPass);
  AC_CHECK_EQ(verdict.exitCode, 0);
}

AC_TEST(threshold_only_unmeasured_yields_not_measured_verdict_with_exit_zero) {
  ac::perf::PerfSample sample{};
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) sample.isMeasured[i] = false;
  const ac::perf::PerfVerdict verdict = ac::perf::evaluateThresholds(sample);
  AC_CHECK(verdict.overall == ac::perf::ThresholdStatus::kNotMeasured);
  AC_CHECK_EQ(verdict.exitCode, 0);
  AC_CHECK(verdict.note.find("不拦人") != std::string::npos);
  // 场景级附加判据（soak 房间未回落等）失败同样把整跑次判红：入口是 PerfSample 的字段。
  ac::perf::PerfSample withScenarioFail{};
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) withScenarioFail.isMeasured[i] = false;
  withScenarioFail.isScenarioFailed = true;
  const ac::perf::PerfVerdict scenarioVerdict = ac::perf::evaluateThresholds(withScenarioFail);
  AC_CHECK(scenarioVerdict.overall == ac::perf::ThresholdStatus::kFail);
  AC_CHECK_EQ(scenarioVerdict.exitCode, 1);
  AC_CHECK(scenarioVerdict.isScenarioFailed);
}

AC_TEST(threshold_break_override_turns_passing_run_red) {
  // §5 反向自检：把 G3 的阈值临时改成 0 → 必须失败且退出码 1。
  const ac::perf::PerfSample sample = passingSample();
  double overrides[ac::perf::kThresholdCount] = {};
  overridesWith(static_cast<std::size_t>(ac::perf::ThresholdId::kSnapshotBytesP95), 0.0, overrides);
  const ac::perf::PerfVerdict verdict = ac::perf::evaluateThresholds(sample, overrides);
  AC_CHECK(verdict.results[2].status == ac::perf::ThresholdStatus::kFail);
  AC_CHECK_EQ(verdict.exitCode, 1);
  AC_CHECK(verdict.overall == ac::perf::ThresholdStatus::kFail);
  AC_CHECK(verdict.results[2].limit == 0.0);
  AC_CHECK(verdict.results[2].measured == 900.0);
}

AC_TEST(threshold_override_can_loosen_limit) {
  ac::perf::PerfSample sample = passingSample();
  sample.metrics.cpuMeanPct = 45.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).exitCode == 1);
  double overrides[ac::perf::kThresholdCount] = {};
  overridesWith(static_cast<std::size_t>(ac::perf::ThresholdId::kCpu), 60.0, overrides);
  const ac::perf::PerfVerdict verdict = ac::perf::evaluateThresholds(sample, overrides);
  AC_CHECK_EQ(verdict.exitCode, 0);
  AC_CHECK(verdict.results[0].limit == 60.0);
}

AC_TEST(threshold_schedule_fallback_uses_third_gap_when_timer_is_coarse) {
  ac::perf::PerfSample sample = passingSample();
  sample.metrics.scheduleErrorP95Ms = 15.0;  // 本机定时器粒度 15 ms：直接判据必然失败
  sample.isScheduleFallbackEnabled = true;
  sample.scheduleHeadTailGapMs = 1.0;
  const ac::perf::PerfVerdict verdict = ac::perf::evaluateThresholds(sample);
  AC_CHECK(verdict.results[5].status == ac::perf::ThresholdStatus::kPass);
  AC_CHECK(verdict.results[5].isScheduleFallbackUsed);
  AC_CHECK(verdict.results[5].measured == 1.0);
  AC_CHECK(verdict.results[5].limit == ac::perf::kScheduleFallbackGapLimitMs);
  AC_CHECK(verdict.note.find("替代判据") != std::string::npos);
  AC_CHECK_EQ(verdict.exitCode, 0);

  // 前后 1/3 差超限 → 替代判据也拦人。
  sample.scheduleHeadTailGapMs = 4.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).exitCode == 1);
  // 漂移超限 → 同样拦人。
  sample.scheduleHeadTailGapMs = 1.0;
  sample.metrics.simDriftMsMax = 80.0;
  AC_CHECK(ac::perf::evaluateThresholds(sample).exitCode == 1);
  // 未启用替代判据时按直接判据失败。
  sample.metrics.simDriftMsMax = 10.0;
  sample.isScheduleFallbackEnabled = false;
  AC_CHECK(ac::perf::evaluateThresholds(sample).exitCode == 1);
}

AC_TEST(threshold_table_pins_frozen_limits_and_units) {
  const ac::perf::ThresholdDef* defs = ac::perf::thresholdDefs();
  AC_CHECK(defs[0].limit == 30.0 && std::string(defs[0].name) == "G1" && defs[0].isStrictlyBelow);
  AC_CHECK(defs[1].limit == 40.0 && std::string(defs[1].unit) == "KB/s");
  AC_CHECK(defs[2].limit == 1228.0 && std::string(defs[2].alias) == "snapshotBytesP95");
  AC_CHECK(defs[3].limit == 2048.0);
  AC_CHECK(defs[4].limit == 5.0);
  AC_CHECK(defs[5].limit == 8.0);
  AC_CHECK(defs[6].limit == 1.0 && defs[6].isStrictlyBelow);
  AC_CHECK(defs[7].limit == 0.0);
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) {
    AC_CHECK(defs[i].name[0] == 'G' && defs[i].name[1] == static_cast<char>('1' + i));
    AC_CHECK(std::string(defs[i].label).empty() == false);
    AC_CHECK(std::string(defs[i].unit).empty() == false);
  }
}

AC_TEST(threshold_lookup_by_id_and_alias) {
  AC_CHECK(ac::perf::findThresholdByName("G3") != nullptr);
  AC_CHECK(ac::perf::findThresholdByName("g3") != nullptr);
  AC_CHECK(ac::perf::findThresholdByName("cpuMeanPct") != nullptr);
  AC_CHECK(ac::perf::findThresholdByName("zeroCounters") != nullptr);
  AC_CHECK(ac::perf::findThresholdByName("G9") == nullptr);
  AC_CHECK(ac::perf::findThresholdByName("") == nullptr);
  AC_CHECK(ac::perf::findThresholdByName("G3")->id == ac::perf::ThresholdId::kSnapshotBytesP95);
}

AC_TEST(threshold_report_json_carries_frozen_fields_and_ids) {
  const ac::perf::PerfSample sample = passingSample();
  const ac::perf::PerfVerdict verdict = ac::perf::evaluateThresholds(sample);
  ac::perf::PerfReportHeader header{};
  header.scenario = "gate-4p2min";
  header.seed = 20260101u;
  header.compiler = "g++ 15.2.0";
  header.host = "Windows 11 x86_64";
  header.startedAt = "2026-09-24T08:00:00Z";
  header.durationSec = 120.0;
  const std::string json = ac::perf::encodePerfReport(header, sample, verdict);
  const char* keys[] = {"\"tool\":\"ac_gate\"",     "\"version\":\"0.1.0\"",
                        "\"scenario\":\"gate-4p2min\"", "\"seed\":20260101",
                        "\"buildType\":\"Release\"",    "\"compiler\":\"g++ 15.2.0\"",
                        "\"host\":",               "\"startedAt\":",
                        "\"durationSec\":120.000", "\"verdict\":\"pass\"",
                        "\"exitCode\":0",          "\"cpuMeanPct\":12.500",
                        "\"cpuP95Pct\":18.000",     "\"bytesPerClientMaxKbps\":31.500",
                        "\"snapshotBytesP95\":900.000", "\"snapshotBytesMax\":1100.000",
                        "\"hardCorrectPerMinPerClient\":0.000",
                        "\"scheduleErrorP95Ms\":6.000", "\"simDriftMsMax\":10.000",
                        "\"rssSlopeMbPerMin\":0.200", "\"eventsDropped\":0",
                        "\"tickSkips\":0",          "\"uncaughtExceptions\":0"};
  for (const char* key : keys) AC_CHECK(json.find(key) != std::string::npos);
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) {
    std::string id = "\"id\":\"G";
    id += static_cast<char>('1' + i);
    id += "\"";
    AC_CHECK(json.find(id) != std::string::npos);
  }
  AC_CHECK(json.find("\"status\":\"pass\"") != std::string::npos);
  AC_CHECK(json.find("\"unit\":\"%\"") != std::string::npos);
  AC_CHECK(json.find("\"limit\":1228.000") != std::string::npos);
  AC_CHECK(json.find("\"label\":") != std::string::npos);
}
