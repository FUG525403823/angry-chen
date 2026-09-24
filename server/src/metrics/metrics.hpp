#pragma once
// S13 §5「/metrics 指标名清单」：Prometheus 文本渲染（text/plain; version=0.0.4）。
//
// 名字清单即契约：本文件的名字表既驱动渲染，也驱动断言（metricName / isMetricNameRegistered）。
// 名字只有两个来源——定长注册表（counters/gauges，名字在 S11/S12/S13 各自的表里）与进程即时值
// （ProcessSnapshot：房间/连接/人数这类运行状态）。新增名字必须同提交改 S13 §5 的表与断言。
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/version.hpp"
#include "metrics/counters.hpp"
#include "metrics/gauges.hpp"

namespace ac::metrics {

// S13 §5 的 45 个名字 + S12 §13.1-2 移交的 ac_tick_skips_total = 46 行（见 README §14.1）。
inline constexpr std::size_t kMetricNameCount = 46u;
inline constexpr std::string_view kMetricsContentType = "text/plain; version=0.0.4";

// 不来自注册表的即时值：它们既不是只增计数，也不是采样量值。
struct ProcessSnapshot {
  std::string_view version = ac::version::kVersion;  // 与 --version 行同源（core/version.hpp）
  std::uint32_t protocol = ac::version::kProtocol;
  std::uint32_t tickMs = ac::version::kTickMs;
  std::uint32_t rooms = 0u;
  std::uint32_t connections = 0u;
  std::uint32_t players = 0u;
  std::uint32_t graceActive = 0u;
  std::uint64_t recordsRetained = 0u;  // 战绩常驻条数（MatchStore::recordCount）
  std::uint64_t uptimeSeconds = 0u;
};

struct MetricsInput {
  const CounterRegistry* counters = nullptr;  // nullptr = 全部按 0 渲染
  const GaugeRegistry* gauges = nullptr;
  ProcessSnapshot process{};
};

enum class MetricKind : std::uint8_t { kCounter = 0u, kGauge = 1u };

// 每个名字渲染三行：HELP、TYPE、值（计数为整数，量值保留 3 位小数）。
std::string renderMetrics(const MetricsInput& input);

// §5 清单即契约：本表是渲染与断言的唯一来源。
std::size_t metricNameCount() noexcept;
std::string_view metricName(std::size_t index) noexcept;
MetricKind metricKind(std::size_t index) noexcept;
bool isMetricNameRegistered(std::string_view name) noexcept;

}  // namespace ac::metrics
