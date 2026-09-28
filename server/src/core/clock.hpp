#pragma once
// S15 §15.4 D4：时钟读数的唯一实现（原先 main / gate / bot / runtime 各有一份 nowMs 拷贝）。
//
// 两个钟分工明确（冻结依据：日志 §5 的 ts 字段是 UTC 墙上钟，其余测量一律单调钟）：
//   nowMs()  —— steady_clock，单调毫秒；超时、采样窗口、调度记账用它，不受系统时间调整影响；
//   wallMs() —— system_clock，UTC epoch 毫秒；只给日志时间戳用。
#include <chrono>
#include <cstdint>

namespace ac::core {

inline std::uint64_t nowMs() noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

inline std::int64_t wallMs() noexcept {
  return static_cast<std::int64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count());
}

}  // namespace ac::core
