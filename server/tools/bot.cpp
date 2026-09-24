// S14 §3 交付物 1：压测机器人（独立进程 —— 服务器侧的 CPU 记账因此只算服务器自己）。
//
// 单进程可以起多个机器人（各自一条套接字 = 各自一个 endpoint），20 Hz 发命令，
// 可选 RTT 与丢包仿真（--latency / --loss）。命令流由 --seed 决定，同种子逐条可复现。
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "core/quantize.hpp"
#include "core/rng.hpp"
#include "net/codec.hpp"
#include "net/udp_socket.hpp"

namespace {

struct Options {
  int players = 1;
  double minutes = 3.0;
  int latencyMs = 0;
  int lossPct = 0;
  std::uint32_t seed = 20260101u;
  std::uint16_t port = 8788u;
  std::uint32_t host = 0x7F000001u;
};

void printUsage(const char* argv0) {
  std::printf("usage: %s [--players N] [--minutes M] [--latency MS] [--loss P] [--seed S]"
              " [--host A.B.C.D] [--port P]\n",
              argv0);
}

// §3：命令 30Hz（以前跟 50ms 的 tick 同频，实际只有 20Hz）。
constexpr std::uint64_t kCommandIntervalMs = 33u;
// §5 latency-200：RTT 200ms ±20ms 是往返口径 → 每向 100ms ±10ms（入向出向都要延迟）。
constexpr std::uint64_t kLatencyJitterMs = 10u;
constexpr std::size_t kDrainBatchSize = 32u;  // 单次轮询最多处理的入向包数

std::uint64_t halfLatencyMs(const Options& options) {
  const std::uint64_t half = static_cast<std::uint64_t>(options.latencyMs) / 2u;
  if (half <= kLatencyJitterMs) return half;
  static std::uint32_t jitterSeed = 0x5EED1234u;  // 每个机器人进程一份，无需跨进程可复现
  jitterSeed = jitterSeed * 1664525u + 1013904223u;
  const std::uint64_t span = kLatencyJitterMs * 2u + 1u;
  return half - kLatencyJitterMs + (jitterSeed % span);
}

bool parseOptions(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const std::size_t eq = arg.find('=');
    const std::string key = eq == std::string::npos ? arg : arg.substr(0u, eq);
    std::string value = eq == std::string::npos ? std::string() : arg.substr(eq + 1u);
    // §6-2/§9 的计划命令用空格分隔（--players 4），两种写法都接受。
    if (eq == std::string::npos && key.rfind("--", 0u) == 0u && i + 1 < argc &&
        std::string(argv[i + 1]).rfind("--", 0u) != 0u) {
      value = argv[++i];
    }
    if (key == "--help" || key == "-h") {
      printUsage(argv[0]);
      return false;
    }
    if (key == "--players") options.players = std::atoi(value.c_str());
    else if (key == "--minutes") options.minutes = std::atof(value.c_str());
    else if (key == "--latency") options.latencyMs = std::atoi(value.c_str());
    else if (key == "--loss") options.lossPct = std::atoi(value.c_str());
    else if (key == "--seed") options.seed = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    else if (key == "--port") options.port = static_cast<std::uint16_t>(std::atoi(value.c_str()));
    else if (key == "--host") {
      unsigned a = 0, b = 0, c = 0, d = 0;
      if (std::sscanf(value.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        options.host = (a << 24) | (b << 16) | (c << 8) | d;
      }
    } else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  if (options.players < 1) options.players = 1;
  if (options.players > 32) options.players = 32;
  return true;
}

// 出向延迟队列：--latency 时命令先入队，到点（now + latency/2）再发，模拟 RTT 的一半。
struct QueuedPacket {
  std::uint64_t dueMs = 0u;
  std::vector<std::uint8_t> bytes{};
};

struct Bot {
  ac::net::UdpSocket socket{};
  ac::net::Endpoint server{};
  ac::Rng rng{0u};
  std::uint32_t nonce = 0u;
  std::uint32_t serverTick = 0u;
  std::uint64_t ackAtMs = 0u;
  std::uint16_t session = 0u;
  std::uint16_t seq = 0u;
  std::uint64_t lastHelloMs = 0u;
  std::uint64_t lastCommandMs = 0u;
  bool hasSession = false;
  std::uint64_t bytesIn = 0u;
  std::uint64_t bytesOut = 0u;
  std::uint64_t packetsIn = 0u;
  std::uint64_t packetsOut = 0u;
  std::uint64_t commandsSent = 0u;
  std::uint64_t snapshotsIn = 0u;
  std::uint64_t matchStatesIn = 0u;
  std::vector<QueuedPacket> outbox{};
  std::vector<QueuedPacket> inbox{};  // 入向延迟队列（RTT 是往返口径，双向都要延）
};

std::uint64_t nowMs() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

void sendBytes(Bot& bot, const std::uint8_t* bytes, std::size_t size) {
  const int sent = bot.socket.sendTo(bot.server, std::span<const std::uint8_t>(bytes, size));
  if (sent > 0) {
    bot.bytesOut += static_cast<std::uint64_t>(sent);
    ++bot.packetsOut;
  }
}

void enqueueOrSend(Bot& bot, const Options& options, const std::uint8_t* bytes,
                   std::size_t size);

void flushOutbox(Bot& bot) {
  const std::uint64_t now = nowMs();
  for (std::size_t i = 0u; i < bot.outbox.size();) {
    if (bot.outbox[i].dueMs <= now) {
      sendBytes(bot, bot.outbox[i].bytes.data(), bot.outbox[i].bytes.size());
      bot.outbox.erase(bot.outbox.begin() + static_cast<std::ptrdiff_t>(i));
    } else {
      ++i;
    }
  }
}

bool isDropped(Bot& bot, const Options& options) {
  if (options.lossPct <= 0) return false;
  return (bot.rng.nextU32() % 100u) < static_cast<std::uint32_t>(options.lossPct);
}

void sendHello(Bot& bot, const Options& options) {
  std::uint8_t buffer[64] = {};
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<std::uint8_t>(ac::net::PacketType::kHello);
  header.flags = ac::net::requiredFlags(ac::net::PacketType::kHello);
  header.session = 0u;
  header.seq = 0u;
  const ac::net::HelloPayload hello{bot.nonce, 0u};
  const ac::net::EncodeResult encoded =
      ac::net::encodeHello(header, hello, buffer, sizeof(buffer));
  if (encoded.isOk) enqueueOrSend(bot, options, buffer, encoded.bytes);
}

void sendCommand(Bot& bot, const Options& options) {
  std::uint8_t buffer[64] = {};
  ac::net::PacketHeader header{};
  header.version = ac::net::kProtocolVersion;
  header.type = static_cast<std::uint8_t>(ac::net::PacketType::kCommand);
  header.flags = ac::net::requiredFlags(ac::net::PacketType::kCommand);
  header.session = bot.session;
  header.seq = bot.seq;
  ac::net::ReliableExt ext{};
  ext.msgId = bot.seq;
  ac::net::CommandPayload payload{};
  const std::uint32_t roll = bot.rng.nextU32();
  payload.moveX = ac::quantizeAxis(static_cast<double>(roll % 3u) - 1.0);
  payload.moveY = ac::quantizeAxis(static_cast<double>((roll / 3u) % 3u) - 1.0);
  payload.yaw = ac::quantizeAngle(static_cast<double>((roll / 9u) % 628u) / 100.0);
  payload.pitch = 0u;
  payload.buttons = static_cast<std::uint8_t>(((roll / 7u) % 4u) == 0u ? 0x09u : 0x00u);
  payload.switchTo = 0u;
  payload.seq = bot.seq;
  // 用 HelloAck 的 serverTick + 本地流逝推算服务器当前 tick（§5 的 clientTick 必须等于服务器 tick）。
  payload.clientTick = bot.serverTick;
  const ac::net::EncodeResult encoded =
      ac::net::encodeCommand(header, ext, payload, buffer, sizeof(buffer));
  if (encoded.isOk && !isDropped(bot, options)) {
    enqueueOrSend(bot, options, buffer, encoded.bytes);
    ++bot.commandsSent;
  }
  ++bot.seq;
  if (bot.seq == 0u) bot.seq = 1u;
}

void enqueueOrSend(Bot& bot, const Options& options, const std::uint8_t* bytes,
                   std::size_t size) {
  if (options.latencyMs <= 0) {
    sendBytes(bot, bytes, size);
    return;
  }
  QueuedPacket packet{};
  packet.dueMs = nowMs() + halfLatencyMs(options);
  packet.bytes.assign(bytes, bytes + size);
  bot.outbox.push_back(std::move(packet));
}

// 单条响应的处理（入向延迟队列到点后也走这里）。
void handleResponse(Bot& bot, const std::uint8_t* buffer, std::size_t size) {
  const ac::net::DecodeResult<ac::net::PacketInfo> packet = ac::net::decodePacket(buffer, size);
    if (!packet.isOk) return;
    if (ac::net::packetPayload(packet.value, buffer, size) == nullptr) {
      return;
    }
    switch (static_cast<ac::net::PacketType>(packet.value.header.type)) {
      case ac::net::PacketType::kHelloAck: {
        const auto ack = ac::net::decodeHelloAck(buffer, size);
        if (!ack.isOk || bot.hasSession) break;
        bot.hasSession = true;
        bot.session = packet.value.header.session;
        bot.serverTick = ack.value.serverTick;
        bot.ackAtMs = nowMs();
        break;
      }
      case ac::net::PacketType::kSnapshot: {
        ++bot.snapshotsIn;
        // §5：clientTick 必须**等于**服务器 tick（security::validateClientTick 是严格相等）。
        // 权威帧里的 tick 就是本地钟外推不出来的那个值，收到就抄下来。
        const ac::net::DecodeResult<ac::net::SnapshotView> snapshot =
            ac::net::decodeSnapshot(buffer, size);
        if (snapshot.isOk) bot.serverTick = snapshot.value.tick;
        break;
      }
      case ac::net::PacketType::kEvent:
        break;
      case ac::net::PacketType::kMatchState:
        ++bot.matchStatesIn;
        break;
      case ac::net::PacketType::kDisconnect:
        std::fprintf(stderr, "bot disconnected by server\n");
        bot.hasSession = false;
        break;
      default:
        break;
    }
}

// 入向：真套接字收到的字节按半 RTT 入队延迟（§5 的 RTT 是往返口径）。
void drainIncoming(Bot& bot, const Options& options) {
  for (std::size_t i = 0u; i < kDrainBatchSize; ++i) {
    if (!bot.socket.poll(0)) break;
    ac::net::Endpoint from{};
    std::uint8_t buffer[ac::net::kMaxPacketBytes] = {};
    const int got = bot.socket.recvFrom(from, std::span<std::uint8_t>(buffer, sizeof(buffer)));
    if (got <= 0) break;
    bot.bytesIn += static_cast<std::uint64_t>(got);
    ++bot.packetsIn;
    if (isDropped(bot, options)) continue;  // 入向丢包仿真：收到也不处理
    if (options.latencyMs <= 0) {
      handleResponse(bot, buffer, static_cast<std::size_t>(got));
      continue;
    }
    QueuedPacket packet{};
    packet.dueMs = nowMs() + halfLatencyMs(options);
    packet.bytes.assign(buffer, buffer + got);
    bot.inbox.push_back(std::move(packet));
  }
}

// 到点的入向包按到达顺序交给处理函数。
void flushInbox(Bot& bot) {
  const std::uint64_t now = nowMs();
  for (std::size_t i = 0u; i < bot.inbox.size();) {
    if (bot.inbox[i].dueMs > now) {
      ++i;
      continue;
    }
    handleResponse(bot, bot.inbox[i].bytes.data(), bot.inbox[i].bytes.size());
    bot.inbox.erase(bot.inbox.begin() + static_cast<std::ptrdiff_t>(i));
  }
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parseOptions(argc, argv, options)) return 2;
  if (!ac::net::ensureWinsock()) {
    std::fprintf(stderr, "winsock init failed\n");
    return 1;
  }

  std::vector<Bot> bots(static_cast<std::size_t>(options.players));
  for (std::size_t i = 0u; i < bots.size(); ++i) {
    Bot& bot = bots[i];
    bot.server.ipv4 = options.host;
    bot.server.port = options.port;
    bot.rng = ac::Rng{options.seed + static_cast<std::uint32_t>(i) * 7919u};
    bot.nonce = 0x51000000u + static_cast<std::uint32_t>(i) + (options.seed & 0xFFFFu);
    if (!bot.socket.bind(0u)) {
      std::fprintf(stderr, "bot bind failed\n");
      return 1;
    }
    sendHello(bot, options);
    bot.lastHelloMs = nowMs();
  }

  const std::uint64_t startMs = nowMs();
  const std::uint64_t endMs = startMs + static_cast<std::uint64_t>(options.minutes * 60000.0);
  while (nowMs() < endMs) {
    const std::uint64_t now = nowMs();
    for (Bot& bot : bots) {
      flushOutbox(bot);
      flushInbox(bot);
      drainIncoming(bot, options);
      if (!bot.hasSession) {
        if (now - bot.lastHelloMs >= 1000u) {
          sendHello(bot, options);
          bot.lastHelloMs = now;
        }
        continue;
      }
      if (now - bot.lastCommandMs >= kCommandIntervalMs) {
        sendCommand(bot, options);
        bot.lastCommandMs = now;
      }
    }
    // 别把核心烧满：性能门禁只测服务端，但机器人抢 CPU 会污染同一台机器的读数。
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  for (const Bot& bot : bots) {
    std::printf("bot session=%u packetsIn=%llu packetsOut=%llu bytesIn=%llu bytesOut=%llu "
                "snapshots=%llu matchStates=%llu commands=%llu hasSession=%d\n",
                static_cast<unsigned>(bot.session),
                static_cast<unsigned long long>(bot.packetsIn),
                static_cast<unsigned long long>(bot.packetsOut),
                static_cast<unsigned long long>(bot.bytesIn),
                static_cast<unsigned long long>(bot.bytesOut),
                static_cast<unsigned long long>(bot.snapshotsIn),
                static_cast<unsigned long long>(bot.matchStatesIn),
                static_cast<unsigned long long>(bot.commandsSent),
                bot.hasSession ? 1 : 0);
  }
  return 0;
}
