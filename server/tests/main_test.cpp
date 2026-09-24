// S01 首套用例：日志契约（§5.4）与断言框架自身行为（§5.5）。
#include "tiny_test.hpp"

#include "core/log.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ac::log::Level;

// ---------- 极简 JSON 解析（只覆盖本框架自己产出的形态：扁平对象 + 标量值） ----------

struct JsonField {
  std::string key;
  std::string text;     // 值的原始文本（字符串值含引号）
  std::string decoded;  // 字符串值的解码结果
  bool isString = false;
};

struct JsonObject {
  bool isOk = false;
  std::vector<JsonField> fields;

  const JsonField* find(const std::string& key) const {
    for (const JsonField& field : fields) {
      if (field.key == key) return &field;
    }
    return nullptr;
  }

  std::string keyOrder() const {
    std::string out;
    for (const JsonField& field : fields) {
      if (!out.empty()) out += ',';
      out += field.key;
    }
    return out;
  }

  std::string stringValue(const std::string& key) const {
    const JsonField* field = find(key);
    return field != nullptr && field->isString ? field->decoded : std::string();
  }

  std::string rawText(const std::string& key) const {
    const JsonField* field = find(key);
    return field != nullptr ? field->text : std::string();
  }
};

bool parseJsonString(const std::string& text, std::size_t& pos, std::string& decoded,
                     std::string& raw) {
  if (pos >= text.size() || text[pos] != '"') return false;
  raw.clear();
  decoded.clear();
  raw.push_back('"');
  ++pos;
  while (pos < text.size()) {
    const char c = text[pos];
    ++pos;
    if (c == '"') return true;
    if (c != '\\') {
      raw.push_back(c);
      decoded.push_back(c);
      continue;
    }
    if (pos >= text.size()) return false;
    const char esc = text[pos];
    ++pos;
    raw.push_back('\\');
    raw.push_back(esc);
    switch (esc) {
      case '"': decoded.push_back('"'); break;
      case '\\': decoded.push_back('\\'); break;
      case '/': decoded.push_back('/'); break;
      case 'n': decoded.push_back('\n'); break;
      case 't': decoded.push_back('\t'); break;
      case 'r': decoded.push_back('\r'); break;
      case 'b': decoded.push_back('\b'); break;
      case 'f': decoded.push_back('\f'); break;
      case 'u': {
        if (pos + 4 > text.size()) return false;
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
          const char h = text[pos + i];
          code <<= 4;
          if (h >= '0' && h <= '9') {
            code |= static_cast<unsigned>(h - '0');
          } else if (h >= 'a' && h <= 'f') {
            code |= static_cast<unsigned>(h - 'a' + 10);
          } else if (h >= 'A' && h <= 'F') {
            code |= static_cast<unsigned>(h - 'A' + 10);
          } else {
            return false;
          }
          raw.push_back(h);
        }
        pos += 4;
        decoded.push_back(code < 0x80 ? static_cast<char>(code) : '?');
        break;
      }
      default:
        return false;
    }
  }
  return false;
}

bool parseValue(const std::string& text, std::size_t& pos, JsonField& field) {
  if (pos < text.size() && text[pos] == '"') {
    field.isString = true;
    return parseJsonString(text, pos, field.decoded, field.text);
  }
  const std::size_t start = pos;
  while (pos < text.size() && text[pos] != ',' && text[pos] != '}') ++pos;
  field.text = text.substr(start, pos - start);
  while (!field.text.empty() && (field.text.back() == ' ' || field.text.back() == '\n')) {
    field.text.pop_back();
  }
  return !field.text.empty();
}

JsonObject parseFlatObject(const std::string& line) {
  JsonObject object;
  std::size_t pos = 0;
  if (pos >= line.size() || line[pos] != '{') return object;
  ++pos;
  if (pos < line.size() && line[pos] == '}') {
    ++pos;
    if (pos == line.size()) object.isOk = true;
    return object;
  }
  while (true) {
    JsonField field;
    std::string raw;
    if (!parseJsonString(line, pos, field.key, raw)) return JsonObject{};
    if (pos >= line.size() || line[pos] != ':') return JsonObject{};
    ++pos;
    if (!parseValue(line, pos, field)) return JsonObject{};
    object.fields.push_back(field);
    if (pos >= line.size()) return JsonObject{};
    if (line[pos] == ',') {
      ++pos;
      continue;
    }
    if (line[pos] == '}') {
      ++pos;
      break;
    }
    return JsonObject{};
  }
  object.isOk = (pos == line.size());
  return object;
}

// ts = 0 的格式化行，便于逐字节比对。
std::string lineAtEpochZero(Level level, std::string_view evt,
                            std::initializer_list<ac::log::Field> fields) {
  return ac::log::formatLine(0, level, evt, fields);
}

}  // namespace

// ---------- §5.4 日志契约 ----------

AC_TEST(log_line_header_shape) {
  AC_CHECK_EQ(lineAtEpochZero(Level::info, "serverStarted", {}),
              std::string("{\"ts\":\"1970-01-01T00:00:00.000Z\",\"level\":\"info\","
                          "\"evt\":\"serverStarted\"}"));
}

AC_TEST(log_timestamp_format) {
  AC_CHECK_EQ(ac::log::formatLine(1234567890123LL, Level::debug, "tickAdvanced", {}),
              std::string("{\"ts\":\"2009-02-13T23:31:30.123Z\",\"level\":\"debug\","
                          "\"evt\":\"tickAdvanced\"}"));
  const std::string ts = parseFlatObject(lineAtEpochZero(Level::info, "x", {})).stringValue("ts");
  AC_CHECK_EQ(ts.size(), std::size_t{24});
  AC_CHECK_EQ(ts.substr(4, 1), std::string("-"));
  AC_CHECK_EQ(ts.substr(10, 1), std::string("T"));
  AC_CHECK_EQ(ts.substr(19, 1), std::string("."));
  AC_CHECK_EQ(ts.substr(23, 1), std::string("Z"));
}

AC_TEST(log_tick_room_slots) {
  const JsonObject object = parseFlatObject(lineAtEpochZero(
      Level::warn, "clientDisconnected", {{"room", "ABCD"}, {"code", 7}, {"tick", 42}}));
  AC_CHECK(object.isOk);
  AC_CHECK_EQ(object.keyOrder(), std::string("ts,level,evt,tick,room,code"));
  AC_CHECK_EQ(object.rawText("tick"), std::string("42"));
  AC_CHECK_EQ(object.stringValue("room"), std::string("ABCD"));
  AC_CHECK_EQ(object.rawText("code"), std::string("7"));
}

AC_TEST(log_extra_field_insertion_order) {
  const JsonObject object =
      parseFlatObject(lineAtEpochZero(Level::trace, "matchStarted", {{"z", 1}, {"a", 2}, {"m", 3}}));
  AC_CHECK_EQ(object.keyOrder(), std::string("ts,level,evt,z,a,m"));
}

AC_TEST(log_string_escaping) {
  AC_CHECK_EQ(lineAtEpochZero(Level::info, "quoted", {{"s", "a\"b\\c"}}),
              std::string("{\"ts\":\"1970-01-01T00:00:00.000Z\",\"level\":\"info\",\"evt\":"
                          "\"quoted\",\"s\":\"a\\\"b\\\\c\"}"));
}

AC_TEST(log_control_char_escaping) {
  const std::string line = lineAtEpochZero(Level::info, "x", {{"s", std::string("\x01\x0a\x1f")}});
  AC_CHECK_EQ(line, std::string("{\"ts\":\"1970-01-01T00:00:00.000Z\",\"level\":\"info\","
                                "\"evt\":\"x\",\"s\":\"\\u0001\\u000A\\u001F\"}"));
  AC_CHECK_EQ(parseFlatObject(line).stringValue("s"), std::string("\x01\x0a\x1f"));
}

AC_TEST(log_double_format) {
  const std::string line = lineAtEpochZero(
      Level::info, "x", {{"tenth", 0.1}, {"third", 1.0 / 3.0}, {"neg", -2.5}, {"whole", 2.0}});
  const JsonObject object = parseFlatObject(line);
  AC_CHECK_EQ(object.rawText("tenth"), std::string("0.10000000000000001"));
  AC_CHECK_EQ(object.rawText("third"), std::string("0.33333333333333331"));
  AC_CHECK_EQ(object.rawText("neg"), std::string("-2.5"));
  AC_CHECK_EQ(object.rawText("whole"), std::string("2"));
}

AC_TEST(log_non_finite_double) {
  const std::string line = lineAtEpochZero(Level::info, "x", {{"nan", std::nan("")},
                                                             {"inf", HUGE_VAL},
                                                             {"ninf", -HUGE_VAL}});
  const JsonObject object = parseFlatObject(line);
  AC_CHECK_EQ(object.rawText("nan"), std::string("null"));
  AC_CHECK_EQ(object.rawText("inf"), std::string("null"));
  AC_CHECK_EQ(object.rawText("ninf"), std::string("null"));
}

AC_TEST(log_integer_and_bool_values) {
  const std::string line =
      lineAtEpochZero(Level::info, "x",
                      {{"i64", std::int64_t{-42}},
                       {"u64", static_cast<std::uint64_t>(18446744073709551615ULL)},
                       {"yes", true},
                       {"no", false},
                       {"small", 7}});
  const JsonObject object = parseFlatObject(line);
  AC_CHECK_EQ(object.rawText("i64"), std::string("-42"));
  AC_CHECK_EQ(object.rawText("u64"), std::string("18446744073709551615"));
  AC_CHECK_EQ(object.rawText("yes"), std::string("true"));
  AC_CHECK_EQ(object.rawText("no"), std::string("false"));
  AC_CHECK_EQ(object.rawText("small"), std::string("7"));
}

AC_TEST(log_line_length_cap) {
  const std::string line =
      lineAtEpochZero(Level::info, "x",
                      {{"k01", std::string(400, 'a')}, {"k02", std::string(400, 'b')},
                       {"k03", std::string(400, 'c')}, {"k04", std::string(400, 'd')},
                       {"k05", std::string(400, 'e')}, {"k06", std::string(400, 'f')},
                       {"k07", std::string(400, 'g')}, {"k08", std::string(400, 'h')},
                       {"k09", std::string(400, 'i')}, {"k10", std::string(400, 'j')},
                       {"k11", std::string(400, 'k')}, {"k12", std::string(400, 'l')}});
  AC_CHECK(line.size() <= 4096);
  AC_CHECK(line.size() > 3000);  // 截断点在首个放不下的字段，尾部最多留一个字段的宽度
  AC_CHECK_EQ(line.substr(line.size() - 18), std::string(",\"truncated\":true}"));
  const JsonObject object = parseFlatObject(line);
  AC_CHECK(object.isOk);
  AC_CHECK_EQ(object.keyOrder().substr(0, 11), std::string("ts,level,ev"));
  AC_CHECK_EQ(object.rawText("truncated"), std::string("true"));
  AC_CHECK_EQ(object.stringValue("k01").size(), std::size_t{400});
  AC_CHECK(!object.stringValue("k09").empty());
  AC_CHECK_EQ(object.stringValue("k12"), std::string());
}

// 用例名不能含 "size"/"match" 等子串：--filter 是全局子串匹配，会污染后续计划的门禁计数
// （S03 §6 的 --filter=size / --filter=match）。
AC_TEST(log_overlong_evt_truncated) {
  const std::string line = lineAtEpochZero(Level::info, std::string(5000, 'e'), {});
  AC_CHECK(line.size() <= 4096);
  AC_CHECK(line.size() > 4000);
  AC_CHECK_EQ(line.substr(line.size() - 18), std::string(",\"truncated\":true}"));
  const JsonObject object = parseFlatObject(line);
  AC_CHECK(object.isOk);
  AC_CHECK_EQ(object.rawText("truncated"), std::string("true"));
  AC_CHECK(object.stringValue("evt").size() < 5000);
}

AC_TEST(log_file_sink_roundtrip) {
  const std::string path = "ac_log_sink_test.log";
  std::remove(path.c_str());
  AC_CHECK(ac::log::useFile(path));
  ac::log::write(Level::warn, "clientDisconnected", {{"room", "ZZZZ"}, {"code", 7}});
  ac::log::close();
  ac::log::useStderr();

  std::ifstream in(path, std::ios::binary);
  const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  std::remove(path.c_str());

  AC_CHECK(!content.empty() && content.back() == '\n');
  const std::size_t end = content.empty() ? 0 : content.find('\n');
  const JsonObject object = parseFlatObject(content.substr(0, end));
  AC_CHECK(object.isOk);
  AC_CHECK_EQ(object.keyOrder(), std::string("ts,level,evt,room,code"));
  AC_CHECK_EQ(object.stringValue("level"), std::string("warn"));
  AC_CHECK_EQ(object.stringValue("evt"), std::string("clientDisconnected"));
  AC_CHECK_EQ(object.stringValue("room"), std::string("ZZZZ"));
  AC_CHECK_EQ(object.rawText("code"), std::string("7"));
  AC_CHECK_EQ(object.stringValue("ts").size(), std::size_t{24});
}

AC_TEST(log_parse_own_line) {
  const std::string line = lineAtEpochZero(
      Level::error, "clientDisconnected",
      {{"tick", 123}, {"room", "ABCD"}, {"reason", "timeout\"x"},
       {"bytes", static_cast<std::uint64_t>(4096)}, {"ratio", 0.5}, {"slow", true}});
  const JsonObject object = parseFlatObject(line);
  AC_CHECK(object.isOk);
  AC_CHECK_EQ(object.keyOrder(), std::string("ts,level,evt,tick,room,reason,bytes,ratio,slow"));
  AC_CHECK_EQ(object.stringValue("level"), std::string("error"));
  AC_CHECK_EQ(object.stringValue("evt"), std::string("clientDisconnected"));
  AC_CHECK_EQ(object.stringValue("reason"), std::string("timeout\"x"));
  AC_CHECK_EQ(object.rawText("bytes"), std::string("4096"));
  AC_CHECK_EQ(object.rawText("ratio"), std::string("0.5"));
  AC_CHECK_EQ(object.rawText("slow"), std::string("true"));
}

AC_TEST(log_level_names) {
  AC_CHECK_EQ(std::string(ac::log::levelName(Level::trace)), std::string("trace"));
  AC_CHECK_EQ(std::string(ac::log::levelName(Level::debug)), std::string("debug"));
  AC_CHECK_EQ(std::string(ac::log::levelName(Level::info)), std::string("info"));
  AC_CHECK_EQ(std::string(ac::log::levelName(Level::warn)), std::string("warn"));
  AC_CHECK_EQ(std::string(ac::log::levelName(Level::error)), std::string("error"));
}

// ---------- §5.5 断言框架自身行为 ----------

AC_TEST(test_result_line_format) {
  AC_CHECK_EQ(ac::test::formatPassLine("log_line_length_cap"),
              std::string("PASS log_line_length_cap"));
  AC_CHECK_EQ(ac::test::formatFailLine("log_x", "server/tests/main_test.cpp", 42, "a == b"),
              std::string("FAIL log_x: server/tests/main_test.cpp:42: a == b"));
  AC_CHECK_EQ(ac::test::formatSummaryLine(3, 4), std::string("TESTS 3/4"));
}

AC_TEST(test_filter_selection) {
  AC_CHECK(ac::test::matchesFilter("log_double_format", ""));
  AC_CHECK(ac::test::matchesFilter("log_double_format", "log_"));
  AC_CHECK(ac::test::matchesFilter("log_double_format", "double"));
  AC_CHECK(!ac::test::matchesFilter("log_double_format", "test_"));
  AC_CHECK(ac::test::matchesFilter("log_double_format", nullptr));
}

AC_TEST(test_current_case_name) {
  AC_CHECK_EQ(std::string(ac::test::currentCase()), std::string("test_current_case_name"));
}

AC_TEST(test_check_macros) {
  AC_CHECK(1 + 1 == 2);
  AC_CHECK_EQ(std::string("ab"), "ab");
  AC_CHECK_NEAR(0.1 + 0.2, 0.3, 1e-9);
  AC_CHECK_NEAR(1.0 / 3.0 * 3.0, 1.0, 1e-15);
}

int main(int argc, char** argv) { return ac::test::runAll(argc, argv); }
