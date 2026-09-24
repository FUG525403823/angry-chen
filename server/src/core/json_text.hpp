#pragma once
// 仓库唯一一份单行 JSON 字符串转义（口径 = S01 §5.4）：" -> \" 、\ -> \\ 、控制字符 -> \u00XX
// （大写十六进制）；其余字节原样透传（UTF-8 不进 \u 形式）。日志行、战绩行、诊断报告共用这一份，
// 避免同一仓里出现第二套转义规则。
#include <cstddef>
#include <string>
#include <string_view>

namespace ac::core::json {

inline constexpr std::size_t kNoLimit = static_cast<std::size_t>(-1);

// 把 text 转义后追加到 out；写满 maxOut 字节即停并把 isComplete 置 false（不会截在转义序列中间）。
void appendEscaped(std::string& out, std::string_view text, std::size_t maxOut, bool& isComplete);

std::string escape(std::string_view text);   // 只转义，不加引号
std::string quote(std::string_view text);    // 转义并加两侧引号

}  // namespace ac::core::json
