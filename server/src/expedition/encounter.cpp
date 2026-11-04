#include "expedition/encounter.hpp"
namespace ac::expedition {
void beginEncounter(EncounterState& state) noexcept { state=EncounterState{}; }
void stepEncounter(EncounterState& state,uint32_t dtMs,bool defended,uint32_t invaders) noexcept {
  if(dtMs!=50u || state.result!=EncounterResult::kNone) return;
  const uint64_t damage=static_cast<uint64_t>(invaders)*2u*dtMs;
  state.hpMilli=damage>=state.hpMilli?0u:state.hpMilli-static_cast<uint32_t>(damage);
  state.remainingMs=state.remainingMs>dtMs?state.remainingMs-dtMs:0u;
  if(defended) state.progressMs+=dtMs;
  if(state.hpMilli==0u) state.result=EncounterResult::kFacilityDestroyed;
  else if(state.progressMs>=180000u) state.result=EncounterResult::kSuccess;
  else if(state.remainingMs==0u) state.result=EncounterResult::kTimeout;
}
}
