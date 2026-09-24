#pragma once
// 结构化 JSON 行日志（S01 §5.4 冻结契约）。
//
// 字段顺序固定为 ts,level,evt,tick,room,<附加键按插入序>：调用点把 tick / room 当成普通
// Field 传入即可，序列化时它们总被提到第 4、5 位。
// 字符串转义 " -> \" 、\ -> \\ 、控制字符 -> \u00XX（大写十六进制）；double 用 %.17g，
// 非有限值输出 null；单行 ≤ 4096 字节，从第一个放不下的字段起其后字段全部丢弃（不留空洞），
// 并在末尾追加 "truncated":true。

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace ac::log {

enum class Level { trace, debug, info, warn, error };

// 字段取值只有这五类：整数一律收敛到 int64/uint64，bool 单独一支。
using FieldValue = std::variant<std::string_view, std::int64_t, std::uint64_t, double, bool>;

struct Field {
  std::string_view key;
  FieldValue value;

  Field(std::string_view k, std::string_view v) : key(k), value(v) {}
  Field(std::string_view k, const char* v) : key(k), value(std::string_view(v)) {}
  Field(std::string_view k, bool v) : key(k), value(v) {}
  Field(std::string_view k, double v) : key(k), value(v) {}

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
  Field(std::string_view k, T v)
      : key(k),
        value(std::is_signed_v<T> ? FieldValue(static_cast<std::int64_t>(v))
                                  : FieldValue(static_cast<std::uint64_t>(v))) {}
};

const char* levelName(Level level) noexcept;

// 纯序列化（不碰 sink）：测试与需要固定时间戳的调用点用它。
std::string formatLine(std::int64_t epochMs, Level level, std::string_view evt,
                       std::initializer_list<Field> fields = {});

// sink：默认 stderr；useFile 以追加方式打开；close 只关闭文件 sink。
void useStderr();
void useStdout();
bool useFile(std::string_view path);
void close();

// 写一行到当前 sink，ts 取系统 UTC 时钟。
void write(Level level, std::string_view evt, std::initializer_list<Field> fields = {});

// ---- S13 §5 的事件行契约 -------------------------------------------------------------
// 字段固定为 { ts, level, evt, room, tick, pid, detail }；detail 是扁平键值，取值只有
// string / number / bool / null / string[] 五类。级别数值（debug=10 … error=40）只当过滤
// 阈值用；level 字段沿用 S01 的名字（§6-6 只要求 ts/level/evt 可被 JSON 解析）。
inline constexpr int kLevelTraceValue = 5;
inline constexpr int kLevelDebugValue = 10;
inline constexpr int kLevelInfoValue = 20;
inline constexpr int kLevelWarnValue = 30;
inline constexpr int kLevelErrorValue = 40;

int levelValue(Level level) noexcept;
bool parseLevelName(std::string_view name, Level& out) noexcept;  // "warn" 与 "30" 都认

Level minLevel() noexcept;
void setMinLevel(Level level) noexcept;
bool setMinLevelFromEnv(const char* value) noexcept;  // 未识别返回 false 且不改动当前阈值
void applyLogLevelFromEnv();                          // 读 AC_LOG_LEVEL（空/未设则保持默认 info）
bool applyLogFileFromEnv();  // 读 AC_LOG_FILE 并切到文件 sink（空/未设或打不开则保持当前 sink）

struct EventContext {
  std::int64_t ts = 0;   // 0 = 取系统 UTC 时钟
  std::uint32_t room = 0u;
  std::uint32_t tick = 0u;
  std::int32_t pid = 0;  // 0 = 与该玩家无关
};

struct DetailField {
  enum class Kind : std::uint8_t { kString, kInt, kUint, kDouble, kBool, kNull, kStrings };

  std::string_view key;
  Kind kind = Kind::kString;
  std::string_view text{};
  std::int64_t intValue = 0;
  std::uint64_t uintValue = 0u;
  double doubleValue = 0.0;
  bool boolValue = false;
  const std::string_view* stringItems = nullptr;
  std::size_t stringItemCount = 0u;

  DetailField(std::string_view k, std::string_view v) : key(k), text(v) {}
  DetailField(std::string_view k, const char* v) : key(k), text(std::string_view(v)) {}
  DetailField(std::string_view k, bool v) : key(k), kind(Kind::kBool), boolValue(v) {}
  DetailField(std::string_view k, double v) : key(k), kind(Kind::kDouble), doubleValue(v) {}

  template <typename T, typename = std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>>
  DetailField(std::string_view k, T v)
      : key(k),
        kind(std::is_signed_v<T> ? Kind::kInt : Kind::kUint),
        intValue(std::is_signed_v<T> ? static_cast<std::int64_t>(v) : 0),
        uintValue(std::is_signed_v<T> ? 0u : static_cast<std::uint64_t>(v)) {}

  static DetailField null(std::string_view k);
  static DetailField array(std::string_view k, const std::string_view* items, std::size_t count);
};

// 纯序列化（不碰 sink，且不按阈值过滤）：单行 ≤ 4096 字节，从第一个放不下的 detail 起丢弃
// 其后字段（不留空洞）并追加 "truncated":true。
std::string formatEventLine(Level level, std::string_view evt, const EventContext& ctx,
                            std::initializer_list<DetailField> detail = {});

// 按阈值过滤后写一行（ctx.ts == 0 时取系统 UTC 时钟）。
void event(Level level, std::string_view evt, const EventContext& ctx,
           std::initializer_list<DetailField> detail = {});

// §5「频率纪律」：未捕获异常必须落 error.uncaught，且写日志的路径不能因此失效。
void reportUncaught(std::string_view what, const EventContext& ctx = {});

// §5 G8：进程内已落盘的 error.uncaught 条数（单线程使用，性能门禁直接读它）。
unsigned uncaughtCount() noexcept;

// §5 的事件名最小集（名字即契约，新增名要同步改清单与断言）。
std::size_t knownEventCount() noexcept;
std::string_view knownEvent(std::size_t index) noexcept;
bool isKnownEvent(std::string_view evt) noexcept;

}  // namespace ac::log
