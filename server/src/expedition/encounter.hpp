#pragma once
#include <cstdint>
namespace ac::expedition {
enum class EncounterResult : uint8_t { kNone=0,kSuccess=1,kFacilityDestroyed=2,kTimeout=3 };
struct EncounterState {
  uint32_t hpMilli=1000000u;
  uint32_t progressMs=0u;
  uint32_t remainingMs=240000u;
  EncounterResult result=EncounterResult::kNone;
};
void beginEncounter(EncounterState& state) noexcept;
void stepEncounter(EncounterState& state,uint32_t dtMs,bool defended,uint32_t invaders) noexcept;
}
