#include "core/json_text.hpp"

#include <cstdio>

namespace ac::core::json {

void appendEscaped(std::string& out, std::string_view text, std::size_t maxOut,
                   bool& isComplete) {
  isComplete = true;
  for (const char raw : text) {
    const auto value = static_cast<unsigned char>(raw);
    std::string piece;
    if (raw == '"') {
      piece = "\\\"";
    } else if (raw == '\\') {
      piece = "\\\\";
    } else if (value < 0x20u) {
      char buffer[8];
      std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned>(value));
      piece = buffer;
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

std::string escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8u);
  bool isComplete = true;
  appendEscaped(out, text, kNoLimit, isComplete);
  return out;
}

std::string quote(std::string_view text) { return "\"" + escape(text) + "\""; }

}  // namespace ac::core::json
