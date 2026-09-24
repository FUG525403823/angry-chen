#include "room/phase.hpp"

namespace ac::room {
namespace {

// §5.1 的转移表压成位图：bit i 置位表示允许转移到码 i。同态（own bit）一律不含。
constexpr uint8_t kAllowedTargets[kMatchPhaseCount] = {
    1u << 1,                // lobby        → loading
    1u << 2,                // loading      → playing
    (1u << 3) | (1u << 4),  // playing      → intermission | ended
    (1u << 2) | (1u << 4),  // intermission → playing | ended
    1u << 0,                // ended        → lobby
};

constexpr std::string_view kNames[kMatchPhaseCount] = {"lobby", "loading", "playing", "intermission",
                                                       "ended"};

}  // namespace

std::string_view matchPhaseName(MatchPhase phase) noexcept {
  const int32_t code = static_cast<int32_t>(phase);
  if (!isValidMatchPhase(code)) return "unknown";
  return kNames[code];
}

bool isValidMatchPhase(int32_t code) noexcept { return code >= 0 && code < kMatchPhaseCount; }

MatchPhase matchPhaseFromCode(int32_t code) noexcept {
  return isValidMatchPhase(code) ? static_cast<MatchPhase>(code) : MatchPhase::kLobby;
}

bool canMatchTransition(MatchPhase from, MatchPhase to) noexcept {
  const int32_t source = static_cast<int32_t>(from);
  const int32_t target = static_cast<int32_t>(to);
  if (!isValidMatchPhase(source) || !isValidMatchPhase(target)) return false;
  return (kAllowedTargets[source] & static_cast<uint8_t>(1u << target)) != 0u;
}

}  // namespace ac::room
