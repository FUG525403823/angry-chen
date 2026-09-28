#include "server/shutdown.hpp"

#include <csignal>
#include <cstdlib>

namespace ac::server {
namespace {

// sig_atomic_t + volatile：处理函数里只写这一个字，读侧在主循环（单线程）。
volatile std::sig_atomic_t gStopRequested = 0;
volatile std::sig_atomic_t gRequestCount = 0;

// 信号处理函数里只做异步信号安全的事：写标志；第二次则 _Exit（不是从处理函数里跑收尾代码）。
void onShutdownSignal(int /*signalNumber*/) {
  if (gStopRequested != 0) std::_Exit(kForcedExitCode);
  gStopRequested = 1;
  gRequestCount = gRequestCount + 1;
}

}  // namespace

void installShutdownHandlers() noexcept {
  (void)std::signal(SIGINT, onShutdownSignal);
  (void)std::signal(SIGTERM, onShutdownSignal);
}

bool isStopRequested() noexcept { return gStopRequested != 0; }

int shutdownRequestCount() noexcept { return static_cast<int>(gRequestCount); }

void requestStop() noexcept {
  if (gStopRequested != 0) return;
  gStopRequested = 1;
  gRequestCount = gRequestCount + 1;
}

void clearStopRequest() noexcept {
  gStopRequested = 0;
  gRequestCount = 0;
}

}  // namespace ac::server
