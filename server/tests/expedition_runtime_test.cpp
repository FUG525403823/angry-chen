#include "tiny_test.hpp"
#include "server/runtime.hpp"
#include "net/codec.hpp"
#include "net/udp_socket.hpp"
#include <filesystem>
#include <span>
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
