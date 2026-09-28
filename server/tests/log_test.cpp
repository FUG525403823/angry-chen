// S13 §5 的日志契约：事件行字段顺序、detail 五类取值、级别数值与阈值、事件名最小集、
// 未捕获异常后写日志仍可用、超长行按 §5.4 口径截断。
#include "tiny_test.hpp"
#include "log_guard.hpp"
#include "test_io.hpp"
#include "tmp_workdir.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

#include "core/log.hpp"

namespace {

}  // namespace

AC_TEST(log_event_line_has_frozen_field_order) {
  const std::string line = ac::log::formatEventLine(
      ac::log::Level::info, "room.create", ac::log::EventContext{1700000000000LL, 7u, 42u, 3},
      {ac::log::DetailField("code", "ABCD"), ac::log::DetailField("kc", 2)});
  AC_CHECK(line.rfind("{\"ts\":\"2023-11-14T22:13:20.000Z\"", 0) == 0u);
  const std::size_t levelAt = line.find("\"level\":\"info\"");
  const std::size_t evtAt = line.find("\"evt\":\"room.create\"");
  const std::size_t roomAt = line.find("\"room\":7");
  const std::size_t tickAt = line.find("\"tick\":42");
  const std::size_t pidAt = line.find("\"pid\":3");
  const std::size_t detailAt = line.find("\"detail\":{");
  AC_CHECK(levelAt != std::string::npos && evtAt != std::string::npos);
  AC_CHECK(roomAt != std::string::npos && tickAt != std::string::npos);
  AC_CHECK(pidAt != std::string::npos && detailAt != std::string::npos);
  AC_CHECK(levelAt < evtAt);
  AC_CHECK(evtAt < roomAt);
  AC_CHECK(roomAt < tickAt);
  AC_CHECK(tickAt < pidAt);
  AC_CHECK(pidAt < detailAt);
  AC_CHECK(line.find("\"code\":\"ABCD\"") != std::string::npos);
  AC_CHECK(line.find("\"kc\":2") != std::string::npos);
  AC_CHECK_EQ(line.back(), '}');
}

AC_TEST(log_payload_renders_five_value_kinds) {
  const std::string_view items[] = {"a", "b c"};
  const std::string line = ac::log::formatEventLine(
      ac::log::Level::warn, "wave.clear", ac::log::EventContext{1700000000000LL, 1u, 2u, 0},
      {ac::log::DetailField("text", "x"), ac::log::DetailField("count", 3),
       ac::log::DetailField("ratio", 0.5), ac::log::DetailField("flag", true),
       ac::log::DetailField::null("nothing"), ac::log::DetailField::array("tags", items, 2u)});
  AC_CHECK(line.find("\"text\":\"x\"") != std::string::npos);
  AC_CHECK(line.find("\"count\":3") != std::string::npos);
  AC_CHECK(line.find("\"ratio\":0.5") != std::string::npos);
  AC_CHECK(line.find("\"flag\":true") != std::string::npos);
  AC_CHECK(line.find("\"nothing\":null") != std::string::npos);
  AC_CHECK(line.find("\"tags\":[\"a\",\"b c\"]") != std::string::npos);
  AC_CHECK(line.find("\"level\":\"warn\"") != std::string::npos);
}

AC_TEST(log_level_values_equal_frozen_numbers) {
  AC_CHECK_EQ(ac::log::levelValue(ac::log::Level::debug), 10);
  AC_CHECK_EQ(ac::log::levelValue(ac::log::Level::info), 20);
  AC_CHECK_EQ(ac::log::levelValue(ac::log::Level::warn), 30);
  AC_CHECK_EQ(ac::log::levelValue(ac::log::Level::error), 40);
  AC_CHECK_EQ(ac::log::levelValue(ac::log::Level::trace), 5);
  ac::log::Level parsed = ac::log::Level::trace;
  AC_CHECK(ac::log::parseLevelName("warn", parsed));
  AC_CHECK(parsed == ac::log::Level::warn);
  AC_CHECK(ac::log::parseLevelName("30", parsed));
  AC_CHECK(parsed == ac::log::Level::warn);
  AC_CHECK(ac::log::parseLevelName("debug", parsed));
  AC_CHECK(parsed == ac::log::Level::debug);
  AC_CHECK(!ac::log::parseLevelName("nope", parsed));
}

AC_TEST(log_threshold_filters_below_configured_level) {
  ac::test::TempDir dir("log");
  AC_CHECK(dir.isReady());
  const std::string path = dir.file("events.log");
  AC_CHECK(ac::log::useFile(path));
  ac::log::setMinLevel(ac::log::Level::warn);
  ac::log::event(ac::log::Level::debug, "wave.start", ac::log::EventContext{1700000000000LL, 1u, 0u, 0});
  ac::log::event(ac::log::Level::warn, "wave.start", ac::log::EventContext{1700000000000LL, 1u, 0u, 0});
  ac::log::close();
  ac::log::setMinLevel(ac::log::Level::info);
  const std::string text = ac::test::readTextFile(path);
  AC_CHECK_EQ(ac::test::countNewlines(text), static_cast<std::size_t>(1));
  AC_CHECK(text.find("\"evt\":\"wave.start\"") != std::string::npos);
  AC_CHECK(ac::log::setMinLevelFromEnv("error"));
  AC_CHECK(ac::log::minLevel() == ac::log::Level::error);
  ac::log::setMinLevel(ac::log::Level::info);
  AC_CHECK(!ac::log::setMinLevelFromEnv("nope"));
  AC_CHECK(ac::log::minLevel() == ac::log::Level::info);
  AC_CHECK(!ac::log::setMinLevelFromEnv(""));
}

AC_TEST(log_event_name_table_has_nineteen_names) {
  AC_CHECK_EQ(ac::log::knownEventCount(), static_cast<std::size_t>(19));
  AC_CHECK(ac::log::knownEvent(0u) == std::string_view("room.create"));
  AC_CHECK(ac::log::knownEvent(ac::log::knownEventCount()) == std::string_view{});
  AC_CHECK(ac::log::isKnownEvent("error.uncaught"));
  AC_CHECK(ac::log::isKnownEvent("session.rate_limited"));
  AC_CHECK(!ac::log::isKnownEvent("nope.nope"));
  for (std::size_t i = 0u; i < ac::log::knownEventCount(); ++i) {
    AC_CHECK(ac::log::knownEvent(i).size() > 0u);
    AC_CHECK(ac::log::isKnownEvent(ac::log::knownEvent(i)));
  }
  // 消费方（C3 接线）：句柄 → 名字表 → 日志行是同一条链，生产调用点（main.cpp / report.cpp /
  // reportUncaught）不再手写这些字面量，所以表有了表外的读方。
  AC_CHECK(ac::log::eventName(ac::log::EventName::kStoreError) == std::string_view("store.error"));
  AC_CHECK(ac::log::eventName(ac::log::EventName::kErrorUncaught) == std::string_view("error.uncaught"));
  for (std::size_t i = 0u; i < ac::log::knownEventCount(); ++i) {
    AC_CHECK(ac::log::eventName(static_cast<ac::log::EventName>(i)) == ac::log::knownEvent(i));
  }
  ac::test::LogGuard guard;
  ac::test::TempDir dir("evtnames");
  AC_CHECK(dir.isReady());
  const std::string path = dir.file("events.log");
  AC_CHECK(ac::log::useFile(path));
  ac::log::event(ac::log::Level::error, ac::log::EventName::kStoreError,
                 ac::log::EventContext{1700000000000LL, 0u, 0u, 0},
                 {ac::log::DetailField("dir", std::string_view("/tmp"))});
  ac::log::close();
  const std::string sink = ac::test::readTextFile(path);
  AC_CHECK_EQ(ac::test::countNewlines(sink), static_cast<std::size_t>(1));
  AC_CHECK(sink.find("\"evt\":\"store.error\"") != std::string::npos);
}

AC_TEST(log_uncaught_error_keeps_writer_alive) {
  ac::test::LogGuard guard;  // 早退也不把 sink 留在文件上
  ac::test::TempDir dir("log");
  AC_CHECK(dir.isReady());
  const std::string path = dir.file("crash.log");
  AC_CHECK(ac::log::useFile(path));
  ac::log::reportUncaught("boom");
  ac::log::event(ac::log::Level::info, "shutdownComplete", ac::log::EventContext{1700000000000LL, 0u, 9u, 0});
  ac::log::close();
  const std::string text = ac::test::readTextFile(path);
  AC_CHECK_EQ(ac::test::countNewlines(text), static_cast<std::size_t>(2));
  AC_CHECK(text.find("\"evt\":\"error.uncaught\"") != std::string::npos);
  AC_CHECK(text.find("\"what\":\"boom\"") != std::string::npos);
  AC_CHECK(text.find("\"evt\":\"shutdownComplete\"") != std::string::npos);
  AC_CHECK(text.find("\"tick\":9") != std::string::npos);
}

AC_TEST(log_long_payload_truncates_without_holes) {
  const std::string blob(4000u, 'x');
  const std::string line = ac::log::formatEventLine(
      ac::log::Level::info, "store.error", ac::log::EventContext{1700000000000LL, 0u, 0u, 0},
      {ac::log::DetailField("blob", std::string_view(blob)), ac::log::DetailField("after", 1),
       ac::log::DetailField("last", 2)});
  AC_CHECK(line.size() <= 4096u);
  AC_CHECK(line.find("\"truncated\":true}") != std::string::npos);
  AC_CHECK(line.find("\"after\":1") == std::string::npos);
  AC_CHECK(line.find("\"last\":2") == std::string::npos);
  AC_CHECK(line.rfind("{\"ts\":", 0) == 0u);
  const std::string shortLine = ac::log::formatEventLine(
      ac::log::Level::info, "store.error", ac::log::EventContext{1700000000000LL, 0u, 0u, 0},
      {ac::log::DetailField("after", 1)});
  AC_CHECK(shortLine.find("\"truncated\"") == std::string::npos);
  AC_CHECK(shortLine.find("\"after\":1") != std::string::npos);
}

AC_TEST(log_file_env_redirects_writer) {
  ac::test::LogGuard guard;
  ac::test::TempDir dir("log");
  const std::string path = dir.file("nested/env.log");
  AC_CHECK_EQ(ac::test::setEnvVar("AC_LOG_FILE", path), true);
  AC_CHECK(ac::log::applyLogFileFromEnv());  // 目录不存在时按需创建
  ac::log::event(ac::log::Level::info, "serverStarted", {},
                 {ac::log::DetailField("via", "env")});
  ac::log::close();
  const std::string text = ac::test::readTextFile(path);
  AC_CHECK(text.find("\"evt\":\"serverStarted\"") != std::string::npos);
  AC_CHECK(text.find("\"detail\":{\"via\":\"env\"}") != std::string::npos);
  AC_CHECK(text.find("\"room\":0") != std::string::npos);
  AC_CHECK(text.find("\"pid\":") != std::string::npos);
  ac::test::clearEnvVar("AC_LOG_FILE");
  AC_CHECK(!ac::log::applyLogFileFromEnv());
}

// F2（清单 D：`AC_LOG_FILE` 指到服务用户不可写的路径时，日志静默退回 stderr ⇒ server.log 恒 0 字节）。
AC_TEST(log_file_env_failure_is_logged_and_keeps_writer_alive) {
  ac::test::LogGuard guard;
  ac::test::TempDir dir("logfail");
  AC_CHECK(dir.isReady());
  // 先把 sink 指到一个可写文件：这样「打不开」的 error 行落在哪里可断言（生产里就是 stderr →
  // server.err.log；排障时看的就是它）。
  const std::string sinkPath = dir.file("current.log");
  AC_CHECK(ac::log::useFile(sinkPath));
  // 目标是一个**目录**：`fopen(ab)` 在各平台都失败（EACCES / EISDIR），等价于现场的 EACCES。
  const std::string badPath = dir.file("not-a-file");
  std::error_code code;
  std::filesystem::create_directories(badPath, code);
  AC_CHECK(!code);
  AC_CHECK_EQ(ac::test::setEnvVar("AC_LOG_FILE", badPath), true);
  AC_CHECK(!ac::log::applyLogFileFromEnv());  // 打不开：返回 false，不切 sink
  ac::test::clearEnvVar("AC_LOG_FILE");
  ac::log::event(ac::log::Level::error, "afterLogFileFailure", {});  // 进程没崩、日志口还在
  ac::log::close();
  const std::string text = ac::test::readTextFile(sinkPath);
  // ① EACCES 不再静默：路径 + errno 文本都进了日志
  AC_CHECK(text.find("\"evt\":\"logFileOpenFailed\"") != std::string::npos);
  AC_CHECK(text.find("\"level\":\"error\"") != std::string::npos);
  AC_CHECK(text.find("not-a-file") != std::string::npos);
  AC_CHECK(text.find("\"errno\":") != std::string::npos);
  AC_CHECK(text.find("\"error\":\"") != std::string::npos);
  // ② 退回的是**当前 sink**（不是关掉日志口），后续行仍能落盘
  AC_CHECK(text.find("\"evt\":\"afterLogFileFailure\"") != std::string::npos);
  std::printf("logFileOpenFailed line: %s\n",
              text.substr(text.find("\"evt\":\"logFileOpenFailed\"") - 1u, 220u).c_str());
}
