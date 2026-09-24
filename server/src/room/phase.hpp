#pragma once
// S10 §5.1：对局阶段枚举、名字表与 5×5 转移校验。阶段只允许沿冻结表前进。
#include <cstdint>
#include <string_view>

namespace ac::room {

inline constexpr int32_t kMatchPhaseCount = 5;

// 码与 v1 match/phase.ts 的 MATCH_PHASE 逐字一致（0..4）。
enum class MatchPhase : uint8_t {
  kLobby = 0,
  kLoading = 1,
  kPlaying = 2,
  kIntermission = 3,
  kEnded = 4,
};

// 与 v1 MATCH_PHASE_NAMES 同序；越界返回 "unknown"（不返回空指针）。
std::string_view matchPhaseName(MatchPhase phase) noexcept;

bool isValidMatchPhase(int32_t code) noexcept;
MatchPhase matchPhaseFromCode(int32_t code) noexcept;

// §5.1：7 组合法、18 组非法（含 5 组同态 = x → x 一律非法）。越界码一律 false。
bool canMatchTransition(MatchPhase from, MatchPhase to) noexcept;

}  // namespace ac::room
