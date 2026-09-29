// ServerProcess 入口（S01）。S13 接上版本同源、日志阈值、指标渲染与战绩存储自检；
// 网络监听与房间循环由后续批次（S14/S15）接线。
#include "core/clock.hpp"
#include "core/log.hpp"
#include "core/version.hpp"
#include "metrics/metrics.hpp"
#include "persist/match_store.hpp"
#include "server/runtime.hpp"
#include "server/serve_cli.hpp"
#include "server/shutdown.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace {

void printUsage(const char* argv0) {
  std::printf("usage: %s [--version|--help|--selftest-log|--selftest-metrics|--selftest-store]\n", argv0);
  std::printf("  --version         打印版本行后退出\n");
  std::printf("  --help            打印本帮助后退出\n");
  std::printf("  --selftest-log    向 stdout 写一条结构化启动日志后退出\n");
  std::printf("  --selftest-metrics 向 stdout 渲染一遍 /metrics 文本后退出\n");
  std::printf("  --selftest-store  打开 AC_DATA_DIR 下的战绩存储并打印常驻/坏行计数与两条存储指标后退出\n");
  std::printf("  --serve           启动服务器运行时（UDP 游戏面 + HTTP 诊断面），直到 --minutes 到时或 SIGINT/SIGTERM\n");
  std::printf("                    选项 --minutes= --udp-port= --http-port= --data-dir= --seed= --log-level=（顺序无关，--k=v 与 --k v 等价）\n");
  std::printf("                    开关 --auto-ready：连上即 ready 并开局（装载/门禁/压测用；ADR-013）\n");
  std::printf("                    环境变量 AC_UDP_PORT / AC_HTTP_PORT / AC_DATA_DIR / AC_LOG_LEVEL；同名的 CLI 选项优先\n");
  std::printf("  无参数            向 stderr 写一条结构化启动日志后退出\n");
}

void writeStartupLog() {
  // 生产启动行也走 §5 的事件形状（S13 起两条写口统一到 event，旧 write() 只留给 S01 的用例）。
  // `serverStarted` 是 S01 的名字、不在 §5 的 19 名最小集里，所以走 string_view 重载（C3 决策）。
  ac::log::event(ac::log::Level::info, "serverStarted", {},
                 {ac::log::DetailField("version", std::string_view(ac::version::kVersion)),
                  ac::log::DetailField("protocol", ac::version::kProtocol),
                  ac::log::DetailField("tickMs", ac::version::kTickMs)});
}

// S13 §6-3 的等价证据（无需监听套接字）：把名字表的每一行整段渲染出来（45 个 §5 名字 + 1 条 S12 移交）。
int runSelftestMetrics() {
  const ac::metrics::CounterRegistry counters{};
  const ac::metrics::GaugeRegistry gauges{};
  const std::string text = ac::metrics::renderMetrics({&counters, &gauges, {}});
  std::fwrite(text.data(), 1u, text.size(), stdout);
  return 0;
}

int runSelftestStore() {
  const std::string dataDir = ac::persist::dataDirFromEnv();
  std::string error;
  const std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dataDir, &error);
  if (store == nullptr) {
    ac::log::event(ac::log::Level::error, ac::log::EventName::kStoreError, {},
                   {ac::log::DetailField("dir", dataDir), ac::log::DetailField("error", error)});
    std::fprintf(stderr, "store open failed: %s\n", error.c_str());
    return 1;
  }
  // 存储自检顺便把与存储有关的两条指标渲染出来：坏行计数由加载过程产生（persist 本身不依赖 metrics，
  // 由调用方写进注册表），常驻条数走 ProcessSnapshot 字段。
  ac::metrics::CounterRegistry counters{};
  ac::metrics::GaugeRegistry gauges{};
  ac::metrics::addCounter(counters, ac::metrics::CounterId::kCorruptLines, store->stats().corruptLines);
  ac::metrics::ProcessSnapshot process{};
  process.recordsRetained = store->recordCount();
  std::printf("dataDir=%.*s path=%.*s records=%zu corrupt=%zu\n",
              static_cast<int>(store->dataDir().size()), store->dataDir().data(),
              static_cast<int>(store->path().size()), store->path().data(), store->recordCount(),
              store->stats().corruptLines);
  const std::string body = ac::metrics::renderMetrics({&counters, &gauges, process});
  std::size_t at = 0u;
  while (at < body.size()) {
    const std::size_t end = body.find('\n', at);
    const std::string_view line(body.data() + at, (end == std::string::npos ? body.size() : end) - at);
    if (line.rfind("ac_corrupt_lines_total", 0u) == 0u || line.rfind("ac_records_retained", 0u) == 0u) {
      std::printf("%.*s\n", static_cast<int>(line.size()), line.data());
    }
    if (end == std::string::npos) break;
    at = end + 1u;
  }
  return 0;
}

std::uint16_t portFromEnv(const char* name, std::uint16_t fallback) {
  const char* raw = std::getenv(name);
  if (raw == nullptr || *raw == '\0') return fallback;
  const int value = std::atoi(raw);
  return value > 0 && value < 65536 ? static_cast<std::uint16_t>(value) : fallback;
}

// S14 §2-3 / S15 §5：常驻服务模式。选项解析搬到 ac::server::parseServeArgs（顺序无关，见 D3），
// 停止走 SIGINT/SIGTERM → 停止标志 → 本循环收尾（退出码 0；第二个信号强杀）。
int runServe(int argc, char** argv) {
  ac::server::ServeOptions options{};
  options.config.udpPort = portFromEnv("AC_UDP_PORT", options.config.udpPort);
  options.config.httpPort = portFromEnv("AC_HTTP_PORT", options.config.httpPort);
  std::string parseError;
  if (!ac::server::parseServeArgs(argc, argv, options, &parseError)) {
    if (options.isHelp) {
      printUsage(argv[0]);
      return 0;
    }
    std::fprintf(stderr, "serve failed: %s\n", parseError.c_str());
    printUsage(argv[0]);
    return 2;
  }
  // CLI 的 --log-level 在 AC_LOG_LEVEL（main 开头已应用）之后生效，即 CLI 优先。
  if (options.isLogLevelSet) ac::log::setMinLevel(options.logLevel);
  ac::server::installShutdownHandlers();

  ac::server::Runtime runtime;
  std::string error;
  if (!runtime.start(options.config, &error)) {
    std::fprintf(stderr, "serve failed: %s\n", error.c_str());
    return 1;
  }
  ac::log::event(ac::log::Level::info, ac::log::EventName::kListening, {},
                 {ac::log::DetailField("udpPort", static_cast<std::uint32_t>(runtime.udpPort())),
                  ac::log::DetailField("httpPort", static_cast<std::uint32_t>(runtime.httpPort())),
                  ac::log::DetailField("dataDir", std::string_view(runtime.dataDir()))});
  const std::uint64_t startMs = ac::core::nowMs();
  const std::uint64_t endMs =
      options.minutes > 0.0 ? startMs + static_cast<std::uint64_t>(options.minutes * 60000.0) : 0u;
  while (!ac::server::isStopRequested() && (endMs == 0u || ac::core::nowMs() < endMs)) {
    runtime.pollOnce(ac::core::nowMs());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ac::log::event(ac::log::Level::info, ac::log::EventName::kShutdownRequested, {},
                 {ac::log::DetailField("reason", std::string_view(
                                                     ac::server::isStopRequested() ? "signal" : "minutes")),
                  ac::log::DetailField("requests", ac::server::shutdownRequestCount())});
  runtime.stop();  // 停止 accept/tick、释放 UDP/HTTP 端口与房间、store 随 Runtime 析构落盘
  ac::log::event(ac::log::Level::info, ac::log::EventName::kShutdownComplete, {},
                 {ac::log::DetailField("ticks", runtime.metrics().ticks)});
  ac::log::close();  // 日志 sink 落盘（文件 sink 时是唯一能保证 defer 到进程结束的一步）
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    ac::log::applyLogLevelFromEnv();
    ac::log::applyLogFileFromEnv();
    // D3：先扫一遍 argv 找 --serve，再分派 —— `ac_server --http-port 8799 --serve` 与
    // `ac_server --serve --http-port 8799` 等价（旧实现要求 --serve 必须打头）。
    for (int i = 1; i < argc; ++i) {
      if (std::string_view(argv[i]) == "--serve") return runServe(argc, argv);
    }
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg = argv[i];
      if (arg == "--version") {
        std::printf("%s\n", ac::version::versionLine().c_str());
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
      if (arg == "--selftest-metrics") return runSelftestMetrics();
      if (arg == "--selftest-store") return runSelftestStore();
      std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
      printUsage(argv[0]);
      return 2;
    }

    writeStartupLog();
    return 0;
  } catch (const std::exception& error) {
    // §5「频率纪律」：未捕获异常必须落 error.uncaught，且进程仍能正常收尾。
    ac::log::reportUncaught(error.what());
    return 1;
  } catch (...) {
    ac::log::reportUncaught("non-standard exception");
    return 1;
  }
}
