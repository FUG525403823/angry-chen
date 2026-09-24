#include "core/log.hpp"

#include "core/json_text.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace ac::log {
namespace {

constexpr std::size_t kMaxLineBytes = 4096;
constexpr const char* kTruncatedTail = ",\"truncated\":true}";

FILE* gSink = nullptr;  // nullptr 表示 stderr
bool gIsSinkOwned = false;

FILE* sink() { return gSink == nullptr ? stderr : gSink; }

// 转义口径见 core/json_text.hpp（仓库唯一一份）。

// S01 §5.4 与 S13 §5 共用的 double 口径：非有限值 -> null，否则 %.17g。
std::string renderDouble(double value) {
  if (!std::isfinite(value)) return std::string("null");
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.17g", value);
  return std::string(buf);
}

std::string renderDetail(const DetailField& field) {
  switch (field.kind) {
    case DetailField::Kind::kString:
      return "\"" + ac::core::json::escape(field.text) + "\"";
    case DetailField::Kind::kInt:
      return std::to_string(field.intValue);
    case DetailField::Kind::kUint:
      return std::to_string(field.uintValue);
    case DetailField::Kind::kDouble:
      return renderDouble(field.doubleValue);
    case DetailField::Kind::kBool:
      return field.boolValue ? std::string("true") : std::string("false");
    case DetailField::Kind::kNull:
      return std::string("null");
    case DetailField::Kind::kStrings: {
      std::string out = "[";
      for (std::size_t i = 0u; i < field.stringItemCount; ++i) {
        if (i != 0u) out += ',';
        out += "\"" + ac::core::json::escape(field.stringItems[i]) + "\"";
      }
      out += ']';
      return out;
    }
  }
  return std::string("null");
}

// S13 §5 的事件名最小集。
constexpr std::string_view kKnownEvents[] = {
    "room.create",      "room.reclaim",     "session.join",    "session.leave",
    "session.rate_limited", "grace.start",  "grace.reconnect", "grace.timeout",
    "match.start",      "match.end",        "wave.start",      "wave.clear",
    "anticheat.speed",  "store.error",      "report.write_failed",
    "listening",        "shutdownRequested", "shutdownComplete", "error.uncaught",
};
constexpr std::size_t kKnownEventCount = sizeof(kKnownEvents) / sizeof(kKnownEvents[0]);

Level gMinLevel = Level::info;

void writeLine(const std::string& line) {
  FILE* out = sink();
  std::fwrite(line.data(), 1, line.size(), out);
  std::fputc('\n', out);
  std::fflush(out);
}

std::string renderValue(const FieldValue& value) {
  return std::visit(
      [](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string_view>) {
          return "\"" + ac::core::json::escape(arg) + "\"";
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
          return renderDouble(arg);
        } else {
          return arg ? std::string("true") : std::string("false");
        }
      },
      value);
}

// UTC 毫秒 -> YYYY-MM-DDTHH:MM:SS.mmmZ，纯整数运算，不用 libc 的时区接口。
std::string formatTimestamp(std::int64_t epochMs) {
  std::int64_t seconds = epochMs / 1000;
  std::int64_t millis = epochMs % 1000;
  if (millis < 0) {
    millis += 1000;
    seconds -= 1;
  }
  std::int64_t days = seconds / 86400;
  std::int64_t rem = seconds % 86400;
  if (rem < 0) {
    rem += 86400;
    days -= 1;
  }

  const std::int64_t z = days + 719468;
  const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const std::int64_t doe = z - era * 146097;
  const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const std::int64_t mp = (5 * doy + 2) / 153;
  const std::int64_t day = doy - (153 * mp + 2) / 5 + 1;
  const std::int64_t month = mp < 10 ? mp + 3 : mp - 9;
  const std::int64_t year = era * 400 + yoe + (month <= 2 ? 1 : 0);

  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04lld-%02lld-%02lldT%02lld:%02lld:%02lld.%03lldZ",
                static_cast<long long>(year), static_cast<long long>(month),
                static_cast<long long>(day), static_cast<long long>(rem / 3600),
                static_cast<long long>((rem % 3600) / 60), static_cast<long long>(rem % 60),
                static_cast<long long>(millis));
  return std::string(buf);
}

std::int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

const char* levelName(Level level) noexcept {
  switch (level) {
    case Level::trace:
      return "trace";
    case Level::debug:
      return "debug";
    case Level::info:
      return "info";
    case Level::warn:
      return "warn";
    case Level::error:
      return "error";
  }
  return "info";
}

std::string formatLine(std::int64_t epochMs, Level level, std::string_view evt,
                       std::initializer_list<Field> fields) {
  const std::size_t budget = kMaxLineBytes - std::strlen(kTruncatedTail);
  const std::string head = std::string("{\"ts\":\"") + formatTimestamp(epochMs) +
                           "\",\"level\":\"" + levelName(level) + "\",\"evt\":\"";
  bool isEvtComplete = true;
  const std::size_t fixedBytes = head.size() + 1;  // 预留 evt 的收尾引号
  const std::size_t evtBudget = budget > fixedBytes ? budget - fixedBytes : 0;
  std::string line = head;
  ac::core::json::appendEscaped(line, evt, evtBudget, isEvtComplete);
  line += '"';
  bool isTruncated = !isEvtComplete;

  const auto emit = [&](const Field& field) {
    if (isTruncated) return;
    const std::string text = "\"" + ac::core::json::escape(field.key) + "\":" + renderValue(field.value);
    if (line.size() + 1 + text.size() > budget) {
      isTruncated = true;
      return;
    }
    line += ',';
    line += text;
  };

  for (const Field& field : fields) {
    if (field.key == "tick") emit(field);
  }
  for (const Field& field : fields) {
    if (field.key == "room") emit(field);
  }
  for (const Field& field : fields) {
    if (field.key != "tick" && field.key != "room") emit(field);
  }

  line += isTruncated ? kTruncatedTail : "}";
  return line;
}

void useStderr() {
  close();
  gSink = nullptr;
  gIsSinkOwned = false;
}

void useStdout() {
  close();
  gSink = stdout;
  gIsSinkOwned = false;
}

bool useFile(std::string_view path) {
  close();
  const std::string name(path);
  FILE* file = std::fopen(name.c_str(), "ab");
  if (file == nullptr) return false;
  gSink = file;
  gIsSinkOwned = true;
  return true;
}

void close() {
  if (gIsSinkOwned && gSink != nullptr) std::fclose(gSink);
  gSink = nullptr;
  gIsSinkOwned = false;
}

void write(Level level, std::string_view evt, std::initializer_list<Field> fields) {
  if (levelValue(level) < levelValue(gMinLevel)) return;
  writeLine(formatLine(nowMs(), level, evt, fields));
}

int levelValue(Level level) noexcept {
  switch (level) {
    case Level::trace:
      return kLevelTraceValue;
    case Level::debug:
      return kLevelDebugValue;
    case Level::info:
      return kLevelInfoValue;
    case Level::warn:
      return kLevelWarnValue;
    case Level::error:
      return kLevelErrorValue;
  }
  return kLevelInfoValue;
}

bool parseLevelName(std::string_view name, Level& out) noexcept {
  if (name == "trace" || name == "5") {
    out = Level::trace;
    return true;
  }
  if (name == "debug" || name == "10") {
    out = Level::debug;
    return true;
  }
  if (name == "info" || name == "20") {
    out = Level::info;
    return true;
  }
  if (name == "warn" || name == "warning" || name == "30") {
    out = Level::warn;
    return true;
  }
  if (name == "error" || name == "40") {
    out = Level::error;
    return true;
  }
  return false;
}

Level minLevel() noexcept { return gMinLevel; }

void setMinLevel(Level level) noexcept { gMinLevel = level; }

bool setMinLevelFromEnv(const char* value) noexcept {
  if (value == nullptr) return false;
  const std::string_view text(value);
  if (text.empty()) return false;
  Level parsed = Level::info;
  if (!parseLevelName(text, parsed)) return false;
  gMinLevel = parsed;
  return true;
}

void applyLogLevelFromEnv() {
  const char* value = std::getenv("AC_LOG_LEVEL");
  if (value != nullptr) setMinLevelFromEnv(value);
}

bool applyLogFileFromEnv() {
  const char* value = std::getenv("AC_LOG_FILE");
  if (value == nullptr || value[0] == '\0') return false;
  // 运维给的是「日志落到哪里」；目录不存在时按需创建，打不开就保持当前 sink（不静默丢日志）。
  const std::filesystem::path target(value);
  if (target.has_parent_path()) {
    std::error_code ignored;
    std::filesystem::create_directories(target.parent_path(), ignored);
  }
  return useFile(value);
}

DetailField DetailField::null(std::string_view key) {
  DetailField field(key, std::string_view{});
  field.kind = Kind::kNull;
  return field;
}

DetailField DetailField::array(std::string_view key, const std::string_view* items, std::size_t count) {
  DetailField field(key, std::string_view{});
  field.kind = Kind::kStrings;
  field.stringItems = items;
  field.stringItemCount = count;
  return field;
}

std::string formatEventLine(Level level, std::string_view evt, const EventContext& ctx,
                            std::initializer_list<DetailField> detail) {
  const std::size_t budget = kMaxLineBytes - std::strlen(kTruncatedTail);
  std::string line;
  line.reserve(256u);
  line += "{\"ts\":\"";
  line += formatTimestamp(ctx.ts);
  line += "\",\"level\":\"";
  line += levelName(level);
  line += "\",\"evt\":\"";
  // ts/level/evt 之外的冻结骨架（room/tick/pid/detail 的花括号与键名）先留出预算。
  const std::size_t skeleton = 96u;
  const std::size_t evtBudget = budget > line.size() + skeleton ? budget - line.size() - skeleton : 0u;
  bool isEvtComplete = true;
  ac::core::json::appendEscaped(line, evt, evtBudget, isEvtComplete);
  line += '"';
  bool isTruncated = !isEvtComplete;

  const auto emitNumber = [&line](const char* key, std::int64_t value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s%lld", key, static_cast<long long>(value));
    line += buf;
  };
  emitNumber(",\"room\":", static_cast<std::int64_t>(ctx.room));
  emitNumber(",\"tick\":", static_cast<std::int64_t>(ctx.tick));
  emitNumber(",\"pid\":", static_cast<std::int64_t>(ctx.pid));
  line += ",\"detail\":{";

  bool isFirst = true;
  for (const DetailField& field : detail) {
    if (isTruncated) break;
    const std::string text = "\"" + ac::core::json::escape(field.key) + "\":" + renderDetail(field);
    const std::size_t separator = isFirst ? 0u : 1u;
    if (line.size() + separator + text.size() + 1u > budget) {
      isTruncated = true;
      break;
    }
    if (!isFirst) line += ',';
    line += text;
    isFirst = false;
  }
  line += '}';
  line += isTruncated ? kTruncatedTail : "}";
  return line;
}

void event(Level level, std::string_view evt, const EventContext& ctx,
           std::initializer_list<DetailField> detail) {
  if (levelValue(level) < levelValue(gMinLevel)) return;
  EventContext resolved = ctx;
  if (resolved.ts == 0) resolved.ts = nowMs();
  writeLine(formatEventLine(level, evt, resolved, detail));
}

void reportUncaught(std::string_view what, const EventContext& ctx) {
  event(Level::error, "error.uncaught", ctx, {DetailField("what", what)});
}

std::size_t knownEventCount() noexcept { return kKnownEventCount; }

std::string_view knownEvent(std::size_t index) noexcept {
  return index < kKnownEventCount ? kKnownEvents[index] : std::string_view{};
}

bool isKnownEvent(std::string_view evt) noexcept {
  for (const std::string_view candidate : kKnownEvents) {
    if (candidate == evt) return true;
  }
  return false;
}

}  // namespace ac::log
