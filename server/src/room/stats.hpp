#pragma once
// S10 §5.6：每人统计累加器与结算记录组装（逐事件驱动，跨 tick 不重放）。
#include <cstdint>

#include "net/codec.hpp"

namespace ac::room {

inline constexpr int32_t kMaxPlayersPerRoom = 4;
inline constexpr std::size_t kNameBufferBytes = ac::net::kNameMaxBytes + 1u;  // 12 + NUL
inline constexpr std::size_t kMatchIdMaxBytes = 64u;

// v1 match/stats.ts 的 PlayerMatchStats。
struct PlayerStats {
  uint32_t kills = 0u;
  uint32_t headshots = 0u;
  uint32_t shotsFired = 0u;
  uint32_t hits = 0u;
  uint32_t revives = 0u;
  uint32_t downs = 0u;
  uint64_t aliveMs = 0u;
  uint64_t downedMs = 0u;
};

// v1 match/controller.ts 的 MatchPlayerRecord。
struct PlayerRecord {
  uint32_t pid = 0u;
  char name[kNameBufferBytes] = {};
  uint8_t nameBytes = 0u;
  PlayerStats stats{};
  bool leftMidMatch = false;
};

// v1 report.ts 的 MatchCounters（本份只用得上这几个）。
struct MatchCounters {
  uint32_t ticks = 0u;
  uint32_t deniedTransitions = 0u;  // §5.1 的 matchTransitionDenied 警告计数
  uint32_t graceTimeouts = 0u;
  uint32_t graceReconnects = 0u;
  uint32_t skipped = 0u;  // 追帧上限丢弃的 tick
  uint32_t peakPlayers = 0u;
  uint32_t peakEntities = 0u;
  uint32_t matchesStarted = 0u;
  uint32_t matchesEnded = 0u;
};

struct PlayerResult {
  char name[kNameBufferBytes] = {};
  uint8_t nameBytes = 0u;
  uint32_t kills = 0u;
  uint32_t headshots = 0u;
  uint32_t shotsFired = 0u;
  uint32_t hits = 0u;
  uint32_t revives = 0u;
  uint32_t downs = 0u;
  uint64_t aliveMs = 0u;
  bool leftMidMatch = false;
};

// §5.6 的结算记录：matchId = roomCode + '-' + startedAtMs。
struct MatchResultRecord {
  char matchId[kMatchIdMaxBytes] = {};
  uint64_t startedAtMs = 0u;
  uint64_t durationMs = 0u;
  int32_t waveReached = 0;
  uint8_t winnerTeam = 0u;
  uint8_t playerCount = 0u;
  PlayerResult players[kMaxPlayersPerRoom] = {};
};

void resetPlayerStats(PlayerStats& stats) noexcept;
void noteKill(PlayerStats& stats, bool headshot) noexcept;
void noteShot(PlayerStats& stats) noexcept;
void noteHit(PlayerStats& stats) noexcept;
void noteDown(PlayerStats& stats) noexcept;
void noteRevive(PlayerStats& stats) noexcept;
void accumulateAlive(PlayerStats& stats, uint32_t dtMs) noexcept;
void accumulateDowned(PlayerStats& stats, uint32_t dtMs) noexcept;

double accuracyOf(const PlayerStats& stats) noexcept;
uint64_t survivalMsOf(const PlayerStats& stats, uint64_t matchDurationMs) noexcept;

// 写入 matchId（roomCode + '-' + 十进制 startedAtMs），返回字节数（不含 NUL）。
std::size_t writeMatchId(char* out, std::size_t capacity, const char* roomCode,
                         uint64_t startedAtMs) noexcept;

}  // namespace ac::room
