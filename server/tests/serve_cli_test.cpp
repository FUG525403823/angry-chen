// S15 §15.4 D3：`--serve` 的选项解析（顺序无关、非法/重复拒绝）与停止请求的缝。
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "server/serve_cli.hpp"
#include "server/shutdown.hpp"
#include "tiny_test.hpp"

namespace {

// argv 的持有者：parseServeArgs 只读 argv，这里负责把 std::string 存到活着的地方。
struct Args {
  std::vector<std::string> storage;
  std::vector<char*> argv;

  explicit Args(std::initializer_list<const char*> items) {
    storage.emplace_back("ac_server");
    for (const char* item : items) storage.emplace_back(item);
    for (std::string& item : storage) argv.push_back(item.data());
  }

  int argc() const { return static_cast<int>(argv.size()); }
};

bool parse(const Args& args, ac::server::ServeOptions& options, std::string* error) {
  return ac::server::parseServeArgs(args.argc(), const_cast<char**>(args.argv.data()), options,
                                    error);
}

struct ScopedEnvironment {
  const char* name;
  bool wasSet;
  std::string previous;

  ScopedEnvironment(const char* key, const char* value) : name(key) {
    const char* old = std::getenv(name);
    wasSet = old != nullptr;
    if (wasSet) previous = old;
    AC_CHECK(set(value) == 0);
  }

  ~ScopedEnvironment() { set(wasSet ? previous.c_str() : nullptr); }

  int set(const char* value) const {
#ifdef _WIN32
    return _putenv_s(name, value == nullptr ? "" : value);
#else
    return value == nullptr ? unsetenv(name) : setenv(name, value, 1);
#endif
  }
};

// §9/§15.4 D3 的同一组取值：端口、数据目录、seed、时长、日志阈值都非默认。
void checkCanonical(const ac::server::ServeOptions& options) {
  AC_CHECK_EQ(options.config.udpPort, static_cast<std::uint16_t>(8798));
  AC_CHECK_EQ(options.config.httpPort, static_cast<std::uint16_t>(8799));
  AC_CHECK(options.config.dataDir == "build/acvar");
  AC_CHECK_EQ(options.config.seed, static_cast<std::uint32_t>(7));
  AC_CHECK_NEAR(options.minutes, 0.5, 1e-12);
  AC_CHECK(options.isLogLevelSet);
  AC_CHECK(ac::log::levelValue(options.logLevel) == ac::log::levelValue(ac::log::Level::warn));
}

}  // namespace

// ① 顺序无关：--serve 打头 / 打尾 / 中间，`--k=v` 与 `--k v` 混用，解析结果必须一致。
AC_TEST(serve_cli_is_order_independent) {
  const Args leading{"--serve", "--udp-port=8798", "--http-port=8799", "--data-dir=build/acvar",
                     "--seed=7", "--minutes=0.5", "--log-level=warn"};
  const Args trailing{"--udp-port=8798", "--http-port=8799", "--data-dir=build/acvar", "--seed=7",
                      "--minutes=0.5", "--log-level=warn", "--serve"};
  const Args middle{"--udp-port=8798", "--http-port=8799", "--serve", "--data-dir=build/acvar",
                    "--seed=7", "--minutes=0.5", "--log-level=warn"};
  const Args spaced{"--udp-port", "8798", "--serve", "--http-port", "8799", "--data-dir",
                    "build/acvar", "--seed", "7", "--minutes", "0.5", "--log-level", "warn"};
  for (const Args* args : {&leading, &trailing, &middle, &spaced}) {
    ac::server::ServeOptions options{};
    std::string error;
    AC_CHECK(parse(*args, options, &error));
    AC_CHECK(error.empty());
    checkCanonical(options);
  }
}

AC_TEST(serve_cli_accepts_http_bind_ipv4) {
  ac::server::ServeOptions options{};
  std::string error;
  AC_CHECK(parse(Args{"--serve", "--http-bind=127.0.0.1"}, options, &error));
  AC_CHECK(error.empty());
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0x7F000001u));
  AC_CHECK(parse(Args{"--http-bind", "192.168.1.10", "--serve"}, options, &error));
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0xC0A8010Au));
}

AC_TEST(serve_cli_accepts_trust_loopback_proxy) {
  ac::server::ServeOptions options{};
  std::string error;
  AC_CHECK(parse(Args{"--serve", "--trust-loopback-proxy=1"}, options, &error));
  AC_CHECK(error.empty());
  AC_CHECK(options.config.trustLoopbackProxy);
  AC_CHECK(parse(Args{"--trust-loopback-proxy", "0", "--serve"}, options, &error));
  AC_CHECK(!options.config.trustLoopbackProxy);
}

AC_TEST(serve_cli_environment_configures_http_binding_and_proxy_trust) {
  const ScopedEnvironment bind{"AC_HTTP_BIND", "192.168.1.10"};
  const ScopedEnvironment trust{"AC_TRUST_LOOPBACK_PROXY", "1"};
  ac::server::ServeOptions options{};
  std::string error;
  AC_CHECK(ac::server::parseServeEnvironment(options, &error));
  AC_CHECK(error.empty());
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0xC0A8010Au));
  AC_CHECK(options.config.trustLoopbackProxy);
}

AC_TEST(serve_cli_http_defaults_and_ipv4_boundaries) {
  ac::server::ServeOptions options{};
  std::string error;
  AC_CHECK(parse(Args{"--serve"}, options, &error));
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0x7F000001u));
  AC_CHECK(!options.config.trustLoopbackProxy);
  AC_CHECK(parse(Args{"--serve", "--http-bind=0.0.0.0"}, options, &error));
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0u));
  AC_CHECK(parse(Args{"--serve", "--http-bind=255.255.255.255"}, options, &error));
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0xFFFFFFFFu));
  AC_CHECK(parse(Args{"--serve", "--http-bind=010.002.003.004"}, options, &error));
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0x0A020304u));
}

AC_TEST(serve_cli_rejects_invalid_http_binding_and_proxy_trust) {
  for (const char* value : {"", "localhost", "::1", "127.1", "127.0.0", "127.0.0.1.2",
                            "127..0.1", ".127.0.1", "127.0.0.", "256.0.0.1", "1.2.3.999",
                            "999999999999.0.0.1", "0000.0.0.1", "-1.0.0.1", "+1.0.0.1",
                            " 127.0.0.1", "127.0.0.1 ", "127.0.0.1:8787", "0x7f.0.0.1"}) {
    ac::server::ServeOptions options{};
    std::string error;
    const std::string arg = std::string("--http-bind=") + value;
    AC_CHECK(!parse(Args{"--serve", arg.c_str()}, options, &error));
    AC_CHECK(error.find("--http-bind") != std::string::npos);
  }
  for (const char* value : {"", "true", "false", "2", "-1", "+1", "01", "00", " 1", "1 "}) {
    ac::server::ServeOptions options{};
    std::string error;
    const std::string arg = std::string("--trust-loopback-proxy=") + value;
    AC_CHECK(!parse(Args{"--serve", arg.c_str()}, options, &error));
    AC_CHECK(error.find("--trust-loopback-proxy") != std::string::npos);
  }
}

AC_TEST(serve_cli_rejects_missing_and_duplicate_http_options) {
  for (const Args& args : {
           Args{"--serve", "--http-bind"},
           Args{"--serve", "--http-bind", "--trust-loopback-proxy=0"},
           Args{"--serve", "--http-bind=127.0.0.1", "--http-bind", "0.0.0.0"},
           Args{"--serve", "--trust-loopback-proxy"},
           Args{"--serve", "--trust-loopback-proxy", "--http-bind=127.0.0.1"},
           Args{"--serve", "--trust-loopback-proxy=1", "--trust-loopback-proxy", "0"}}) {
    ac::server::ServeOptions options{};
    std::string error;
    AC_CHECK(!parse(args, options, &error));
    AC_CHECK(!error.empty());
  }
}

AC_TEST(serve_cli_overrides_valid_http_environment) {
  const ScopedEnvironment bind{"AC_HTTP_BIND", "192.168.1.10"};
  const ScopedEnvironment trust{"AC_TRUST_LOOPBACK_PROXY", "1"};
  ac::server::ServeOptions options{};
  std::string error;
  AC_CHECK(ac::server::parseServeEnvironment(options, &error));
  AC_CHECK(parse(Args{"--http-bind", "127.0.0.1", "--serve", "--trust-loopback-proxy=0"},
                 options, &error));
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0x7F000001u));
  AC_CHECK(!options.config.trustLoopbackProxy);
  AC_CHECK(error.empty());
}

AC_TEST(serve_cli_absent_http_environment_preserves_defaults) {
  const ScopedEnvironment bind{"AC_HTTP_BIND", nullptr};
  const ScopedEnvironment trust{"AC_TRUST_LOOPBACK_PROXY", nullptr};
  ac::server::ServeOptions options{};
  std::string error;
  AC_CHECK(ac::server::parseServeEnvironment(options, &error));
  AC_CHECK_EQ(options.config.httpBindIpv4, static_cast<std::uint32_t>(0x7F000001u));
  AC_CHECK(!options.config.trustLoopbackProxy);
  AC_CHECK(error.empty());
}

AC_TEST(serve_cli_rejects_invalid_http_environment_before_cli_override) {
  for (const char* value : {"localhost", "::1", "127.1", "256.0.0.1", "127.0.0.1 ", " "}) {
    const ScopedEnvironment bind{"AC_HTTP_BIND", value};
    const ScopedEnvironment trust{"AC_TRUST_LOOPBACK_PROXY", "0"};
    ac::server::ServeOptions options{};
    std::string error;
    AC_CHECK(!(ac::server::parseServeEnvironment(options, &error) &&
               parse(Args{"--serve", "--http-bind=127.0.0.1"}, options, &error)));
    AC_CHECK(error.find("AC_HTTP_BIND") != std::string::npos);
  }
  for (const char* value : {"true", "false", "2", "-1", "+1", "01", " 1", "1 ", " "}) {
    const ScopedEnvironment bind{"AC_HTTP_BIND", "127.0.0.1"};
    const ScopedEnvironment trust{"AC_TRUST_LOOPBACK_PROXY", value};
    ac::server::ServeOptions options{};
    std::string error;
    AC_CHECK(!(ac::server::parseServeEnvironment(options, &error) &&
               parse(Args{"--serve", "--trust-loopback-proxy=0"}, options, &error)));
    AC_CHECK(error.find("AC_TRUST_LOOPBACK_PROXY") != std::string::npos);
  }
#ifndef _WIN32
  // Windows CRT 的 _putenv_s(name, "") 删除变量，POSIX 可区分空值和未设置。
  for (const char* name : {"AC_HTTP_BIND", "AC_TRUST_LOOPBACK_PROXY"}) {
    const ScopedEnvironment bind{"AC_HTTP_BIND", "127.0.0.1"};
    const ScopedEnvironment trust{"AC_TRUST_LOOPBACK_PROXY", "0"};
    const ScopedEnvironment empty{name, ""};
    ac::server::ServeOptions options{};
    std::string error;
    AC_CHECK(!ac::server::parseServeEnvironment(options, &error));
    AC_CHECK(error.find(name) != std::string::npos);
  }
#endif
}

// ② 非法/重复/缺值一律拒绝（旧实现把这些静默忽略，正是计划 §6-2 的失败含义）。
AC_TEST(serve_cli_rejects_illegal_and_duplicate_options) {
  const Args unknown{"--serve", "--tick=50"};
  const Args duplicatePort{"--serve", "--udp-port=8798", "--udp-port=8799"};
  const Args duplicateServe{"--serve", "--serve"};
  const Args missingValue{"--serve", "--udp-port"};
  const Args nextIsOption{"--serve", "--udp-port", "--http-port=8799"};
  const Args badPort{"--serve", "--udp-port=abc"};
  const Args outOfRangePort{"--serve", "--udp-port=70000"};
  const Args badMinutes{"--serve", "--minutes=soon"};
  const Args badSeed{"--serve", "--seed=-1"};
  const Args badLevel{"--serve", "--log-level=shout"};
  const Args serveWithValue{"--serve=1"};
  const Args withoutServe{"--udp-port=8798"};
  for (const Args* args : {&unknown, &duplicatePort, &duplicateServe, &missingValue, &nextIsOption,
                           &badPort, &outOfRangePort, &badMinutes, &badSeed, &badLevel,
                           &serveWithValue, &withoutServe}) {
    ac::server::ServeOptions options{};
    std::string error;
    AC_CHECK(!parse(*args, options, &error));
    AC_CHECK(!options.isHelp);
    AC_CHECK(!error.empty());
  }
  // 端口 0（系统分配）与 seed 边界值是合法的。
  ac::server::ServeOptions options{};
  std::string error;
  AC_CHECK(parse(Args{"--serve", "--udp-port=0", "--http-port=0", "--seed=4294967295"}, options,
                 &error));
  AC_CHECK_EQ(options.config.udpPort, static_cast<std::uint16_t>(0));
  AC_CHECK_EQ(options.config.seed, static_cast<std::uint32_t>(0xFFFFFFFFu));
  // --help 走 usage 分支：失败但 isHelp，且 error 保持为空（调用方据此按成功退出）。
  ac::server::ServeOptions helpOptions{};
  std::string helpError;
  AC_CHECK(!parse(Args{"--help"}, helpOptions, &helpError));
  AC_CHECK(helpOptions.isHelp);
  AC_CHECK(helpError.empty());
}

// ③ ADR-013：产品默认走大厅（`isAutoReady == false`），装载/门禁/压测必须显式开 `--auto-ready`。
AC_TEST(serve_cli_auto_ready_flag) {
  ac::server::ServeOptions plain{};
  std::string plainError;
  AC_CHECK(parse(Args{"--serve"}, plain, &plainError));
  AC_CHECK(!plain.config.isAutoReady);

  ac::server::ServeOptions armed{};
  std::string armedError;
  AC_CHECK(parse(Args{"--serve", "--auto-ready", "--minutes=0.5"}, armed, &armedError));
  AC_CHECK(armed.config.isAutoReady);

  const Args autoReadyWithValue{"--serve", "--auto-ready=1"};
  const Args autoReadyTwice{"--serve", "--auto-ready", "--auto-ready"};
  for (const Args* args : {&autoReadyWithValue, &autoReadyTwice}) {
    ac::server::ServeOptions rejected{};
    std::string error;
    AC_CHECK(!parse(*args, rejected, &error));
    AC_CHECK(!error.empty());
  }
}

// ④ 停止请求的缝：注入与信号两条路径都必须置上同一个可观察标志，且可复位。
AC_TEST(serve_shutdown_request_is_observable_and_resettable) {
  ac::server::installShutdownHandlers();
  ac::server::clearStopRequest();
  AC_CHECK(!ac::server::isStopRequested());
  AC_CHECK_EQ(ac::server::shutdownRequestCount(), 0);
  ac::server::requestStop();  // 生产路径由信号处理函数调用，这里是同一段收尾逻辑的注入点
  AC_CHECK(ac::server::isStopRequested());
  AC_CHECK_EQ(ac::server::shutdownRequestCount(), 1);
  ac::server::requestStop();  // 重复注入不叠加
  AC_CHECK_EQ(ac::server::shutdownRequestCount(), 1);
  ac::server::clearStopRequest();
  AC_CHECK(!ac::server::isStopRequested());
}

// ④ 真信号路径：raise 走的也是 runServe 收尾会观察的那个标志（SIGINT 两侧平台都支持）。
AC_TEST(serve_shutdown_signal_sets_stop_request) {
  ac::server::installShutdownHandlers();
  ac::server::clearStopRequest();
  std::raise(SIGINT);
  AC_CHECK(ac::server::isStopRequested());
  AC_CHECK_EQ(ac::server::shutdownRequestCount(), 1);
  ac::server::clearStopRequest();
  AC_CHECK(!ac::server::isStopRequested());
  // SIGTERM（systemd 的 KillSignal）走同一个处理函数：POSIX 必达，Windows CRT 的 raise(SIGTERM)
  // 在本机实测也可达（打印 schedTermReachedHandler 作证据）。
  std::raise(SIGTERM);
  std::printf("sigtermReachedHandler=%d\n", ac::server::isStopRequested() ? 1 : 0);
  AC_CHECK(ac::server::isStopRequested());
  ac::server::clearStopRequest();
}
