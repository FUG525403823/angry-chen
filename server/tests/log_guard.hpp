#pragma once
// 改过全局日志 sink / 阈值的用例用它兜底：析构时恢复 stderr 与 info 阈值，
// 断言中途 return 也不会把 sink 留在文件上（S13 评审低-6）。
#include "core/log.hpp"

namespace ac::test {

class LogGuard {
 public:
  LogGuard() = default;
  LogGuard(const LogGuard&) = delete;
  LogGuard& operator=(const LogGuard&) = delete;
  ~LogGuard() {
    ac::log::close();
    ac::log::useStderr();
    ac::log::setMinLevel(ac::log::Level::info);
  }
};

}  // namespace ac::test
