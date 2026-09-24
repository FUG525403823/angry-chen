#include "core/version.hpp"

#include "core/math.hpp"

#include <cstdio>

namespace ac::version {

// §5 的 ac_server_version 标签与 --version 行同源之外，还要与调度器真正使用的 tick 同源。
static_assert(kTickMs == static_cast<std::uint32_t>(ac::kTickMs), "version 的 tick 必须等于调度器的 tick");

std::string versionLine() {
  char buf[96];
  std::snprintf(buf, sizeof(buf), "%.*s %.*s protocol=%u tick=%ums",
                static_cast<int>(kName.size()), kName.data(),
                static_cast<int>(kVersion.size()), kVersion.data(),
                static_cast<unsigned>(kProtocol), static_cast<unsigned>(kTickMs));
  return std::string(buf);
}

}  // namespace ac::version
