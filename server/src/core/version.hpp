#pragma once
// S13 §5「版本」行：`ac_server_version` 的标签与 --version 行同源，唯一来源就是这里。
// 进程入口与 /metrics 渲染都读这份常量，避免出现第二个版本字面量。
#include <cstdint>
#include <string>
#include <string_view>

namespace ac::version {

// S15 §5：版本号由构建注入（CMake 的 AC_SERVER_VERSION）；未注入时回落到这里的默认值。
#ifndef AC_SERVER_VERSION
#define AC_SERVER_VERSION "0.1.0"
#endif

inline constexpr std::string_view kName = "ac_server";
inline constexpr std::string_view kVersion = AC_SERVER_VERSION;
inline constexpr std::uint32_t kProtocol = 1u;
inline constexpr std::uint32_t kTickMs = 50u;

// "ac_server 0.1.0 protocol=1 tick=50ms"（--version 行的唯一产出点）
std::string versionLine();

}  // namespace ac::version
