#include "server/serve_cli.hpp"

#include <cstdint>
#include <cstdlib>

namespace ac::server {
namespace {

constexpr long kMinPort = 0;  // 0 = 由系统分配（用例用；生产由 §5 冻结为 8787/8788）
constexpr long kMaxPort = 65535;

// 端口/seed 只收纯十进制数字：strtol 会吞掉前导空白与正负号，那会让「非法选项」的判定随平台走。
bool isDecimalDigits(const std::string& value) {
  if (value.empty()) return false;
  for (const char ch : value) {
    if (ch < '0' || ch > '9') return false;
  }
  return true;
}

bool parseLong(const std::string& value, long minimum, long maximum, const char* name, long& out,
               std::string* error) {
  if (!isDecimalDigits(value)) {
    *error = std::string(name) + " 非法值: " + value;
    return false;
  }
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed < minimum || parsed > maximum) {
    *error = std::string(name) + " 非法值: " + value;
    return false;
  }
  out = parsed;
  return true;
}

bool parseDouble(const std::string& value, const char* name, double& out, std::string* error) {
  if (value.empty()) {
    *error = std::string(name) + " 缺少数值";
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(value.c_str(), &end);
  if (end == value.c_str() || *end != '\0' || parsed < 0.0) {
    *error = std::string(name) + " 非法值: " + value;
    return false;
  }
  out = parsed;
  return true;
}

bool parseSeed(const std::string& value, std::uint32_t& out, std::string* error) {
  if (!isDecimalDigits(value)) {
    *error = "--seed 非法值: " + value;
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (end == value.c_str() || *end != '\0' || parsed > 0xFFFFFFFFul) {
    *error = "--seed 非法值: " + value;
    return false;
  }
  out = static_cast<std::uint32_t>(parsed);
  return true;
}

}  // namespace

bool parseServeArgs(int argc, char** argv, ServeOptions& options, std::string* error) {
  bool seenServe = false;
  bool seenAutoReady = false;
  bool seenMinutes = false;
  bool seenUdpPort = false;
  bool seenHttpPort = false;
  bool seenDataDir = false;
  bool seenSeed = false;
  bool seenLogLevel = false;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const std::size_t eq = arg.find('=');
    const std::string key = eq == std::string::npos ? arg : arg.substr(0u, eq);
    std::string value = eq == std::string::npos ? std::string() : arg.substr(eq + 1u);

    if (key == "--help" || key == "-h") {
      options.isHelp = true;
      return false;
    }
    if (key == "--serve") {
      if (eq != std::string::npos) {
        *error = "--serve 不接受取值";
        return false;
      }
      if (seenServe) {
        *error = "重复选项: --serve";
        return false;
      }
      seenServe = true;
      continue;
    }
    // ADR-013：无取值开关。产品默认走大厅，装载/门禁/压测必须显式打开。
    if (key == "--auto-ready") {
      if (eq != std::string::npos) {
        *error = "--auto-ready 不接受取值";
        return false;
      }
      if (seenAutoReady) {
        *error = "重复选项: --auto-ready";
        return false;
      }
      seenAutoReady = true;
      options.config.isAutoReady = true;
      continue;
    }

    bool* seen = nullptr;
    if (key == "--minutes") seen = &seenMinutes;
    else if (key == "--udp-port") seen = &seenUdpPort;
    else if (key == "--http-port") seen = &seenHttpPort;
    else if (key == "--data-dir") seen = &seenDataDir;
    else if (key == "--seed") seen = &seenSeed;
    else if (key == "--log-level") seen = &seenLogLevel;
    if (seen == nullptr) {
      *error = "未知选项: " + arg;
      return false;
    }
    if (*seen) {
      *error = "重复选项: " + key;
      return false;
    }
    *seen = true;
    // 空格写法：本选项需要取值时才吃下一个 token（`--k` 后紧跟 `--x` 视为缺值，不是把 `--x` 当值）。
    if (eq == std::string::npos) {
      if (i + 1 >= argc || std::string(argv[i + 1]).rfind("--", 0u) == 0u) {
        *error = key + " 缺少取值";
        return false;
      }
      value = argv[++i];
    }

    if (key == "--minutes") {
      if (!parseDouble(value, "--minutes", options.minutes, error)) return false;
      continue;
    }
    if (key == "--udp-port" || key == "--http-port") {
      long port = 0;
      if (!parseLong(value, kMinPort, kMaxPort, key.c_str(), port, error)) return false;
      if (key == "--udp-port") options.config.udpPort = static_cast<std::uint16_t>(port);
      else options.config.httpPort = static_cast<std::uint16_t>(port);
      continue;
    }
    if (key == "--seed") {
      if (!parseSeed(value, options.config.seed, error)) return false;
      continue;
    }
    if (key == "--data-dir") {
      options.config.dataDir = value;
      continue;
    }
    // --log-level：与 AC_LOG_LEVEL 同一套名字（"warn" 与 "30" 都认）。
    ac::log::Level level = ac::log::Level::info;
    if (!ac::log::parseLevelName(value, level)) {
      *error = "--log-level 非法值: " + value;
      return false;
    }
    options.logLevel = level;
    options.isLogLevelSet = true;
  }

  if (!seenServe) {
    *error = "缺少 --serve";
    return false;
  }
  return true;
}

}  // namespace ac::server
