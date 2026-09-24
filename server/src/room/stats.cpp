#include "room/stats.hpp"

#include <cstring>

namespace ac::room {
namespace {

// 十进制写入 [first, last) 的低位在 last-1，返回新的 last。
char* writeDecimalBackwards(char* last, uint64_t value) noexcept {
  if (value == 0u) {
    *(--last) = '0';
    return last;
  }
  while (value > 0u) {
    *(--last) = static_cast<char>('0' + static_cast<char>(value % 10u));
    value /= 10u;
  }
  return last;
}

}  // namespace

void resetPlayerStats(PlayerStats& stats) noexcept { stats = PlayerStats{}; }

void noteKill(PlayerStats& stats, bool headshot) noexcept {
  stats.kills += 1u;
  if (headshot) stats.headshots += 1u;
}

void noteShot(PlayerStats& stats) noexcept { stats.shotsFired += 1u; }
void noteHit(PlayerStats& stats) noexcept { stats.hits += 1u; }
void noteDown(PlayerStats& stats) noexcept { stats.downs += 1u; }
void noteRevive(PlayerStats& stats) noexcept { stats.revives += 1u; }

void accumulateAlive(PlayerStats& stats, uint32_t dtMs) noexcept { stats.aliveMs += dtMs; }
void accumulateDowned(PlayerStats& stats, uint32_t dtMs) noexcept { stats.downedMs += dtMs; }

double accuracyOf(const PlayerStats& stats) noexcept {
  const uint32_t shots = stats.shotsFired > 0u ? stats.shotsFired : 1u;
  return static_cast<double>(stats.hits) / static_cast<double>(shots);
}

uint64_t survivalMsOf(const PlayerStats& stats, uint64_t matchDurationMs) noexcept {
  return matchDurationMs > stats.downedMs ? matchDurationMs - stats.downedMs : 0u;
}

std::size_t writeMatchId(char* out, std::size_t capacity, const char* roomCode,
                         uint64_t startedAtMs) noexcept {
  if (out == nullptr || capacity == 0u) return 0u;
  std::size_t used = 0u;
  if (roomCode != nullptr) {
    while (roomCode[used] != '\0' && used + 1u < capacity) {
      out[used] = roomCode[used];
      ++used;
    }
  }
  if (used + 1u < capacity) out[used++] = '-';
  char digits[24] = {};
  char* const begin = writeDecimalBackwards(digits + sizeof(digits), startedAtMs);
  const std::size_t count = static_cast<std::size_t>((digits + sizeof(digits)) - begin);
  for (std::size_t i = 0u; i < count && used + 1u < capacity; ++i) out[used++] = begin[i];
  out[used] = '\0';
  return used;
}

}  // namespace ac::room
