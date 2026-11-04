#include "tiny_test.hpp"
#include "expedition/encounter.hpp"
AC_TEST(expedition_encounter_terminal_priority_and_fixed_step) {
  ac::expedition::EncounterState state{};
  state.progressMs=179950u;
  state.hpMilli=100u;
  ac::expedition::stepEncounter(state,49u,true,1u);
  AC_CHECK_EQ(state.progressMs,179950u);
  ac::expedition::stepEncounter(state,50u,true,1u);
  AC_CHECK_EQ(state.result,ac::expedition::EncounterResult::kFacilityDestroyed);
  ac::expedition::beginEncounter(state);
  for(unsigned i=0u;i<4800u;++i) ac::expedition::stepEncounter(state,50u,false,0u);
  AC_CHECK_EQ(state.result,ac::expedition::EncounterResult::kTimeout);
  ac::expedition::beginEncounter(state);
  for(unsigned i=0u;i<3600u;++i) ac::expedition::stepEncounter(state,50u,true,0u);
  AC_CHECK_EQ(state.result,ac::expedition::EncounterResult::kSuccess);
}

AC_TEST(expedition_encounter_requires_defender_and_invasion_damages_purifier) {
  ac::expedition::EncounterState encounter{};
  ac::expedition::beginEncounter(encounter);
  ac::expedition::stepEncounter(encounter,50u,false,2u);
  AC_CHECK_EQ(encounter.progressMs,0u);
  AC_CHECK_EQ(encounter.hpMilli,999800u);
  ac::expedition::stepEncounter(encounter,50u,true,0u);
  AC_CHECK_EQ(encounter.progressMs,50u);
}
