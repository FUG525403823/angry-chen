#include "tiny_test.hpp"
#include "net/expedition_codec.hpp"
#include <array>
#include <algorithm>

AC_TEST(expedition_rejects_malformed_wire) {
  using namespace ac::net;
  std::array<uint8_t,116> b{};
  ExpeditionRequest req{42,2,1,2,1,0};
  auto h=PacketHeader{3,13,1,7,9};
  AC_CHECK(encodeExpeditionRequest(h,{},req,b.data(),b.size()).isOk);
  for(std::size_t n=0;n<38;++n)AC_CHECK(!decodeExpeditionRequest(b.data(),n).isOk);
  AC_CHECK(!decodeExpeditionRequest(b.data(),39).isOk);
  b[0]=2;AC_CHECK(!decodeExpeditionRequest(b.data(),38).isOk);b[0]=3;
  b[34]=1;AC_CHECK(!decodeExpeditionRequest(b.data(),38).isOk);b[34]=0;
  b[33]=7;AC_CHECK(!decodeExpeditionRequest(b.data(),38).isOk);
  req.op=1;req.choiceId=0;AC_CHECK(!encodeExpeditionRequest(h,{},req,b.data(),b.size()).isOk);
  req={42,2,1,5,1,0};AC_CHECK(!encodeExpeditionRequest(h,{},req,b.data(),b.size()).isOk);
  h.type=15;AC_CHECK(!encodeExpeditionRequest(h,{},req,b.data(),b.size()).isOk);
  ExpeditionState s{};s.playerCount=4;s.localPid=1;
  for(unsigned i=0;i<4;++i)s.players[i].pid=static_cast<uint16_t>(i+1);
  auto e=encodeExpeditionState(PacketHeader{3,14,1,7,9},{},s,b.data(),b.size());AC_CHECK(e.isOk);AC_CHECK_EQ(e.bytes,116u);
  for(std::size_t n=0;n<116;++n)AC_CHECK(!decodeExpeditionState(b.data(),n).isOk);
  b[74]=0xc0;AC_CHECK(!decodeExpeditionState(b.data(),116).isOk);
}

AC_TEST(expedition_hello_v3_strict_length) {
  using namespace ac::net;
  std::array<uint8_t,28> b{};
  auto e=encodeHello(PacketHeader{3,1,0,0,0},HelloPayload{1,0},b.data(),b.size());
  AC_CHECK(e.isOk); AC_CHECK_EQ(e.bytes,28u);
  AC_CHECK(decodeHello(b.data(),28).isOk); AC_CHECK(!decodeHello(b.data(),20).isOk);
}

AC_TEST(expedition_state_full_recovery) {
  using namespace ac::net;
  ExpeditionState state{}; state.mode=1; state.runId=42; state.localPid=7; state.playerCount=1;
  state.players[0]={7,33,500,19}; state.offerId=2; state.offerCount=3; state.choices={1,4,0};
  std::array<uint8_t,116> bytes{};
  auto encoded=encodeExpeditionState(PacketHeader{3,14,1,7,9},ReliableExt{1,0,0},state,bytes.data(),bytes.size());
  AC_CHECK(encoded.isOk); AC_CHECK_EQ(encoded.bytes,83u);
  auto decoded=decodeExpeditionState(bytes.data(),encoded.bytes);
  AC_CHECK(decoded.isOk); AC_CHECK_EQ(decoded.value.players[0].triggerCount,19u);
  AC_CHECK_EQ(decoded.value.choices[1],4u);
}

AC_TEST(expedition_request_handwritten_wire) {
  using namespace ac::net;
  PacketHeader header{3, 13, kFlagReliable, 7, 9};
  ExpeditionRequest request{0x11223344, 0x55667788, 1, 2, 6, 0};
  std::array<uint8_t, 38> bytes{};
  const auto encoded = encodeExpeditionRequest(header, ReliableExt{1, 0, 0}, request, bytes.data(), bytes.size());
  const std::array<uint8_t, 38> expected{3,13,1,0,7,0,9,0,1,0,0,0,0,0,0,0,0,0,0,0,
      0x44,0x33,0x22,0x11,0x88,0x77,0x66,0x55,1,0,0,0,2,6,0,0,0,0};
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, expected.size());
  AC_CHECK(bytes == expected);
  const auto decoded = decodeExpeditionRequest(expected.data(), expected.size());
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.runId, 0x11223344u);
  AC_CHECK_EQ(decoded.value.choiceId, 6u);
}
