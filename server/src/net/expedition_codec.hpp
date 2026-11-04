#pragma once
#include "net/codec.hpp"
#include <array>
namespace ac::net {
inline constexpr std::size_t kExpeditionRequestPayloadBytes = 18;
inline constexpr std::size_t kExpeditionStateFixedBytes = 52;
struct ExpeditionRequest {
  uint32_t runId{}, offerId{}, requestId{};
  uint8_t op{}, choiceId{};
  uint32_t reserved{};
};
struct ExpeditionPlayerState {
  uint16_t pid{};
  uint8_t cardMask{};
  uint32_t toolReadyAtMs{}, triggerCount{};
};
struct ExpeditionState {
  uint8_t schema{1}, mode{};
  uint32_t runId{}, revision{}, clockMs{};
  uint8_t stage{}, node{}, result{}, layoutId{1}, objectiveId{1}, toolId{1};
  uint16_t objectiveHp{};
  uint32_t progressMs{}, combatLeftMs{}, rewardDeadlineMs{};
  uint16_t localPid{};
  uint32_t offerId{};
  uint8_t offerCount{};
  std::array<uint8_t, 3> choices{};
  uint8_t resolved{}, rerollsLeft{};
  uint32_t lastRequestId{};
  uint8_t lastResult{}, playerCount{};
  std::array<ExpeditionPlayerState, 4> players{};
};
EncodeResult encodeExpeditionRequest(const PacketHeader&, const ReliableExt&, const ExpeditionRequest&, uint8_t*, std::size_t) noexcept;
DecodeResult<ExpeditionRequest> decodeExpeditionRequest(const uint8_t*, std::size_t) noexcept;
EncodeResult encodeExpeditionState(const PacketHeader&, const ReliableExt&, const ExpeditionState&, uint8_t*, std::size_t) noexcept;
DecodeResult<ExpeditionState> decodeExpeditionState(const uint8_t*, std::size_t) noexcept;
} // namespace ac::net
