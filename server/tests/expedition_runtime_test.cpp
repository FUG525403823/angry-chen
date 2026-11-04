#include "tiny_test.hpp"
#include "server/runtime.hpp"
#include "net/codec.hpp"
#include "net/expedition_codec.hpp"
#include "config/player.hpp"
#include "metrics/counters.hpp"
#include "net/udp_socket.hpp"
#include <filesystem>
#include <span>
namespace {
struct RecoveryPeer {
  ac::server::Runtime runtime;
  ac::net::UdpSocket socket;
  uint64_t now=10000;
  uint16_t session=0;
  uint32_t tick=0, baseline=0, run=0, offer=0;
  uint16_t ackedSeq=0;
  bool hasSnapshot=false;
  uint8_t stage=0;
  void drain() {
    uint8_t b[4096]{}; ac::net::Endpoint from{};
    for(int i=0;i<4096;++i) {
      int n=socket.recvFrom(from,std::span<uint8_t>(b,sizeof(b)));if(n<=0)break;
      auto p=ac::net::decodePacket(b,static_cast<std::size_t>(n));if(!p.isOk)continue;
      if(p.value.header.type==2)session=p.value.header.session;
      if(p.value.header.type==5) {
        auto s=ac::net::decodeSnapshot(b,static_cast<std::size_t>(n),nullptr);
        if(s.isOk){tick=s.value.tick;baseline=s.value.baselineTick;ackedSeq=s.value.lastAckedSeq;hasSnapshot=true;}
      }
      if(p.value.header.type==14){auto s=ac::net::decodeExpeditionState(b,static_cast<std::size_t>(n));if(s.isOk){stage=s.value.stage;run=s.value.runId;offer=s.value.offerId;}}
    }
  }
  bool send(const uint8_t* b,std::size_t n) {return n && socket.sendTo(ac::net::Endpoint{0x7f000001u,runtime.udpPort()},std::span<const uint8_t>(b,n))>0;}
  void step(){now+=50;hasSnapshot=false;runtime.pollOnce(now);drain();}
  bool start(bool expedition) {
    ac::server::RuntimeConfig c{};c.udpPort=0;c.httpPort=0;c.isHttpEnabled=false;c.sheepTarget=0;c.isAutoReady=false;c.dataDir="ac-runtime-expedition-recovery-data";
    std::filesystem::create_directories(c.dataDir);std::string err;if(!runtime.start(c,&err)||!socket.bind(0))return false;
    uint8_t b[128]{};auto e=ac::net::encodeHello({3,1,0,0,0},{1337,0},b,sizeof(b));if(!send(b,e.bytes))return false;runtime.pollOnce(now);drain();if(!session)return false;
    if(expedition){request(1,0,0,1);step();}
    command(1,0,ac::config::kButtonReady);step();
    for(int i=0;i<45 && (expedition?stage!=1:tick==0);++i)step();
    if(expedition){if(stage!=1)return false;request(3,run,offer,2);step();}
    for(int i=0;i<130 && (!hasSnapshot || tick<12);++i)step();
    return tick>=12 && (!expedition || stage==2);
  }
  void request(uint8_t op,uint32_t r,uint32_t o,uint32_t id){uint8_t b[128]{};auto e=ac::net::encodeExpeditionRequest({3,13,1,session,1},{id,0,0},{r,o,id,op,0,0},b,sizeof(b));AC_CHECK(send(b,e.bytes));}
  void command(uint16_t seq,uint32_t t,uint8_t buttons=0){uint8_t b[128]{};ac::net::CommandPayload c{};c.seq=seq;c.clientTick=t;c.buttons=buttons;c.moveX=127;auto e=ac::net::encodeCommand({3,4,1,session,seq},{seq,0,0},c,b,sizeof(b));AC_CHECK(send(b,e.bytes));}
};
}
AC_TEST(expedition_runtime_stale_command_recovers_delta_chain_without_accepting_input) {
  RecoveryPeer p;AC_CHECK(p.start(true));
  // One lost delta means the client keeps this last applied authority tick.
  const auto applied=p.tick;
  for (int i = 0; i < 13; ++i) {
    p.step();
  }
  const auto drops=ac::metrics::counterValue(p.runtime.counters(),ac::metrics::CounterId::kDroppedFrames);
  p.command(65535,applied,ac::config::kButtonFire);p.step();
  AC_CHECK(p.hasSnapshot);AC_CHECK_EQ(p.baseline,0u);
  AC_CHECK(p.ackedSeq!=65535);
  AC_CHECK(ac::metrics::counterValue(p.runtime.counters(),ac::metrics::CounterId::kDroppedFrames)>drops);
  // Replays cannot solicit another full even when the four-tick throttle expires.
  for (int i = 0; i < 4; ++i) {
    p.step();
  }
  p.command(65535,applied);p.step();AC_CHECK(p.baseline!=0);
  // Forward 16-bit wrapping is a new hint; it does not poison accepted-input dedup.
  p.command(1,applied);p.step();AC_CHECK_EQ(p.baseline,0u);
  p.command(2,applied);p.step();AC_CHECK(p.baseline!=0);
  for (int i = 0; i < 4; ++i) {
    p.step();
  }

  p.command(3,p.tick+100);p.step();AC_CHECK(p.baseline!=0);
  // Out-of-order hints and a different endpoint claiming this session are rejected.
  p.command(65534,applied);p.step();AC_CHECK(p.baseline!=0);
  ac::net::UdpSocket spoof;AC_CHECK(spoof.bind(0));uint8_t b[64]{};
  ac::net::CommandPayload c{};c.seq=10;c.clientTick=applied;
  auto e=ac::net::encodeCommand({3,4,1,p.session,10},{10,0,0},c,b,sizeof(b));
  AC_CHECK(spoof.sendTo(ac::net::Endpoint{0x7f000001u,p.runtime.udpPort()},std::span<const uint8_t>(b,e.bytes))>0);
  p.step();AC_CHECK(p.baseline!=0);
  p.command(3,p.tick);p.step();AC_CHECK_EQ(p.ackedSeq,3u);
}
AC_TEST(expedition_runtime_bounded_profile_delay_accepts_twelve_ticks_only) {
  RecoveryPeer p;AC_CHECK(p.start(true));const auto applied=p.tick;
  // D=100ms with both directions reordered: 225+225+100+33.34ms,
  // rounded up to the 50ms tick is 12 ticks (600ms).
  for (int i = 0; i < 12; ++i) {
    p.step();
  }
  p.command(20,applied);p.runtime.pollOnce(p.now);p.drain();
  p.step();AC_CHECK_EQ(p.ackedSeq,20u);
  p.command(21,applied);p.step();AC_CHECK(p.ackedSeq!=21u);AC_CHECK_EQ(p.baseline,0u);
  for (int i = 0; i < 4; ++i) {
    p.step();
  }

  p.command(22,p.tick+1);p.runtime.pollOnce(p.now);p.drain();p.step();AC_CHECK(p.ackedSeq!=22u);AC_CHECK(p.baseline!=0u);
}

AC_TEST(expedition_runtime_classic_stale_input_does_not_request_full) {
  RecoveryPeer p;AC_CHECK(p.start(false));const auto applied=p.tick;
  for (int i = 0; i < 4; ++i) {
    p.step();
  }
  p.command(20,applied);p.step();
  AC_CHECK(p.hasSnapshot);AC_CHECK(p.baseline!=0);
}

AC_TEST(expedition_runtime_content_mismatch_is_explicit_and_does_not_allocate) {
  ac::server::RuntimeConfig config{};
  config.udpPort=0u; config.httpPort=0u; config.isHttpEnabled=false;
  config.dataDir="ac-runtime-expedition-test-data";
  std::filesystem::create_directories(config.dataDir);
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config,&error));
  ac::net::UdpSocket client;
  AC_CHECK(client.bind(0u));
  uint8_t bytes[64]{};
  ac::net::PacketHeader header{3u,1u,0u,0u,0u};
  ac::net::HelloPayload hello{1337u,0u}; hello.contentHash^=1u;
  const auto encoded=ac::net::encodeHello(header,hello,bytes,sizeof(bytes));
  AC_CHECK(encoded.isOk);
  AC_CHECK(client.sendTo(ac::net::Endpoint{0x7f000001u,runtime.udpPort()},std::span<const uint8_t>(bytes,encoded.bytes))>0);
  runtime.pollOnce(100000u);
  AC_CHECK_EQ(runtime.clientCount(),0u);
  AC_CHECK(client.poll(100));
  uint8_t reply[64]{}; ac::net::Endpoint from{};
  const int got=client.recvFrom(from,std::span<uint8_t>(reply,sizeof(reply)));
  AC_CHECK(got>0);
  AC_CHECK(ac::net::decodeDisconnect(reply,static_cast<std::size_t>(got)).isOk);
  runtime.stop();
}

AC_TEST(expedition_runtime_rejects_legacy_hello_without_allocating_player) {
  ac::server::RuntimeConfig config{};
  config.udpPort=0u; config.httpPort=0u; config.isHttpEnabled=false;
  config.dataDir="ac-runtime-expedition-test-data";
  std::filesystem::create_directories(config.dataDir);
  ac::server::Runtime runtime;
  std::string error;
  AC_CHECK(runtime.start(config,&error));
  ac::net::UdpSocket client;
  AC_CHECK(client.bind(0u));
  uint8_t bytes[64]={};
  ac::net::PacketHeader header{};
  header.version=2u; header.type=1u;
  const auto encoded=ac::net::encodeHello(header,ac::net::HelloPayload{1337u,0u},bytes,sizeof(bytes));
  AC_CHECK(encoded.isOk);
  AC_CHECK(client.sendTo(ac::net::Endpoint{0x7f000001u,runtime.udpPort()},std::span<const uint8_t>(bytes,encoded.bytes))>0);
  runtime.pollOnce(100000u);
  AC_CHECK_EQ(runtime.clientCount(),0u);
  uint8_t reply[64]{};
  ac::net::Endpoint from{};
  AC_CHECK(client.poll(100));
  const int got=client.recvFrom(from,std::span<uint8_t>(reply,sizeof(reply)));
  AC_CHECK(got>0);
  const auto rejection=ac::net::decodeDisconnect(reply,static_cast<std::size_t>(got));
  AC_CHECK(rejection.isOk);
  AC_CHECK_EQ(rejection.value.reason,1u);
  runtime.stop();
}
