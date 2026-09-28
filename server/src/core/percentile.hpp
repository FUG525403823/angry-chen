#pragma once
// S15 §15.4 D2：分位口径的唯一实现（调度器取样环 / ac_gate / ac_bench 三处合并到这一份）。
// 纯 header、无堆分配：排序与缓冲仍归调用方（取样环是固定数组，工具是自己的 vector），
// 这里只固化「升序数组 → 分位取值」这一步。
#include <cmath>
#include <cstddef>

namespace ac::core {

// 两种边界口径都是**最近秩**（nearest-rank，不做线性插值），差异只在 q×N 恰为整数时取哪一侧：
//   kNearestRankUpper = ceil(q×N) - 1（q×N 为整数时取**上**秩）—— ac_gate / ac_bench 自始至今的口径；
//   kMidpointRank     = round((N-1)×q)（偶数样本的中位数取**下**侧）—— core::TickScheduler 取样环的口径。
// 两者对 N ≤ 64 的大多数取值相同，但在 q=0.5 且 N 为偶数（如 N=2 ⇒ 索引 0 vs 1）等整数点上分岔，
// 所以合并实现时必须参数化：§15.4 D2 要求三处判定逐位不变。
enum class PercentileRule {
  kNearestRankUpper,
  kMidpointRank,
};

// 前置条件：count > 0；count == 0 时返回 0（调用方按空输入各自处理，见 percentileOfSorted）。
inline std::size_t percentileIndex(std::size_t count, double fraction,
                                   PercentileRule rule) noexcept {
  if (count == 0u) return 0u;
  if (rule == PercentileRule::kMidpointRank) {
    const double position = static_cast<double>(count - 1u) * fraction;
    std::size_t index = static_cast<std::size_t>(position + 0.5);
    if (index >= count) index = count - 1u;
    return index;
  }
  std::size_t rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(count)));
  if (rank == 0u) rank = 1u;
  if (rank > count) rank = count;
  return rank - 1u;
}

// sorted 必须已升序；空输入返回 0.0（三处旧实现的空输入行为一致）。
inline double percentileOfSorted(const double* sorted, std::size_t count, double fraction,
                                 PercentileRule rule) noexcept {
  if (count == 0u || sorted == nullptr) return 0.0;
  return sorted[percentileIndex(count, fraction, rule)];
}

}  // namespace ac::core
