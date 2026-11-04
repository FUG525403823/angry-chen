#include "tiny_test.hpp"
#include "expedition/run.hpp"

AC_TEST(expedition_run_old_run_cannot_poison_current_request_watermark) {
  ac::expedition::RunState run{};
  const uint16_t ids[]={1u};
  ac::expedition::beginRun(run,2u,7u,ids,1u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,1u,1u,0xffffffffu,2u,1u),3u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,2u,run.players[0].offerId,1u,2u,1u),1u);
}

AC_TEST(expedition_run_requests_are_owned_idempotent_and_offer_scoped) {
  ac::expedition::RunState run{};
  const uint16_t ids[]={1u,2u};
  ac::expedition::beginRun(run,42u,7u,ids,2u);
  const auto offer=run.players[0].offerId;
  AC_CHECK_EQ(ac::expedition::request(run,9u,42u,offer,1u,2u,1u),8u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,41u,offer,1u,2u,1u),3u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,41u,offer,1u,2u,1u),3u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,offer+1u,2u,2u,1u),5u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,offer,3u,2u,1u),1u);
  AC_CHECK_EQ(run.players[0].cardMask,1u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,offer,3u,2u,1u),1u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,offer,3u,2u,4u),7u);
  AC_CHECK_EQ(run.players[0].cardMask,1u);
  AC_CHECK_EQ(run.players[1].cardMask,0u);
}


AC_TEST(expedition_run_skip_reroll_and_deadline_do_not_block_on_disconnected_player) {
  ac::expedition::RunState run{};
  const uint16_t ids[]={1u,2u};
  ac::expedition::beginRun(run,42u,7u,ids,2u);
  auto& first=run.players[0];
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,first.offerId,1u,4u,0u),1u);
  AC_CHECK_EQ(first.rerollsLeft,0u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,first.offerId,2u,4u,0u),8u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,first.offerId,3u,3u,0u),1u);
  AC_CHECK(first.supplyPending);
  run.players[1].connected=false;
  ac::expedition::tickRun(run,50u);
  AC_CHECK_EQ(run.stage,ac::expedition::Stage::kCombat);
  ac::expedition::beginRun(run,43u,7u,ids,2u);
  for(unsigned i=0;i<600u;++i) ac::expedition::tickRun(run,50u);
  AC_CHECK_EQ(run.stage,ac::expedition::Stage::kCombat);
  AC_CHECK(run.players[0].supplyPending);
  AC_CHECK(run.players[1].supplyPending);
}

AC_TEST(expedition_run_three_nodes_filter_cards_and_end_without_classic_wave_rules) {
  ac::expedition::RunState run{};
  const uint16_t ids[]={1u};
  ac::expedition::beginRun(run,42u,7u,ids,1u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,run.players[0].offerId,1u,2u,1u),1u);
  ac::expedition::tickRun(run,50u);
  ac::expedition::finishEncounter(run,ac::expedition::Result::kSuccess);
  AC_CHECK_EQ(run.node,2u);
  AC_CHECK_EQ(run.stage,ac::expedition::Stage::kReward);
  const auto& choices=run.players[0].choices;
  for(auto choice:choices) AC_CHECK(choice==0u || choice==2u || choice==3u || choice==4u);
  AC_CHECK_EQ(ac::expedition::request(run,1u,42u,run.players[0].offerId,2u,3u,0u),1u);
  ac::expedition::tickRun(run,50u);
  ac::expedition::finishEncounter(run,ac::expedition::Result::kSuccess);
  AC_CHECK_EQ(run.node,3u);
  ac::expedition::request(run,1u,42u,run.players[0].offerId,3u,3u,0u);
  ac::expedition::tickRun(run,50u);
  ac::expedition::finishEncounter(run,ac::expedition::Result::kSuccess);
  AC_CHECK_EQ(run.stage,ac::expedition::Stage::kEnded);
  AC_CHECK_EQ(run.result,ac::expedition::Result::kSuccess);
}

AC_TEST(expedition_run_starts_with_personal_safe_offers) {
  ac::expedition::RunState run{};
  const uint16_t ids[] = {1u, 2u};
  ac::expedition::beginRun(run, 42u, 7u, ids, 2u);
  AC_CHECK_EQ(run.runId, 42u);
  AC_CHECK_EQ(run.node, 1u);
  AC_CHECK_EQ(run.stage, ac::expedition::Stage::kReward);
  AC_CHECK_EQ(run.players[0].choices[0], 1u);
  AC_CHECK_EQ(run.players[0].choices[1], 4u);
  AC_CHECK_EQ(run.players[0].choices[2], 0u);
  AC_CHECK(run.players[0].offerId != run.players[1].offerId);
  AC_CHECK_EQ(run.rewardDeadlineMs, 30000u);
}
