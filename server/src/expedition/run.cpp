#include "expedition/run.hpp"
namespace ac::expedition {
namespace {
void makeOffer(RunState& run,RunPlayer& player) noexcept {
  uint8_t pool[6]={};
  uint8_t count=0u;
  for(uint8_t card=1u;card<=6u;++card) {
    const uint8_t bit=static_cast<uint8_t>(1u<<(card-1u));
    if((player.cardMask&bit)!=0u) continue;
    if((card==2u || card==3u) && (player.cardMask&1u)==0u) continue;
    if((card==5u || card==6u) && (player.cardMask&8u)==0u) continue;
    pool[count++]=card;
  }
  for(uint8_t i=0u;i<3u;++i) {
    player.choices[i]=0u;
    if(count==0u) continue;
    const auto index=static_cast<uint8_t>(run.rewardRng.nextU32()%count);
    player.choices[i]=pool[index];
    pool[index]=pool[--count];
  }
  player.offerId=run.nextOfferId++;
  player.resolved=false;
}
uint8_t choose(RunState& run,RunPlayer& player,uint32_t offerId,uint8_t op,uint8_t choice) noexcept {
  if(run.stage!=Stage::kReward) return 4u;
  if(offerId!=player.offerId || player.resolved) return 5u;
  if(op==3u) {
    if(choice!=0u) return 6u;
    player.resolved=true;
    player.supplyPending=true;
    return 1u;
  }
  if(op==4u) {
    if(choice!=0u) return 6u;
    if(player.rerollsLeft==0u) return 8u;
    --player.rerollsLeft;
    makeOffer(run,player);
    // Re-rolling draws only from the prerequisite-filtered pool.
    return 1u;
  }
  if(op!=2u) return 6u;
  bool found=false;
  for(const auto candidate:player.choices) if(candidate==choice) found=true;
  if(!found || choice>6u) return 6u;
  if(choice!=0u) {
    const uint8_t bit=static_cast<uint8_t>(1u<<(choice-1u));
    if((player.cardMask&bit)!=0u) return 6u;
    player.cardMask=static_cast<uint8_t>(player.cardMask|bit);
  } else player.supplyPending=true;
  player.resolved=true;
  return 1u;
}
}
uint8_t request(RunState& run,uint16_t pid,uint32_t runId,uint32_t offerId,uint32_t requestId,uint8_t op,uint8_t choice) noexcept {
  RunPlayer* player=nullptr;
  for(uint8_t i=0u;i<run.playerCount;++i) if(run.players[i].pid==pid) player=&run.players[i];
  if(player==nullptr) return 8u;
  // A stale run may be acknowledged, but cannot consume this run's monotonic ID space.
  if(run.enabled && runId!=run.runId) return 3u;
  if(requestId==0u) return 7u;
  if(requestId==player->lastRequestId) {
    if(runId==player->lastRunId && offerId==player->lastOfferId && op==player->lastOp && choice==player->lastChoice) return player->lastResult;
    return 7u;
  }
  if(requestId<player->lastRequestId) return 7u;
  uint8_t result=2u;
  if(run.enabled) result=runId==run.runId?choose(run,*player,offerId,op,choice):3u;
  player->lastRequestId=requestId;
  player->lastRunId=runId;
  player->lastOfferId=offerId;
  player->lastOp=op;
  player->lastChoice=choice;
  player->lastResult=result;
  ++run.revision;
  return result;
}

void finishEncounter(RunState& run,Result result) noexcept {
  if(run.stage!=Stage::kCombat || result==Result::kNone) return;
  ++run.revision;
  if(result!=Result::kSuccess || run.node>=3u) {
    run.result=result;
    run.stage=Stage::kEnded;
    return;
  }
  ++run.node;
  run.stage=Stage::kReward;
  run.rewardDeadlineMs=run.clockMs+30000u;
  run.objectiveHp=1000u;
  run.progressMs=0u;
  run.combatLeftMs=240000u;
  for(uint8_t i=0u;i<run.playerCount;++i) makeOffer(run,run.players[i]);
}
void tickRun(RunState& run,uint32_t dtMs) noexcept {
  if(!run.enabled || dtMs!=50u || run.stage==Stage::kIdle || run.stage==Stage::kEnded) return;
  run.clockMs+=dtMs;
  if(run.stage!=Stage::kReward) return;
  bool done=true;
  for(uint8_t i=0u;i<run.playerCount;++i) {
    auto& player=run.players[i];
    if(!player.resolved && run.clockMs>=run.rewardDeadlineMs) {
      player.resolved=true;
      player.supplyPending=true;
    }
    if(player.connected && !player.resolved) done=false;
  }
  if(done) { run.stage=Stage::kCombat; ++run.revision; }
}
void beginRun(RunState& run,uint32_t runId,uint32_t seed,const uint16_t* ids,uint8_t count) noexcept {
  run=RunState{};
  run.enabled=true;
  run.runId=runId;
  run.revision=1u;
  run.stage=Stage::kReward;
  run.node=1u;
  run.rewardDeadlineMs=30000u;
  run.rewardRng=ac::Rng{seed * ac::kRngSeedMultiplier + 4u};
  run.playerCount=count<=4u?count:4u;
  for(uint8_t i=0u;i<run.playerCount;++i) {
    auto& player=run.players[i];
    player.pid=ids[i];
    player.offerId=run.nextOfferId++;
    player.choices[0]=1u;
    player.choices[1]=4u;
    player.choices[2]=0u;
  }
}
}
