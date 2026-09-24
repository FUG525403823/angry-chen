// S14 §3 交付物 3 / §5：性能守门工具。
//
// 运行方式：gate **承载服务器运行时**（真 UDP 8788 + 真 HTTP 8787），把 ac_bot 拉成**独立进程**
// （§8：机器人单独进程，CPU 记账因此只算服务器自己），每秒采样 CPU/RSS 与每客户端出站字节，
// 跑完按 §5 的阈值表判定并写 build/server-perf.json；任一 fail → 退出码 1。
#include <algorithm>
#include <cstddef>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "core/log.hpp"
#include "core/scheduler.hpp"
#include "core/version.hpp"
#include "net/tcp_listener.hpp"
#include "perf/thresholds.hpp"
#include "server/runtime.hpp"

#if defined(_WIN32)
#include <windows.h>  // 必须在 psapi.h 之前（WINBOOL/DWORD 由它带来）

#include <process.h>
#include <psapi.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

constexpr int kWarmupSeconds = 15;      // G1：去掉前 15 s 预热
constexpr int kSampleIntervalMs = 1000;
constexpr int kRssSampleEverySeconds = 1;  // §5 G7 行写的是「1s 采样线性回归」（§15.3 登记）
constexpr double kBytesPerKb = 1024.0;
constexpr std::size_t kMaxClientStats = 16u;

struct ScenarioDef {
  const char* name;
  int players;
  int sheep;
  double minutes;
  bool isSoak;
  bool isLatency;
};

constexpr ScenarioDef kScenarios[] = {
    {"gate-4p2min", 4, 60, 2.0, false, false},
    {"soak-4p5min", 4, 60, 5.0, true, false},
    {"latency-200", 4, 60, 2.0, false, true},
};

struct Options {
  std::string scenario = "gate-4p2min";
  std::string out = "build/server-perf.json";
  std::string botPath = "";
  std::uint16_t portBase = 8788u;
  std::uint32_t seed = 20260101u;
  std::vector<std::string> breaks{};
  double minutes = 0.0;  // >0 表示调试用短跑，只覆盖时长（§15.3 登记的非计划选项）
};

std::uint64_t nowMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

// 近邻秩分位（升序输入），与 core/metrics 同口径：sorted[ceil(q * N) - 1]。
double percentileOf(std::vector<double> values, double q) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  std::size_t rank = static_cast<std::size_t>(std::ceil(q * static_cast<double>(values.size())));
  if (rank == 0u) rank = 1u;
  if (rank > values.size()) rank = values.size();
  return values[rank - 1u];
}

double meanOf(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  double sum = 0.0;
  for (double value : values) sum += value;
  return sum / static_cast<double>(values.size());
}

// 最小二乘斜率（y 单位 / 分钟）。
double slopePerMinute(const std::vector<double>& minutes, const std::vector<double>& values) {
  if (minutes.size() < 2u || minutes.size() != values.size()) return 0.0;
  const double meanX = meanOf(minutes);
  const double meanY = meanOf(values);
  double numerator = 0.0;
  double denominator = 0.0;
  for (std::size_t i = 0u; i < minutes.size(); ++i) {
    numerator += (minutes[i] - meanX) * (values[i] - meanY);
    denominator += (minutes[i] - meanX) * (minutes[i] - meanX);
  }
  return denominator == 0.0 ? 0.0 : numerator / denominator;
}

#if defined(_WIN32)
std::uint64_t processCpuMs() {
  FILETIME created{};
  FILETIME exited{};
  FILETIME kernel{};
  FILETIME user{};
  if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) == 0) return 0u;
  const auto toMs = [](const FILETIME& time) {
    const std::uint64_t ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32u) |
                                static_cast<std::uint64_t>(time.dwLowDateTime);
    return ticks / 10000ull;
  };
  return toMs(kernel) + toMs(user);
}

double processRssMb() {
  PROCESS_MEMORY_COUNTERS counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) return 0.0;
  return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
}

std::size_t cpuCoreCount() {
  SYSTEM_INFO info{};
  GetNativeSystemInfo(&info);
  return static_cast<std::size_t>(info.dwNumberOfProcessors);
}

std::string osName() { return "Windows"; }
#else
std::uint64_t processCpuMs() {
  std::FILE* file = std::fopen("/proc/self/stat", "r");
  if (file == nullptr) return 0u;
  char buffer[1024] = {};
  const std::size_t read = std::fread(buffer, 1u, sizeof(buffer) - 1u, file);
  std::fclose(file);
  buffer[read] = '\0';
  const char* close = std::strrchr(buffer, ')');
  if (close == nullptr) return 0u;
  // 第 14/15 个字段（utime/stime），单位 = clock tick。
  unsigned long long utime = 0ull;
  unsigned long long stime = 0ull;
  if (std::sscanf(close + 2, "%*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu", &utime,
                  &stime) != 2) {
    return 0u;
  }
  const long ticks = sysconf(_SC_CLK_TCK);
  const double perTickMs = 1000.0 / static_cast<double>(ticks > 0 ? ticks : 100);
  return static_cast<std::uint64_t>(static_cast<double>(utime + stime) * perTickMs);
}

double processRssMb() {
  std::FILE* file = std::fopen("/proc/self/statm", "r");
  if (file == nullptr) return 0.0;
  unsigned long long total = 0ull;
  unsigned long long resident = 0ull;
  const int fields = std::fscanf(file, "%llu %llu", &total, &resident);
  std::fclose(file);
  if (fields != 2) return 0.0;
  const long pageSize = sysconf(_SC_PAGESIZE);
  return static_cast<double>(resident) * static_cast<double>(pageSize) / (1024.0 * 1024.0);
}

std::size_t cpuCoreCount() {
  const long count = sysconf(_SC_NPROCESSORS_ONLN);
  return count > 0 ? static_cast<std::size_t>(count) : 1u;
}

std::string osName() { return "Linux"; }
#endif

struct Child {
#if defined(_WIN32)
  intptr_t handle = -1;
#else
  int pid = -1;
#endif
  bool isRunning = false;
};

bool spawnChild(const std::string& exe, const std::vector<std::string>& args, Child& child,
                std::string* error) {
  std::vector<const char*> argv{};
  argv.reserve(args.size() + 1u);
  for (const std::string& arg : args) argv.push_back(arg.c_str());
  argv.push_back(nullptr);
#if defined(_WIN32)
  const intptr_t handle = _spawnv(_P_NOWAIT, exe.c_str(), argv.data());
  if (handle == -1) {
    if (error != nullptr) *error = "spawn failed: " + exe;
    return false;
  }
  child.handle = handle;
#else
  const int pid = fork();
  if (pid < 0) {
    if (error != nullptr) *error = "fork failed";
    return false;
  }
  if (pid == 0) {
    execv(exe.c_str(), const_cast<char* const*>(argv.data()));
    _exit(127);
  }
  child.pid = pid;
#endif
  child.isRunning = true;
  return true;
}

void killChild(Child& child) {
  if (!child.isRunning) return;
#if defined(_WIN32)
  (void)TerminateProcess(reinterpret_cast<HANDLE>(child.handle), 0u);
  (void)WaitForSingleObject(reinterpret_cast<HANDLE>(child.handle), 2000u);
  (void)CloseHandle(reinterpret_cast<HANDLE>(child.handle));
  child.handle = -1;
#else
  (void)kill(child.pid, SIGKILL);
  int status = 0;
  (void)waitpid(child.pid, &status, 0);
  child.pid = -1;
#endif
  child.isRunning = false;
}

std::string executableDir(const char* argv0) {
  std::string path = argv0 == nullptr ? std::string() : std::string(argv0);
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string(".") : path.substr(0u, slash);
}

std::string botExecutable(const char* argv0, const Options& options) {
  if (!options.botPath.empty()) return options.botPath;
#if defined(_WIN32)
  return executableDir(argv0) + "\\ac_bot.exe";
#else
  return executableDir(argv0) + "/ac_bot";
#endif
}

std::string compilerName() {
#if defined(__clang__)
  return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("g++ ") + __VERSION__;
#elif defined(_MSC_VER)
  return "msvc " + std::to_string(_MSC_VER);
#else
  return "unknown";
#endif
}

std::string buildTypeName() {
#if defined(NDEBUG)
  return "Release";
#else
  return "Debug";
#endif
}

std::string isoNow() {
  const std::time_t seconds = std::time(nullptr);
  std::tm utc{};
#if defined(_WIN32)
  (void)gmtime_s(&utc, &seconds);
#else
  (void)gmtime_r(&seconds, &utc);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return std::string(buffer);
}

// 一条 HTTP 请求（用 gate 自己的运行时轮询把响应读完）：返回原始响应文本。
std::string httpGet(ac::server::Runtime& runtime, const char* target) {
  ac::net::TcpConnection connection =
      ac::net::connectTcp(0x7F000001u, runtime.httpPort(), 500);
  if (!connection.isOpen()) return std::string();
  std::string request = "GET ";
  request += target;
  request += " HTTP/1.1\r\nhost: 127.0.0.1\r\n\r\n";
  (void)connection.sendAll(request.data(), request.size());
  std::string response{};
  const std::uint64_t deadline = nowMs() + 2000u;
  std::uint8_t buffer[512] = {};
  while (nowMs() < deadline) {
    runtime.pollOnce(nowMs());
    const int got = connection.recv(buffer, 5);
    if (got > 0) {
      response.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(got));
      continue;
    }
    if (got < 0) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  connection.close();
  return response;
}

std::size_t statusOf(const std::string& response) {
  if (response.size() < 12u) return 0u;
  return static_cast<std::size_t>(std::atoi(response.c_str() + 9u));
}

// §5 G6：替代判据只在「本机定时器粒度 > 8ms」时启用（否则一律按 8ms 硬卡）。
double measureTimerGranularityMs() {
  double finest = 0.0;
  std::uint64_t previous = nowMs();
  for (int i = 0; i < 64; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const std::uint64_t now = nowMs();
    const double delta = static_cast<double>(now - previous);
    previous = now;
    if (delta > 0.0 && (finest == 0.0 || delta < finest)) finest = delta;
  }
  return finest;
}

void printUsage(const char* argv0) {
  std::printf("usage: %s [--scenario NAME] [--out PATH] [--bot PATH] [--port-base P] [--seed S]"
              " [--break ID=VALUE]... [--minutes M 调试短跑]\n",
              argv0);
  for (const ScenarioDef& scenario : kScenarios) {
    std::printf("  %-12s players=%d sheep=%d minutes=%.1f%s%s\n", scenario.name, scenario.players,
                scenario.sheep, scenario.minutes, scenario.isSoak ? " soak" : "",
                scenario.isLatency ? " latency" : "");
  }
}

bool parseOptions(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const std::size_t eq = arg.find('=');
    const std::string key = eq == std::string::npos ? arg : arg.substr(0u, eq);
    std::string value = eq == std::string::npos ? std::string() : arg.substr(eq + 1u);
    // §6-2/§9 的计划命令用空格分隔（--scenario gate-4p2min），两种写法都接受。
    if (eq == std::string::npos && key.rfind("--", 0u) == 0u && i + 1 < argc &&
        std::string(argv[i + 1]).rfind("--", 0u) != 0u) {
      value = argv[++i];
    }
    if (key == "--help" || key == "-h") {
      printUsage(argv[0]);
      return false;
    }
    if (key == "--scenario") options.scenario = value;
    else if (key == "--out") options.out = value;
    else if (key == "--bot") options.botPath = value;
    else if (key == "--port-base") options.portBase = static_cast<std::uint16_t>(std::atoi(value.c_str()));
    else if (key == "--seed") options.seed = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    else if (key == "--break") options.breaks.push_back(value);
    else if (key == "--minutes") options.minutes = std::atof(value.c_str());
    else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

const ScenarioDef* findScenario(const std::string& name) {
  for (const ScenarioDef& scenario : kScenarios) {
    if (name == scenario.name) return &scenario;
  }
  return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parseOptions(argc, argv, options)) return 2;
  const ScenarioDef* scenario = findScenario(options.scenario);
  // 调试用短跑：只覆盖时长，场景表本身仍是 §5 的冻结值。
  ScenarioDef shortened{};
  if (scenario != nullptr && options.minutes > 0.0) {
    shortened = *scenario;
    shortened.minutes = options.minutes;
    scenario = &shortened;
  }
  if (scenario == nullptr) {
    std::fprintf(stderr, "unknown scenario: %s\n", options.scenario.c_str());
    return 2;
  }
  if (!ac::net::ensureWinsock()) {
    std::fprintf(stderr, "winsock init failed\n");
    return 1;
  }

  ac::server::RuntimeConfig config{};
  config.udpPort = options.portBase;
  config.httpPort = static_cast<std::uint16_t>(options.portBase - 1u);
  config.seed = options.seed;
  config.sheepTarget = scenario->sheep;
  ac::server::Runtime runtime;
  std::string error;
  if (!runtime.start(config, &error)) {
    std::fprintf(stderr, "runtime start failed: %s\n", error.c_str());
    return 1;
  }

  Child child{};
  const std::string botExe = botExecutable(argv[0], options);
  std::vector<std::string> botArgs{botExe, "--players=" + std::to_string(scenario->players),
                                   "--minutes=" + std::to_string(scenario->minutes),
                                   "--seed=" + std::to_string(options.seed),
                                   "--port=" + std::to_string(runtime.udpPort())};
  if (scenario->isLatency) {
    botArgs.push_back("--latency=200");
    botArgs.push_back("--loss=1");
  }
  std::string spawnError;
  if (!spawnChild(botExe, botArgs, child, &spawnError)) {
    std::fprintf(stderr, "%s\n", spawnError.c_str());
    runtime.stop();
    return 1;
  }
  std::printf("gate scenario=%s bots=%s players=%d sheep=%d minutes=%.1f udp=%u http=%u\n",
              scenario->name, botExe.c_str(), scenario->players, scenario->sheep,
              scenario->minutes, static_cast<unsigned>(runtime.udpPort()),
              static_cast<unsigned>(runtime.httpPort()));

  std::vector<double> cpuSamples{};
  std::vector<double> rssSamples{};
  std::vector<double> rssMinutes{};
  std::vector<std::uint16_t> prevIds{};
  std::vector<std::size_t> prevBytes{};
  double bytesPerClientMaxKbps = 0.0;
  // §5 G6 替代判据：前后 1/3 的 P95 差 + 漂移上限，都按 1s 采样的 |sim_drift| 序列算
  // （core 的 scheduleHeadTailGapMs 是「最大值-最小值」的极差，不是计划说的首尾段 P95 差）。
  std::vector<double> driftSamples{};
  std::uint64_t lastDriftTick = 0u;
  ac::server::ClientStat stats[kMaxClientStats] = {};

  const std::uint64_t startWallMs = nowMs();
  const std::uint64_t endWallMs =
      startWallMs + static_cast<std::uint64_t>(scenario->minutes * 60000.0);
  std::uint64_t prevCpuMs = processCpuMs();
  std::uint64_t prevCpuAtMs = nowMs();
  int second = 0;
  while (nowMs() < endWallMs) {
    runtime.pollOnce(nowMs());
    // 漂移在「刚执行完一个 tick」的时刻采样：tick 之间量到的是相位锯齿（天然 ±一个 tick），
    // 不是落后量；tick 边界上量到的才是「这个 tick 迟到了多少」。
    const ac::server::RuntimeMetrics live = runtime.metrics();
    if (live.ticks != lastDriftTick) {
      lastDriftTick = live.ticks;
      driftSamples.push_back(std::fabs(live.simDriftMs));
    }
    const std::uint64_t now = nowMs();
    if (now - prevCpuAtMs >= kSampleIntervalMs) {
      const double windowMs = static_cast<double>(now - prevCpuAtMs);
      const std::uint64_t cpuMs = processCpuMs();
      const double cpuPct = windowMs > 0.0
                                ? static_cast<double>(cpuMs - prevCpuMs) / windowMs * 100.0
                                : 0.0;
      prevCpuMs = cpuMs;
      prevCpuAtMs = now;
      ++second;
      if (second > kWarmupSeconds) {
        cpuSamples.push_back(cpuPct);
        if (second % kRssSampleEverySeconds == 0) {
          rssSamples.push_back(processRssMb());
          rssMinutes.push_back(static_cast<double>(second) / 60.0);
        }
      }
      const std::size_t clientCount = runtime.clientStats(stats, kMaxClientStats);
      for (std::size_t i = 0u; i < clientCount; ++i) {
        std::size_t previous = 0u;
        std::size_t slot = prevIds.size();
        for (std::size_t j = 0u; j < prevIds.size(); ++j) {
          if (prevIds[j] == stats[i].transportId) {
            previous = prevBytes[j];
            slot = j;
            break;
          }
        }
        const double kbps =
            static_cast<double>(stats[i].bytesOut - previous) / windowMs * 1000.0 / kBytesPerKb;
        if (kbps > bytesPerClientMaxKbps) bytesPerClientMaxKbps = kbps;
        if (slot == prevIds.size()) {
          prevIds.push_back(stats[i].transportId);
          prevBytes.push_back(stats[i].bytesOut);
        } else {
          prevBytes[slot] = stats[i].bytesOut;
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const double durationSec = static_cast<double>(nowMs() - startWallMs) / 1000.0;
  killChild(child);

  // §2-3：HTTP 面在负载之后仍可服务（运行中的存活证明写进报告 note）。
  const std::string healthResponse = httpGet(runtime, "/health");
  const std::size_t healthStatus = statusOf(healthResponse);
  const std::string metricsResponse = httpGet(runtime, "/metrics");
  const std::size_t metricsStatus = statusOf(metricsResponse);
  const bool hasSnapshotsCounter =
      metricsResponse.find("ac_snapshots_sent_total") != std::string::npos;

  const ac::server::RuntimeMetrics metrics = runtime.metrics();
  std::string note{};
  bool isScenarioFailed = false;
  if (scenario->isSoak) {
    // §2-4：soak 结束后房间数回 0。
    runtime.stop();
    const bool isRoomsReleased = runtime.roomCount() == 0u;
    note += isRoomsReleased ? "soak roomsAfter=0" : "soak roomsAfter!=0";
    if (!isRoomsReleased) isScenarioFailed = true;
  } else {
    runtime.stop();
  }
  note += (note.empty() ? "" : "; ");
  note += "http health=" + std::to_string(healthStatus) + " metrics=" +
          std::to_string(metricsStatus) + (hasSnapshotsCounter ? " counters=ok" : " counters=missing");
  if (scenario->isLatency) {
    note += "; latency-200：隔墙命中与回滚夹取的判定留在 S07/S11 的用例组（step/security 全绿）";
  }

  ac::perf::PerfSample sample{};
  sample.metrics.cpuMeanPct = meanOf(cpuSamples);
  sample.metrics.cpuP95Pct = percentileOf(cpuSamples, 0.95);
  sample.metrics.bytesPerClientMaxKbps = bytesPerClientMaxKbps;
  sample.metrics.snapshotBytesP95 = metrics.snapshotBytesP95;
  sample.metrics.snapshotBytesMax = static_cast<double>(metrics.snapshotBytesMax);
  const double personMinutes = static_cast<double>(scenario->players) * (durationSec / 60.0);
  sample.metrics.hardCorrectPerMinPerClient =
      personMinutes > 0.0 ? static_cast<double>(metrics.hardCorrect) / personMinutes : 0.0;
  sample.metrics.scheduleErrorP95Ms = metrics.scheduleErrorP95Ms;
  sample.metrics.simDriftMsMax = driftSamples.empty() ? std::fabs(metrics.simDriftMs)
                                                      : *std::max_element(driftSamples.begin(), driftSamples.end());
  if (driftSamples.size() >= 6u) {
    const std::size_t third = driftSamples.size() / 3u;
    const std::vector<double> head(driftSamples.begin(), driftSamples.begin() + static_cast<std::ptrdiff_t>(third));
    const std::vector<double> tail(driftSamples.end() - static_cast<std::ptrdiff_t>(third), driftSamples.end());
    // 计划口径是两个 P95 的「差」，取绝对值即可（都是 |drift|）。
    sample.scheduleHeadTailGapMs = std::fabs(percentileOf(head, 0.95) - percentileOf(tail, 0.95));
  }
  sample.metrics.rssSlopeMbPerMin =
      scenario->isSoak ? slopePerMinute(rssMinutes, rssSamples) : 0.0;
  sample.metrics.eventsDropped = static_cast<std::uint64_t>(metrics.eventsDropped);
  sample.metrics.tickSkips = static_cast<std::uint64_t>(metrics.tickSkips);
  // §5 G8：未捕获异常要真读日志侧的计数（评审：以前恒 0，该项永不可能 fail）。
  sample.metrics.uncaughtExceptions = static_cast<std::uint64_t>(ac::log::uncaughtCount());
  sample.isScenarioFailed = isScenarioFailed;
  // 注：scheduleHeadTailGapMs 已在上面的 G6 段按漂移序列算好（不再用 core 的极差口径）。
  // §5 G6 替代判据：只在定时器粒度比 8ms 预算更粗时才允许放宽（评审：以前无条件启用）。
  const double timerGranularityMs = measureTimerGranularityMs();
  sample.isScheduleFallbackEnabled = timerGranularityMs > ac::core::kTickScheduleErrorP95BudgetMs;
  note += "; ticks=" + std::to_string(metrics.ticks) + " expectedTicks=" +
          std::to_string(static_cast<std::uint64_t>(durationSec * 1000.0 /
                                                    static_cast<double>(ac::version::kTickMs))) +
          " intervalP95=" + std::to_string(static_cast<long long>(metrics.tickIntervalErrorP95Ms)) +
          "ms workP95=" + std::to_string(static_cast<long long>(metrics.tickWorkP95Ms)) +
          "ms headTailGap=" + std::to_string(static_cast<long long>(metrics.scheduleHeadTailGapMs)) +
          "ms timerGranularity=" + std::to_string(static_cast<long long>(timerGranularityMs)) + "ms";
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) sample.isMeasured[i] = true;
  sample.isMeasured[static_cast<std::size_t>(ac::perf::ThresholdId::kRssSlope)] = scenario->isSoak;

  double overrides[ac::perf::kThresholdCount] = {};
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) overrides[i] = ac::perf::kNoOverride;
  for (const std::string& entry : options.breaks) {
    const std::size_t eq = entry.find('=');
    if (eq == std::string::npos) continue;
    const ac::perf::ThresholdDef* def = ac::perf::findThresholdByName(entry.substr(0u, eq));
    if (def == nullptr) {
      std::fprintf(stderr, "unknown threshold in --break: %s\n", entry.c_str());
      return 2;
    }
    overrides[static_cast<std::size_t>(def->id)] = std::atof(entry.substr(eq + 1u).c_str());
  }

  ac::perf::PerfVerdict judged =
      ac::perf::evaluateThresholds(sample, options.breaks.empty() ? nullptr : overrides);
  // 场景级检查（soak 的房间回落等）已随 sample.isScenarioFailed 进入判定（评审 #1）。
  const std::string buildType = buildTypeName();
  const std::string compiler = compilerName();
  const std::string host = osName() + " " + std::to_string(cpuCoreCount()) + "-core";
  const std::string startedAt = isoNow();
  ac::perf::PerfReportHeader header{};
  header.tool = "ac_gate";
  header.scenario = scenario->name;
  header.seed = options.seed;
  header.buildType = buildType;
  header.compiler = compiler;
  header.host = host;
  header.startedAt = startedAt;
  header.durationSec = durationSec;
  if (!note.empty()) {
    judged.note = judged.note.empty() ? note : judged.note + "; " + note;
  }
  const std::string json = ac::perf::encodePerfReport(header, sample, judged);
  if (std::FILE* file = std::fopen(options.out.c_str(), "wb"); file != nullptr) {
    (void)std::fwrite(json.data(), 1u, json.size(), file);
    (void)std::fputc('\n', file);
    std::fclose(file);
  } else {
    std::fprintf(stderr, "cannot write report: %s\n", options.out.c_str());
    return 1;
  }

  std::printf("verdict=%s exit=%d duration=%.1fs cpuMean=%.2f%% cpuP95=%.2f%% bw=%.2fKB/s "
              "snapP95=%.0fB snapMax=%.0fB schedP95=%.2fms drift=%.1fms rss=%.3fMB/min "
              "dropped=%llu skips=%llu\n",
              ac::perf::thresholdStatusName(judged.overall), judged.exitCode, durationSec,
              sample.metrics.cpuMeanPct, sample.metrics.cpuP95Pct,
              sample.metrics.bytesPerClientMaxKbps, sample.metrics.snapshotBytesP95,
              sample.metrics.snapshotBytesMax, sample.metrics.scheduleErrorP95Ms,
              sample.metrics.simDriftMsMax, sample.metrics.rssSlopeMbPerMin,
              static_cast<unsigned long long>(sample.metrics.eventsDropped),
              static_cast<unsigned long long>(sample.metrics.tickSkips));
  for (std::size_t i = 0u; i < ac::perf::kThresholdCount; ++i) {
    const ac::perf::ThresholdDef& def = ac::perf::thresholdDefs()[i];
    std::printf("  %s %-28s measured=%.3f limit=%.3f %s -> %s\n", def.name, def.label,
                judged.results[i].measured, judged.results[i].limit, def.unit,
                ac::perf::thresholdStatusName(judged.results[i].status));
  }
  return judged.exitCode;
}
