#include "core/log.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace ac::log {
namespace {

constexpr std::size_t kMaxLineBytes = 4096;
constexpr const char* kTruncatedTail = ",\"truncated\":true}";

FILE* gSink = nullptr;  // nullptr 表示 stderr
bool gIsSinkOwned = false;

FILE* sink() { return gSink == nullptr ? stderr : gSink; }

// 唯一的转义规则：把 s 转义后追加到 out。写满 maxOut 字节即停并把 isComplete 置 false，
// 因此不会截在转义序列中间。
void escapeInto(std::string& out, std::string_view s, std::size_t maxOut, bool& isComplete) {
  isComplete = true;
  for (const char raw : s) {
    const unsigned char c = static_cast<unsigned char>(raw);
    std::string piece;
    if (c == '"') {
      piece = "\\\"";
    } else if (c == '\\') {
      piece = "\\\\";
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
      piece = buf;
    } else {
      piece.push_back(raw);
    }
    if (out.size() + piece.size() > maxOut) {
      isComplete = false;
      return;
    }
    out += piece;
  }
}

std::string escapeAll(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  bool isComplete = true;
  escapeInto(out, s, std::numeric_limits<std::size_t>::max(), isComplete);
  return out;
}

std::string renderValue(const FieldValue& value) {
  return std::visit(
      [](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::string_view>) {
          return "\"" + escapeAll(arg) + "\"";
        } else if constexpr (std::is_same_v<T, std::int64_t>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, std::uint64_t>) {
          return std::to_string(arg);
        } else if constexpr (std::is_same_v<T, double>) {
          if (!std::isfinite(arg)) return std::string("null");
          char buf[40];
          std::snprintf(buf, sizeof(buf), "%.17g", arg);
          return std::string(buf);
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
  escapeInto(line, evt, evtBudget, isEvtComplete);
  line += '"';
  bool isTruncated = !isEvtComplete;

  const auto emit = [&](const Field& field) {
    if (isTruncated) return;
    const std::string text = "\"" + escapeAll(field.key) + "\":" + renderValue(field.value);
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
  const std::string line = formatLine(nowMs(), level, evt, fields);
  FILE* out = sink();
  std::fwrite(line.data(), 1, line.size(), out);
  std::fputc('\n', out);
  std::fflush(out);
}

}  // namespace ac::log
