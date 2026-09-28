#pragma once
// S15 §15.4 D3：进程级停止请求 —— SIGINT/SIGTERM → 优雅退出（usage 里承诺的 Ctrl+C 收尾）。
//
// 收尾动作由 runServe 完成（停止 accept/tick 循环、runtime.stop() 释放端口与房间、日志落盘），
// 本模块只负责「把信号翻译成可观察的停止标志」。该标志同时是对外可注入的缝：
// 用例不真发信号也能驱动同一段收尾逻辑（见 tests/serve_cli_test.cpp）。
namespace ac::server {

// 第二个信号不再等待收尾（操作者已经等过一轮），直接 _Exit。
inline constexpr int kForcedExitCode = 130;

// 安装 SIGINT/SIGTERM 处理。幂等：重复调用不会叠加处理函数。
void installShutdownHandlers() noexcept;
// 是否已收到停止请求（runServe 的 accept/tick 循环每个轮次检查一次）。
bool isStopRequested() noexcept;
// 已记录的停止请求次数（信号 + 注入）。
int shutdownRequestCount() noexcept;
// 注入一次停止请求（生产路径由信号处理函数调用）。
void requestStop() noexcept;
// 复位（用例清理用；生产进程不需要）。
void clearStopRequest() noexcept;

}  // namespace ac::server
