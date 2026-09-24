#pragma once
// S02 §3/§5.4：CRC32C（反射多项式 0x82F63B78），fixture 与角度表校验的唯一散列。
//
// 两个入口：crc32c（单次）与 crc32cExtend（分段；把上一段的结果喂给下一段，等价于拼接后单次计算）。

#include <cstddef>
#include <cstdint>

namespace ac {

namespace detail {

struct Crc32cTable {
  uint32_t values[256];
  constexpr Crc32cTable() noexcept : values{} {
    for (uint32_t i = 0; i < 256u; ++i) {
      uint32_t c = i;
      for (int bit = 0; bit < 8; ++bit) {
        c = (c & 1u) != 0u ? (0x82F63B78u ^ (c >> 1)) : (c >> 1);
      }
      values[i] = c;
    }
  }
};

inline constexpr Crc32cTable kCrc32cTable{};

inline uint32_t crc32cRaw(uint32_t crc, const void* data, std::size_t len) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  for (std::size_t i = 0; i < len; ++i) {
    crc = kCrc32cTable.values[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
  }
  return crc;
}

}  // namespace detail

inline uint32_t crc32c(const void* data, std::size_t len) noexcept {
  return detail::crc32cRaw(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

inline uint32_t crc32cExtend(uint32_t crc, const void* data, std::size_t len) noexcept {
  return detail::crc32cRaw(crc ^ 0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

}  // namespace ac
