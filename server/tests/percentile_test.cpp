// S15 §15.4 D2：三处分位实现合并到 core/percentile.hpp 后，判定口径必须逐位不变。
// 本文件把「合并前」的三份公式原样抄下来，对 1..4096 个样本逐一比对索引，作为口径冻结的证据。
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

#include "core/percentile.hpp"
#include "tiny_test.hpp"

namespace {

// 合并前 ac_gate 的 percentileOf 取值：sorted[ceil(q·N)-1]，空输入 0。
std::size_t legacyGateIndex(std::size_t count, double q) {
  if (count == 0u) return 0u;
  std::size_t rank = static_cast<std::size_t>(std::ceil(q * static_cast<double>(count)));
  if (rank == 0u) rank = 1u;
  if (rank > count) rank = count;
  return rank - 1u;
}

// 合并前 ac_bench 的 percentile 取值：整数百分点写法的同一口径。
std::size_t legacyBenchIndex(std::size_t count, double q) {
  if (count == 0u) return 0u;
  std::size_t rank = (count * static_cast<std::size_t>(q * 100.0) + 99u) / 100u;
  if (rank == 0u) rank = 1u;
  if (rank > count) rank = count;
  return rank - 1u;
}

// 合并前 core::TickScheduler 的 percentileIndex 取值：round((N-1)·q)，右端夹取。
std::size_t legacySchedulerIndex(std::size_t count, double q) {
  if (count == 0u) return 0u;
  const double position = static_cast<double>(count - 1u) * q;
  std::size_t index = static_cast<std::size_t>(position + 0.5);
  if (index >= count) index = count - 1u;
  return index;
}

constexpr double kProbeFractions[] = {0.0, 0.25, 0.5, 0.75, 0.95, 0.99, 1.0};

}  // namespace

AC_TEST(percentile_upper_rule_matches_legacy_tool_formulas) {
  std::size_t mismatches = 0u;
  for (std::size_t count = 1u; count <= 4096u; ++count) {
    for (const double q : kProbeFractions) {
      const std::size_t merged = ac::core::percentileIndex(
          count, q, ac::core::PercentileRule::kNearestRankUpper);
      if (merged != legacyGateIndex(count, q) || merged != legacyBenchIndex(count, q)) ++mismatches;
    }
  }
  std::printf("percentileUpper mismatches=%zu\n", mismatches);
  AC_CHECK_EQ(mismatches, 0u);
}

AC_TEST(percentile_midpoint_rule_matches_legacy_scheduler_formula) {
  std::size_t mismatches = 0u;
  for (std::size_t count = 1u; count <= 4096u; ++count) {
    for (const double q : kProbeFractions) {
      if (ac::core::percentileIndex(count, q, ac::core::PercentileRule::kMidpointRank) !=
          legacySchedulerIndex(count, q)) {
        ++mismatches;
      }
    }
  }
  std::printf("percentileMidpoint mismatches=%zu\n", mismatches);
  AC_CHECK_EQ(mismatches, 0u);
}

// 两规则确实是「真差异」（不是重复实现）：q·N 落在整数点上时取侧的规则不同。
AC_TEST(percentile_rules_differ_only_on_integer_ranks) {
  // N=2、q=0.5：上秩口径取第 1 个样本，取样环口径取第 2 个（round(0.5+0.5)）。
  AC_CHECK_EQ(ac::core::percentileIndex(2u, 0.5, ac::core::PercentileRule::kNearestRankUpper), 0u);
  AC_CHECK_EQ(ac::core::percentileIndex(2u, 0.5, ac::core::PercentileRule::kMidpointRank), 1u);
  // N=100、q=0.5 是取样环容量（64）之外，仅作为口径说明的边界样本。
  AC_CHECK_EQ(ac::core::percentileIndex(100u, 0.5, ac::core::PercentileRule::kNearestRankUpper),
              49u);
  AC_CHECK_EQ(ac::core::percentileIndex(100u, 0.5, ac::core::PercentileRule::kMidpointRank), 50u);
  // P95 在 N=64、N=20 这两个实际取样规模上两者一致（三处回归因此不受影响）。
  for (const std::size_t count : {20u, 64u, 600u}) {
    AC_CHECK_EQ(ac::core::percentileIndex(count, 0.95, ac::core::PercentileRule::kNearestRankUpper),
                ac::core::percentileIndex(count, 0.95, ac::core::PercentileRule::kMidpointRank));
  }
}

AC_TEST(percentile_of_sorted_handles_edges_and_empty_input) {
  const double sorted[] = {1.0, 2.0, 3.0, 4.0};
  AC_CHECK_NEAR(ac::core::percentileOfSorted(sorted, 4u, 0.5,
                                             ac::core::PercentileRule::kNearestRankUpper),
                2.0, 1e-12);
  AC_CHECK_NEAR(ac::core::percentileOfSorted(sorted, 1u, 0.95,
                                             ac::core::PercentileRule::kNearestRankUpper),
                1.0, 1e-12);
  AC_CHECK_NEAR(ac::core::percentileOfSorted(sorted, 4u, 1.0,
                                             ac::core::PercentileRule::kMidpointRank),
                4.0, 1e-12);
  AC_CHECK_NEAR(ac::core::percentileOfSorted(nullptr, 0u, 0.95,
                                             ac::core::PercentileRule::kNearestRankUpper),
                0.0, 1e-12);
  AC_CHECK_NEAR(ac::core::percentileOfSorted(sorted, 0u, 0.5,
                                             ac::core::PercentileRule::kMidpointRank),
                0.0, 1e-12);
  // 头部并列值：上秩口径在 N=2 时取第二个样本。
  const double ties[] = {7.0, 7.0};
  AC_CHECK_NEAR(ac::core::percentileOfSorted(ties, 2u, 0.95,
                                             ac::core::PercentileRule::kNearestRankUpper),
                7.0, 1e-12);
}
