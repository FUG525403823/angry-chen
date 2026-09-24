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

}  // namespace ac::log
