#include "room/session.hpp"

#include <cstring>

namespace ac::room {
namespace {

constexpr uint32_t kCodePointLessThanSpace = 0x20u;
constexpr uint32_t kCodePointDelete = 0x7fu;

bool isControlCodePoint(uint32_t code) noexcept {
  return code < kCodePointLessThanSpace || code == kCodePointDelete;
}

// v1 stripForbiddenNameChars：< > & " ' 一律剔除（不因它们整名拒绝）。
bool isForbiddenNameCodePoint(uint32_t code) noexcept {
  return code == 0x3cu || code == 0x3eu || code == 0x26u || code == 0x22u || code == 0x27u;
}

// v1 isAllowedNameChar：数字 / 大写 / 小写 / 下划线 + 三档 CJK。
bool isAllowedNameCodePoint(uint32_t code) noexcept {
  if (code >= 0x30u && code <= 0x39u) return true;
  if (code >= 0x41u && code <= 0x5au) return true;
  if (code >= 0x61u && code <= 0x7au) return true;
  if (code == 0x5fu) return true;
  if (code >= 0x3400u && code <= 0x4dbfu) return true;
  if (code >= 0x4e00u && code <= 0x9fffu) return true;
  if (code >= 0xf900u && code <= 0xfaffu) return true;
  return false;
}

// v1 String.prototype.trim() 的 ASCII 部分（名字表里没有其它可 trim 的字符）。
bool isTrimCodePoint(uint32_t code) noexcept {
  return code == 0x20u || code == 0x09u || code == 0x0au || code == 0x0du;
}

// 解码一个码点：返回消费的字节数（1..4），非法或截断返回 0。
std::size_t decodeCodePoint(const char* text, std::size_t size, uint32_t& out) noexcept {
  const auto byte = [&](std::size_t index) { return static_cast<uint8_t>(text[index]); };
  const uint8_t first = byte(0u);
  if (first < 0x80u) {
    out = first;
    return 1u;
  }
  std::size_t length = 0u;
  uint32_t code = 0u;
  uint32_t minimum = 0u;
  if (first >= 0xc2u && first <= 0xdfu) {
    length = 2u;
    code = first & 0x1fu;
    minimum = 0x80u;
  } else if (first >= 0xe0u && first <= 0xefu) {
    length = 3u;
    code = first & 0x0fu;
    minimum = 0x800u;
  } else if (first >= 0xf0u && first <= 0xf4u) {
    length = 4u;
    code = first & 0x07u;
    minimum = 0x10000u;
  } else {
    return 0u;
  }
  if (size < length) return 0u;
  for (std::size_t i = 1u; i < length; ++i) {
    const uint8_t next = byte(i);
    if ((next & 0xc0u) != 0x80u) return 0u;
    code = (code << 6u) | static_cast<uint32_t>(next & 0x3fu);
  }
  if (code < minimum || code > 0x10ffffu) return 0u;
  if (code >= 0xd800u && code <= 0xdfffu) return 0u;
  out = code;
  return length;
}

}  // namespace

Session createSession(uint32_t id, uint64_t nowMs) noexcept {
  Session session{};
  session.id = id;
  session.joinedAtMs = nowMs;
  return session;
}

bool isConnected(const Session& session) noexcept { return session.disconnectedAtMs < 0; }

bool isInRoom(const Session& session) noexcept { return session.roomCode[0] != '\0'; }

bool setSessionName(Session& session, std::string_view raw) noexcept {
  char filtered[ac::net::kNameMaxBytes + 1u] = {};
  std::size_t used = 0u;
  const char* const text = raw.data();
  std::size_t index = 0u;
  while (index < raw.size()) {
    uint32_t code = 0u;
    const std::size_t width = decodeCodePoint(text + index, raw.size() - index, code);
    if (width == 0u) return false;
    index += width;
    if (isControlCodePoint(code) || isForbiddenNameCodePoint(code)) continue;
    if (used + width > sizeof(filtered)) return false;  // 过滤后仍超 12 字节：直接拒绝
    std::memcpy(filtered + used, text + index - width, width);
    used += width;
  }
  // trim：按码点判 ASCII 空白，两端各收一次。
  std::size_t begin = 0u;
  std::size_t end = used;
  while (begin < end) {
    uint32_t code = 0u;
    const std::size_t width = decodeCodePoint(filtered + begin, end - begin, code);
    if (width == 0u || !isTrimCodePoint(code)) break;
    begin += width;
  }
  while (end > begin) {
    std::size_t start = end - 1u;
    while (start > begin && (static_cast<uint8_t>(filtered[start]) & 0xc0u) == 0x80u) --start;
    uint32_t code = 0u;
    const std::size_t width = decodeCodePoint(filtered + start, end - start, code);
    if (width == 0u || start + width != end || !isTrimCodePoint(code)) break;
    end = start;
  }
  const std::size_t length = end - begin;
  if (length < ac::net::kNameMinBytes || length > ac::net::kNameMaxBytes) return false;
  std::size_t cursor = begin;
  while (cursor < end) {
    uint32_t code = 0u;
    const std::size_t width = decodeCodePoint(filtered + cursor, end - cursor, code);
    if (width == 0u || !isAllowedNameCodePoint(code)) return false;
    cursor += width;
  }
  std::memmove(session.name, filtered + begin, length);
  session.name[length] = '\0';
  session.nameBytes = static_cast<uint8_t>(length);
  return true;
}

void clearSessionRoom(Session& session) noexcept {
  session.pid = 0u;
  session.roomCode[0] = '\0';
  session.ready = false;
  session.weaponApplied = false;
  session.disconnectedAtMs = -1;
}

}  // namespace ac::room
