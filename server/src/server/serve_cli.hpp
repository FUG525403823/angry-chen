#pragma once
// S15 §15.4 D3：`ac_server --serve` 的选项解析。
// 从 main.cpp 里搬出来是为了能被 ac_tests 直接覆盖（原实现只能在真进程里跑）。
//
// 契约：
//   1. 与选项出现顺序无关 —— `--serve` 放在 `--udp-port` 之前/之后/中间都等价；
//   2. `--k=v` 与 `--k v` 两种写法等价（§6-2/§9 的计划命令用空格写法）；
//   3. 未知选项、重复选项、缺值、端口越界、非数字一律判失败（不再静默忽略）；
//   4. `--log-level` 只影响日志阈值，等价于环境变量 AC_LOG_LEVEL（CLI 在 env 之后生效，即 CLI 优先）；
//   5. `--auto-ready` 是无取值开关（ADR-013）：打开后会话连上即 ready 并开局，供装载/门禁/压测使用；
//      产品默认（不带该开关）走真实大厅，客户端不带 Ready 位就不会开局。
#include <string>

#include "core/log.hpp"
#include "server/runtime.hpp"

namespace ac::server {

struct ServeOptions {
  RuntimeConfig config{};
  double minutes = 0.0;  // 0 = 一直跑到收到 SIGINT/SIGTERM
  bool isLogLevelSet = false;
  ac::log::Level logLevel = ac::log::Level::info;
  bool isHelp = false;  // `--help`/`-h`：调用方打印 usage 并按成功退出
};

// 解析 AC_HTTP_BIND / AC_TRUST_LOOPBACK_PROXY；未设置时保留原值，非法值返回 false。
// 在 parseServeArgs 之前调用，使 CLI 优先，且非法环境配置无法被 CLI 掩盖。
bool parseServeEnvironment(ServeOptions& options, std::string* error);

// 解析完整 argv（含 argv[0]）。`--serve` 必须出现且只能出现一次。
// 失败返回 false：isHelp 为真时是 usage 请求，否则 *error 写明原因。
bool parseServeArgs(int argc, char** argv, ServeOptions& options, std::string* error);

}  // namespace ac::server
