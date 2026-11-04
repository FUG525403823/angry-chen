#include "tiny_test.hpp"
#include "expedition/build.hpp"
#include "sim/entity_table.hpp"
#include "sim/world.hpp"
#include <memory>
#include "combat/resolve.hpp"
#include "ai/sheep_brain.hpp"
#include "ai/king_phases.hpp"
#include "sim/step.hpp"

namespace {
using ac::config::HitPart;
ac::sim::Entity playerWith(uint8_t card) {
  ac::sim::Entity e{}; e.id = 1; e.active = true; e.hp = 50; e.maxHp = 100;
  ac::expedition::enableBuild(e.build); ac::expedition::selectCard(e.build, card); return e;
}
ac::sim::Entity sheep() {
  ac::sim::Entity e{}; e.id = 2; e.active = true; e.kind = ac::sim::EntityKind::kSheep;
  e.hp = 100; return e;
}
}

AC_TEST(expedition_build_mark_order_expiry_and_shooter_isolation) {
  auto p = playerWith(1); auto t = sheep();
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,5,0),1.0);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,5,0),0.85);
  ac::expedition::onHit(p,t,HitPart::kHead,5,0);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,5,2999),1.5);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,5,3000),0.85);
  auto other = playerWith(1); other.id=3;
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(other,t,HitPart::kTorso,5,1),0.85);
  p.build.enabled=false;
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,5,1),1.0);
}

AC_TEST(expedition_build_refund_cooldown_cap_and_reserve) {
  auto p=playerWith(2); auto t=sheep(); ac::combat::createWeaponState(p.weapon);
  const auto cap=ac::combat::activeMag(p.weapon); p.weapon.magInSlot[0]=cap-2;
  const auto reserve=p.weapon.reserveAmmo;
  ac::expedition::onHit(p,t,HitPart::kHead,5,0);
  AC_CHECK_EQ(ac::combat::activeMag(p.weapon),cap-1);
  ac::expedition::onHit(p,t,HitPart::kHead,5,499);
  AC_CHECK_EQ(ac::combat::activeMag(p.weapon),cap-1);
  ac::expedition::onHit(p,t,HitPart::kHead,5,500);
  ac::expedition::onHit(p,t,HitPart::kHead,5,1000);
  AC_CHECK_EQ(ac::combat::activeMag(p.weapon),cap);
  AC_CHECK_EQ(p.weapon.reserveAmmo,reserve);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,5,0),0.85);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,5,0),1.0);
}

AC_TEST(expedition_build_focus_stationary_boundary_movement_and_classic) {
  auto p=playerWith(3); auto t=sheep();
  ac::expedition::updateBuild(p,999,999);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,5,999),1.0);
  ac::expedition::updateBuild(p,1000,1);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,5,1000),1.75);
  p.vel.x=0.01; ac::expedition::updateBuild(p,1050,50);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,5,1050),0.8);
  p.vel.x=0; ac::expedition::updateBuild(p,1100,50);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,5,1100),1.0);
  p.build.enabled=false; p.vel.x=1;
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,5,1100),1.0);
}

AC_TEST(expedition_build_stagger_range_target_cooldown_and_classic) {
  auto p=playerWith(4); auto t=sheep();
  t.state=ac::config::sheepStateCode(ac::config::SheepState::kChase);
  ac::expedition::onHit(p,t,HitPart::kTorso,6,0);
  AC_CHECK_EQ(t.state,ac::config::sheepStateCode(ac::config::SheepState::kStagger));
  AC_CHECK_EQ(t.ai.timerMs,500.0);
  t.ai.timerMs=0; ac::expedition::onHit(p,t,HitPart::kHead,6,1999);
  AC_CHECK_EQ(t.ai.timerMs,0.0);
  ac::expedition::onHit(p,t,HitPart::kHead,6,2000); AC_CHECK_EQ(t.ai.timerMs,500.0);
  auto independent=sheep(); independent.id=4;
  ac::expedition::onHit(p,independent,HitPart::kTorso,6,1); AC_CHECK_EQ(independent.ai.timerMs,500.0);
  auto second=playerWith(4); second.id=3; t.ai.timerMs=0;
  ac::expedition::onHit(second,t,HitPart::kTorso,6,1); AC_CHECK_EQ(t.ai.timerMs,500.0);
  auto other=sheep(); other.id=3; ac::expedition::onHit(p,other,HitPart::kTorso,6.001,2000);
  AC_CHECK_EQ(other.ai.timerMs,0.0);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,6.001,0),0.8);
  p.build.enabled=false; t.ai.timerMs=0; ac::expedition::onHit(p,t,HitPart::kHead,6,4000);
  AC_CHECK_EQ(t.ai.timerMs,0.0);
}

AC_TEST(expedition_build_sustain_range_cooldown_cap_no_revive) {
  auto p=playerWith(5); auto t=sheep(); t.hp=0;
  ac::expedition::onKill(p,t,6,0); AC_CHECK_EQ(p.hp,58.0);
  ac::expedition::onKill(p,t,6,999); AC_CHECK_EQ(p.hp,58.0);
  p.hp=99; ac::expedition::onKill(p,t,6,1000); AC_CHECK_EQ(p.hp,100.0);
  p.hp=50; ac::expedition::onKill(p,t,6.001,2000); AC_CHECK_EQ(p.hp,50.0);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,6.001,0),0.8);
  p.downed.downed=true; p.hp=0; ac::expedition::onKill(p,t,6,3000); AC_CHECK_EQ(p.hp,0.0);
}

AC_TEST(expedition_build_tempo_active_mag_reload_reserve_and_cooldown) {
  auto p=playerWith(6); auto t=sheep(); t.hp=0; ac::combat::createWeaponState(p.weapon);
  p.weapon.activeSlot=1; p.weapon.magInSlot[1]=0; p.weapon.reloadEndsAtMs=9999;
  const auto reserve=p.weapon.reserveAmmo;
  ac::expedition::onKill(p,t,6,0);
  AC_CHECK_EQ(p.weapon.magInSlot[1],30); AC_CHECK_EQ(p.weapon.reloadEndsAtMs,0.0);
  p.weapon.magInSlot[1]=1; ac::expedition::onKill(p,t,6,1999); AC_CHECK_EQ(p.weapon.magInSlot[1],1);
  ac::expedition::onKill(p,t,6,2000); AC_CHECK_EQ(p.weapon.magInSlot[1],30);
  p.weapon.magInSlot[1]=1; ac::expedition::onKill(p,t,6.001,4000); AC_CHECK_EQ(p.weapon.magInSlot[1],1);
  p.build.enabled=false; ac::expedition::onKill(p,t,6,4000); AC_CHECK_EQ(p.weapon.magInSlot[1],1);
  AC_CHECK_EQ(p.weapon.reserveAmmo,reserve);
  p.build.enabled=true; AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,7,0),0.8);
}

AC_TEST(expedition_build_pulse_3d_boundary_alive_idle_cap_and_cooldown) {
  auto w=std::make_unique<ac::sim::World>();
  w->highWater=5; w->activeCount=5;
  for (uint16_t i=0;i<5;++i) { w->activeIds[i]=i+1; w->entities[i]=playerWith(1); w->entities[i].id=i+1; }
  auto& p=w->entities[0]; auto& edge=w->entities[1]; edge.pos.y=8; edge.hp=99;
  w->entities[2].pos.y=8.001; w->entities[3].downed.downed=true; w->entities[3].hp=0;
  w->entities[4].idle=true;
  AC_CHECK(ac::expedition::pulse(*w,p,0)); AC_CHECK_EQ(p.hp,75.0); AC_CHECK_EQ(edge.hp,100.0);
  AC_CHECK_EQ(w->entities[2].hp,50.0); AC_CHECK_EQ(w->entities[3].hp,0.0); AC_CHECK_EQ(w->entities[4].hp,50.0);
  AC_CHECK(!ac::expedition::pulse(*w,p,19999)); AC_CHECK(ac::expedition::pulse(*w,p,20000));
  AC_CHECK_EQ(p.build.pulseReadyAtMs,40000u);
  p.hp=100; edge.hp=100; AC_CHECK(ac::expedition::pulse(*w,p,40000));
  p.idle=true; AC_CHECK(!ac::expedition::pulse(*w,p,60000)); p.idle=false;
  p.build.enabled=false; AC_CHECK(!ac::expedition::pulse(*w,p,60000));
}

AC_TEST(expedition_build_resolve_actual_fire_all_cards_and_classic) {
  for (uint8_t card=0;card<=6;++card) {
    auto w=std::make_unique<ac::sim::World>(); w->activeCount=2; w->highWater=2; w->activeIds[0]=1; w->activeIds[1]=2;
    auto& p=w->entities[0]; p=playerWith(card); ac::combat::createWeaponState(p.weapon);
    p.pos={-5,-0.7,20};
    if (card==0) p.build.enabled=false;
    auto& t=w->entities[1]; t=sheep(); t.pos={0,0,20}; t.team=1; t.maxHp=1000; t.hp=1000;
    t.state=ac::config::sheepStateCode(ac::config::SheepState::kChase);
    if (card==5 || card==6) t.hp=1;
    if (card==3) p.build.stationaryMs=950;
    ac::sim::Command cmd{}; cmd.seq=1; cmd.yaw=ac::kPi/2; cmd.buttons=ac::config::kButtonFire;
    ac::sim::resolveCombat(*w,&cmd,1,50,nullptr);
    AC_CHECK(w->eventCount>0); AC_CHECK((w->events[0].flags & ac::config::kHitFlagHeadshot)!=0);
    if (card==1) {
      AC_CHECK_EQ(t.hp,950.0); AC_CHECK_EQ(p.build.triggerCount,1u);
      w->timeMs=200; cmd.seq=2; ac::sim::resolveCombat(*w,&cmd,1,50,nullptr);
      AC_CHECK_EQ(t.hp,875.0);
    }
    if (card==2) AC_CHECK_EQ(ac::combat::activeMag(p.weapon),12);
    if (card==3) AC_CHECK_EQ(t.hp,912.5);
    if (card==4) AC_CHECK_EQ(t.ai.timerMs,500.0);
    if (card==5) AC_CHECK_EQ(p.hp,58.0);
    if (card==6) AC_CHECK_EQ(ac::combat::activeMag(p.weapon),12);
    if (card==0) { AC_CHECK_EQ(t.hp,950.0); AC_CHECK_EQ(p.build.triggerCount,0u); }
  }
}

AC_TEST(expedition_build_invalid_hit_and_kill_are_neutral) {
  auto p=playerWith(1); auto t=sheep();
  ac::expedition::onHit(p,t,HitPart::kHead,-1,0); AC_CHECK_EQ(p.build.triggerCount,0u);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,-1,0),1.0);
  p=playerWith(5); ac::expedition::onKill(p,t,5,0); AC_CHECK_EQ(p.hp,50.0);
  p=playerWith(2); ac::combat::createWeaponState(p.weapon); p.weapon.magInSlot[0]=1;
  p.active=false; ac::expedition::onHit(p,t,HitPart::kHead,5,0); AC_CHECK_EQ(p.weapon.magInSlot[0],1);
}

AC_TEST(expedition_build_stagger_actual_ai_and_king_phase) {
  auto w=std::make_unique<ac::sim::World>(); auto p=playerWith(4); auto t=sheep();
  t.sheepKind=3; t.maxHp=1000; t.hp=100; t.ai.phase=1;
  t.state=ac::config::sheepStateCode(ac::config::SheepState::kChase);
  ac::expedition::onHit(p,t,HitPart::kTorso,6,0);
  ac::ai::updateKing(*w,t,50);
  AC_CHECK_EQ(t.state,ac::config::sheepStateCode(ac::config::SheepState::kStagger));
  auto intent=ac::ai::updateSheepIntent(*w,t,nullptr,0,1.0,499);
  AC_CHECK_EQ(intent.speed,0.0); AC_CHECK_EQ(t.ai.timerMs,1.0);
  w->activeCount=2; w->highWater=2; w->activeIds[0]=1; w->activeIds[1]=2;
  w->entities[0]=p; w->entities[1]=t; w->entities[1].ai.target.targetId=1;
  const uint16_t ids[]={1};
  AC_CHECK_EQ(ac::sim::resolveSheepAttacks(*w,ids,1),0);
  AC_CHECK_EQ(ac::sim::resolveEliteFire(*w,ids,1),0);
  AC_CHECK_EQ(w->entities[0].hp,50.0);
  intent=ac::ai::updateSheepIntent(*w,t,nullptr,0,1.0,1);
  AC_CHECK_EQ(t.state,ac::config::sheepStateCode(ac::config::SheepState::kChase));
}

AC_TEST(expedition_build_classic_all_hooks_are_neutral_and_reset_clears) {
  auto p=playerWith(1); auto t=sheep();
  for (uint8_t card=2;card<=6;++card) ac::expedition::selectCard(p.build,card);
  ac::combat::createWeaponState(p.weapon); p.weapon.magInSlot[0]=1;
  p.build.enabled=false; p.vel.x=1;
  ac::expedition::onHit(p,t,HitPart::kHead,5,0);
  ac::expedition::updateBuild(p,1000,1000); t.hp=0; ac::expedition::onKill(p,t,5,1000);
  AC_CHECK_EQ(p.hp,50.0); AC_CHECK_EQ(p.weapon.magInSlot[0],1);
  AC_CHECK_EQ(p.build.triggerCount,0u); AC_CHECK_EQ(t.ai.timerMs,0.0);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,10,0),1.0);
  ac::expedition::resetBuild(p.build);
  AC_CHECK_EQ(p.build.selectedMask,0u); AC_CHECK_EQ(p.build.pulseReadyAtMs,0u);
  AC_CHECK_EQ(p.build.stationaryMs,0u); AC_CHECK_EQ(p.build.triggerCount,0u);
}

AC_TEST(expedition_build_combined_multipliers_follow_card_id_order) {
  auto p=playerWith(1); auto t=sheep();
  for (uint8_t card=2;card<=6;++card) ac::expedition::selectCard(p.build,card);
  p.build.stationaryMs=1000;
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kTorso,6,0),0.7224999999999999);
  ac::expedition::onHit(p,t,HitPart::kHead,6,0);
  AC_CHECK_EQ(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,6,1),2.625);
  AC_CHECK_NEAR(ac::expedition::calculateHitModifier(p,t,HitPart::kHead,7,1),1.344,1e-12);
}

AC_TEST(expedition_build_selection_rejects_illegal_duplicate_and_classic) {
  ac::sim::Entity player{};
  AC_CHECK(!ac::expedition::selectCard(player.build, 1u));
  ac::expedition::enableBuild(player.build);
  AC_CHECK(!ac::expedition::selectCard(player.build, 0u));
  AC_CHECK(!ac::expedition::selectCard(player.build, 7u));
  AC_CHECK(ac::expedition::selectCard(player.build, 2u));
  AC_CHECK(!ac::expedition::selectCard(player.build, 2u));
  ac::expedition::resetBuild(player.build);
  AC_CHECK(!player.build.enabled);
  AC_CHECK_EQ(player.build.selectedMask, 0u);
}
