// ServerProcess 入口（S01）。本份只实现版本/帮助/自检日志三件事，网络与模拟由后续计划接入。
#include "core/log.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace {

constexpr const char* kVersion = "0.1.0";
constexpr int kProtocolVersion = 1;
constexpr int kTickMs = 50;

void printUsage(const char* argv0) {
  std::printf("usage: %s [--version|--help|--selftest-log]\n", argv0);
  std::printf("  --version       打印版本行后退出\n");
  std::printf("  --help          打印本帮助后退出\n");
  std::printf("  --selftest-log  向 stdout 写一条结构化启动日志后退出\n");
  std::printf("  无参数          向 stderr 写一条结构化启动日志后退出\n");
}

void writeStartupLog() {
  ac::log::write(ac::log::Level::info, "serverStarted",
                 {{"version", kVersion}, {"protocol", kProtocolVersion}, {"tickMs", kTickMs}});
}

}  // namespace

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--version") {
      std::printf("ac_server %s protocol=%d tick=%dms\n", kVersion, kProtocolVersion, kTickMs);
      return 0;
    }
    if (arg == "--help") {
      printUsage(argv[0]);
      return 0;
    }
    if (arg == "--selftest-log") {
      ac::log::useStdout();
      writeStartupLog();
      ac::log::close();
      return 0;
    }
    std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
    printUsage(argv[0]);
    return 2;
  }

  writeStartupLog();
  return 0;
}
