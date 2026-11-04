#pragma once
#include <cstdint>
#include "core/rng.hpp"

namespace ac::expedition {
enum class Stage : uint8_t { kIdle=0, kReward=1, kCombat=2, kEnded=3 };
enum class Result : uint8_t { kNone=0, kSuccess=1, kFacilityDestroyed=2, kTimeout=3, kAllDowned=4 };
struct RunPlayer {
  uint16_t pid=0u;
  uint8_t cardMask=0u;
  uint32_t offerId=0u;
  uint8_t choices[3]={};
  bool resolved=false;
  bool connected=true;
  uint8_t rerollsLeft=1u;
  uint32_t lastRequestId=0u;
  uint8_t lastResult=0u;
  uint32_t lastRunId=0u;
  uint32_t lastOfferId=0u;
  uint8_t lastOp=0u;
  uint8_t lastChoice=0u;
  bool supplyPending=false;
};
struct RunState {
  bool enabled=false;
  uint32_t runId=0u;
  uint32_t revision=0u;
  uint32_t clockMs=0u;
  Stage stage=Stage::kIdle;
  uint8_t node=0u;
  Result result=Result::kNone;
  uint16_t objectiveHp=1000u;
  uint32_t progressMs=0u;
  uint32_t combatLeftMs=240000u;
  uint32_t rewardDeadlineMs=0u;
  RunPlayer players[4]={};
  uint8_t playerCount=0u;
  uint32_t nextOfferId=1u;
  ac::Rng rewardRng{};
};
void beginRun(RunState& run,uint32_t runId,uint32_t seed,const uint16_t* ids,uint8_t count) noexcept;
void tickRun(RunState& run,uint32_t dtMs) noexcept;
void finishEncounter(RunState& run,Result result) noexcept;
uint8_t request(RunState& run,uint16_t pid,uint32_t runId,uint32_t offerId,uint32_t requestId,uint8_t op,uint8_t choice) noexcept;
}
