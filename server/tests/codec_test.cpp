// S03 §5.1–§5.7：字节级 fixture 往返、字段级编解码、随机损坏与尺寸预算。
// fixture 是双端唯一的字节真相：本文件读 server/tests/fixtures/*.hex，解出的字段与 # expect 行比对，
// 再把结构化结果重编码回原字节（9 个往返 fixture 必须逐字节相等）。
#include "tiny_test.hpp"

#include "core/rng.hpp"
#include "net/codec.hpp"
#include "net/wire.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifndef AC_FIXTURE_DIR
#define AC_FIXTURE_DIR "server/tests/fixtures"
#endif

namespace net = ac::net;
using net::CommandPayload;
using net::DecodeFailure;
using net::EntityRecord;
using net::EventEntry;
using net::MatchStatePlayer;
using net::PacketHeader;
using net::PacketType;
using net::ReliableExt;
using net::SnapshotBaseline;
using net::SnapshotFrame;
using net::SnapshotView;

namespace {

// ---------- 通用小工具 ----------

// 损坏驱动复用服务端自己的 mulberry32（ADR-010 §5 指定的唯一 RNG）+ 固定种子，跨机器同一批损坏字节。
uint32_t below(ac::Rng& rng, uint32_t bound) { return bound == 0u ? 0u : rng.nextU32() % bound; }
ac::Rng makeFuzzRng() { return ac::createRng(0x5A17u, ac::RngStream::kFx); }

std::string toLower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return text;
}

std::string trim(const std::string& text) {
  std::size_t begin = 0;
  std::size_t end = text.size();
  while (begin < end && (text[begin] == ' ' || text[begin] == '\t')) ++begin;
  while (end > begin && (text[end - 1u] == ' ' || text[end - 1u] == '\t')) --end;
  return text.substr(begin, end - begin);
}

bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

uint8_t hexValue(char c) {
  if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
  if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
  return static_cast<uint8_t>(c - 'A' + 10);
}

// # expect 的值：0x 前缀按十六进制，否则十进制。
uint64_t parseNumber(const std::string& text) {
  if (text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    return std::strtoull(text.c_str() + 2, nullptr, 16);
  }
  return std::strtoull(text.c_str(), nullptr, 10);
}

using FieldList = std::vector<std::pair<std::string, uint64_t>>;

void addField(FieldList& fields, std::string key, uint64_t value) {
  fields.emplace_back(toLower(std::move(key)), value);
}

const uint64_t* findField(const FieldList& fields, const std::string& key) {
  for (const auto& field : fields) {
    if (field.first == key) return &field.second;
  }
  return nullptr;
}

bool expectField(const FieldList& fields, const std::string& key, uint64_t value) {
  const uint64_t* actual = findField(fields, key);
  AC_CHECK(actual != nullptr);
  if (actual == nullptr) return false;
  AC_CHECK_EQ(*actual, value);
  return *actual == value;
}

PacketHeader makeHeader(PacketType type, uint16_t flags, uint16_t session, uint16_t seq) {
  return PacketHeader{net::kProtocolVersion, static_cast<uint8_t>(type), flags, session, seq};
}

ReliableExt makeExt(uint32_t msgId, uint32_t ackBase, uint32_t ackBits) {
  return ReliableExt{msgId, ackBase, ackBits};
}

std::vector<EntityRecord> makeRecords(std::size_t count) {
  std::vector<EntityRecord> records(count);
  for (std::size_t i = 0; i < count; ++i) {
    EntityRecord& record = records[i];
    record.id = static_cast<uint16_t>(i + 1u);
    record.kindFlags = static_cast<uint8_t>(i % 4u);
    record.xCm = static_cast<int16_t>(i);
    record.yCm = static_cast<int16_t>(-static_cast<int>(i));
    record.zCm = static_cast<int16_t>(i * 3);
    record.yawUnits = static_cast<uint16_t>(i * 97u);
    record.pitchUnits = static_cast<uint16_t>(i * 13u);
    record.hpRatioUnits = static_cast<uint8_t>(255u - i);
    record.state = static_cast<uint8_t>(i & 0x0Fu);
  }
  return records;
}

// ---------- fixture 装载 ----------

struct Fixture {
  std::string name;
  std::vector<uint8_t> bytes;
  std::vector<std::pair<std::string, std::string>> expect;
};

std::string fixturePath(const std::string& name) {
  return std::string(AC_FIXTURE_DIR) + "/" + name + ".hex";
}

bool loadFixture(const std::string& name, Fixture& fixture) {
  fixture.name = name;
  fixture.bytes.clear();
  fixture.expect.clear();
  std::ifstream input(fixturePath(name));
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (line[0] == '#') {
      const std::string prefix = "# expect:";
      if (line.rfind(prefix, 0) == 0u) {
        const std::string body = line.substr(prefix.size());
        std::size_t start = 0;
        while (start <= body.size()) {
          const std::size_t comma = body.find(',', start);
          const std::string item =
              body.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
          const std::size_t equals = item.find('=');
          if (equals != std::string::npos) {
            fixture.expect.emplace_back(toLower(trim(item.substr(0, equals))),
                                        trim(item.substr(equals + 1u)));
          }
          if (comma == std::string::npos) break;
          start = comma + 1u;
        }
      }
      continue;
    }
    std::size_t index = 0;
    while (index < line.size()) {
      while (index < line.size() && line[index] == ' ') ++index;
      if (index >= line.size()) break;
      if (index + 1u >= line.size() || !isHexDigit(line[index]) || !isHexDigit(line[index + 1u])) {
        return false;
      }
      fixture.bytes.push_back(
          static_cast<uint8_t>(hexValue(line[index]) * 16u + hexValue(line[index + 1u])));
      index += 2u;
    }
  }
  return true;
}

// fixture 加载失败即用例失败（统一前导，避免每个用例重复三行）。
bool loadFixtureOrFail(const std::string& name, Fixture& fixture) {
  const bool isLoaded = loadFixture(name, fixture);
  AC_CHECK(isLoaded);
  return isLoaded;
}


// ---------- fixture 解码 + 重编码 ----------

struct FixtureDecode {
  bool isDecoded = false;
  bool hasRoundtrip = false;
  DecodeFailure failure = DecodeFailure::kOk;
  std::size_t byteCount = 0;
  net::PacketInfo info{};
  SnapshotView snapshot{};
  FieldList fields;
  std::vector<uint8_t> reencoded;
};

void addEventFields(FieldList& fields, const EventEntry& entry) {
  addField(fields, "eventid", entry.eventId);
  addField(fields, "eventtype", net::eventTypeOf(entry));
  std::visit(
      [&fields](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, net::PlayerHitEvent>) {
          addField(fields, "subjectid", payload.subjectId);
          addField(fields, "targetid", payload.targetId);
          addField(fields, "value", payload.value);
          addField(fields, "eventflags", payload.flags);
          addField(fields, "hitx", static_cast<uint16_t>(payload.hitX));
          addField(fields, "hity", static_cast<uint16_t>(payload.hitY));
          addField(fields, "hitz", static_cast<uint16_t>(payload.hitZ));
        } else if constexpr (std::is_same_v<Payload, net::PhaseChangeEvent>) {
          addField(fields, "phase", payload.phase);
          addField(fields, "wave", payload.wave);
          addField(fields, "intermissionms", payload.intermissionMs);
        }
      },
      entry.data);
}

void addRecordFields(FieldList& fields, const EntityRecord& record) {
  addField(fields, "recordid", record.id);
  addField(fields, "kindflags", record.kindFlags);
  addField(fields, "xcm", static_cast<uint16_t>(record.xCm));
  addField(fields, "ycm", static_cast<uint16_t>(record.yCm));
  addField(fields, "zcm", static_cast<uint16_t>(record.zCm));
  addField(fields, "yawunits", record.yawUnits);
  addField(fields, "pitchunits", record.pitchUnits);
  addField(fields, "hpratiounits", record.hpRatioUnits);
  addField(fields, "state", record.state);
}

void keepEncoded(const net::EncodeResult& encoded, const std::vector<uint8_t>& buffer,
                 std::vector<uint8_t>& out) {
  if (!encoded.isOk) return;
  out.assign(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(encoded.bytes));
}

FixtureDecode decodeFixture(const Fixture& fixture) {
  FixtureDecode out;
  out.byteCount = fixture.bytes.size();
  const uint8_t* bytes = fixture.bytes.data();
  const std::size_t size = fixture.bytes.size();
  const auto packet = net::decodePacket(bytes, size);
  if (!packet.isOk) {
    out.failure = packet.failure;
    addField(out.fields, "failure", static_cast<uint64_t>(packet.failure));
    return out;
  }
  out.info = packet.value;
  addField(out.fields, "version", packet.value.header.version);
  addField(out.fields, "type", packet.value.header.type);
  addField(out.fields, "flags", packet.value.header.flags);
  addField(out.fields, "session", packet.value.header.session);
  addField(out.fields, "seq", packet.value.header.seq);
  if (packet.value.hasReliableExt) {
    addField(out.fields, "msgid", packet.value.reliableExt.msgId);
    addField(out.fields, "ackbase", packet.value.reliableExt.ackBase);
    addField(out.fields, "ackbits", packet.value.reliableExt.ackBits);
  }
  if (packet.value.hasFragmentHeader) {
    addField(out.fields, "fragid", packet.value.fragment.fragId);
    addField(out.fields, "fragindex", packet.value.fragment.fragIndex);
    addField(out.fields, "fragcount", packet.value.fragment.fragCount);
  }
  addField(out.fields, "payloadbytes", packet.value.payloadBytes);

  std::vector<uint8_t> buffer(net::kMaxSnapshotBytes + 256u);
  // 快照的差分包必须由调用方给出基线才能重编码，其余类型的往返是自足的。
  out.hasRoundtrip = packet.value.header.type != static_cast<uint8_t>(PacketType::kSnapshot);
  switch (static_cast<PacketType>(packet.value.header.type)) {
    case PacketType::kHello: {
      const auto payload = net::decodeHello(bytes, size);
      if (!payload.isOk) {
        out.failure = payload.failure;
        return out;
      }
      addField(out.fields, "nonce", payload.value.nonce);
      addField(out.fields, "token", payload.value.token);
      keepEncoded(net::encodeHello(packet.value.header, payload.value, buffer.data(), buffer.size()),
                  buffer, out.reencoded);
      break;
    }
    case PacketType::kHelloAck: {
      const auto payload = net::decodeHelloAck(bytes, size);
      if (!payload.isOk) {
        out.failure = payload.failure;
        return out;
      }
      addField(out.fields, "servertick", payload.value.serverTick);
      addField(out.fields, "salt", payload.value.salt);
      keepEncoded(net::encodeHelloAck(packet.value.header, packet.value.reliableExt, payload.value,
                                      buffer.data(), buffer.size()),
                  buffer, out.reencoded);
      break;
    }
    case PacketType::kResume: {
      const auto payload = net::decodeResume(bytes, size);
      if (!payload.isOk) {
        out.failure = payload.failure;
        return out;
      }
      addField(out.fields, "token", payload.value.token);
      keepEncoded(net::encodeResume(packet.value.header, packet.value.reliableExt, payload.value,
                                    buffer.data(), buffer.size()),
                  buffer, out.reencoded);
      break;
    }
    case PacketType::kKeepAlive: {
      const DecodeFailure failure = net::decodeKeepAlive(bytes, size);
      if (failure != DecodeFailure::kOk) {
        out.failure = failure;
        return out;
      }
      keepEncoded(net::encodeKeepAlive(packet.value.header, packet.value.reliableExt, buffer.data(),
                                       buffer.size()),
                  buffer, out.reencoded);
      break;
    }
    case PacketType::kCommand: {
      const auto payload = net::decodeCommand(bytes, size);
      if (!payload.isOk) {
        out.failure = payload.failure;
        return out;
      }
      addField(out.fields, "movex", static_cast<uint8_t>(payload.value.moveX));
      addField(out.fields, "movey", static_cast<uint8_t>(payload.value.moveY));
      addField(out.fields, "yaw", payload.value.yaw);
      addField(out.fields, "pitch", payload.value.pitch);
      addField(out.fields, "buttons", payload.value.buttons);
      addField(out.fields, "switchto", payload.value.switchTo);
      addField(out.fields, "cmdseq", payload.value.seq);
      addField(out.fields, "clienttick", payload.value.clientTick);
      keepEncoded(net::encodeCommand(packet.value.header, packet.value.reliableExt, payload.value,
                                     buffer.data(), buffer.size()),
                  buffer, out.reencoded);
      break;
    }
    case PacketType::kSnapshot: {
      const auto view = net::decodeSnapshot(bytes, size);
      if (!view.isOk) {
        out.failure = view.failure;
        return out;
      }
      out.snapshot = view.value;
      addField(out.fields, "tick", view.value.tick);
      addField(out.fields, "servertimems", view.value.serverTimeMs);
      addField(out.fields, "lastackedseq", view.value.lastAckedSeq);
      addField(out.fields, "baselinetick", view.value.baselineTick);
      addField(out.fields, "count", view.value.records.size());
      addField(out.fields, "removedcount", view.value.removedIds.size());
      addField(out.fields, "eventcount", view.value.events.size());
      if (!view.value.records.empty()) addRecordFields(out.fields, view.value.records.front());
      if (!view.value.removedIds.empty()) addField(out.fields, "removedid", view.value.removedIds.front());
      if (!view.value.events.empty()) addEventFields(out.fields, view.value.events.front());
      if (view.value.baselineTick == 0u) {
        const SnapshotFrame frame{view.value.tick,       view.value.serverTimeMs,
                                  view.value.lastAckedSeq, nullptr,
                                  view.value.records.data(), view.value.records.size(),
                                  view.value.events.data(),  view.value.events.size()};
        keepEncoded(net::encodeSnapshot(packet.value.header, frame, buffer.data(), buffer.size()),
                    buffer, out.reencoded);
        out.hasRoundtrip = true;
      }
      break;
    }
    case PacketType::kEvent: {
      const auto frame = net::decodeEventFrame(bytes, size);
      if (!frame.isOk) {
        out.failure = frame.failure;
        return out;
      }
      addField(out.fields, "tick", frame.value.tick);
      addField(out.fields, "count", frame.value.events.size());
      if (!frame.value.events.empty()) addEventFields(out.fields, frame.value.events.front());
      keepEncoded(net::encodeEventFrame(packet.value.header, packet.value.reliableExt, frame.value.tick,
                                        frame.value.events.data(), frame.value.events.size(),
                                        buffer.data(), buffer.size()),
                  buffer, out.reencoded);
      break;
    }
    case PacketType::kFragment: {
      const uint8_t* payload = net::packetPayload(packet.value, bytes, size);
      net::ByteWriter writer(buffer.data(), buffer.size());
      net::writeHeader(writer, packet.value.header);
      if (packet.value.hasReliableExt) net::writeReliableExt(writer, packet.value.reliableExt);
      net::writeFragmentHeader(writer, packet.value.fragment);
      writer.writeBytes(payload, packet.value.payloadBytes);
      keepEncoded(net::EncodeResult{!writer.isOverflow, writer.size()}, buffer, out.reencoded);
      break;
    }
    case PacketType::kMatchState: {
      const auto state = net::decodeMatchState(bytes, size);
      if (!state.isOk) {
        out.failure = state.failure;
        return out;
      }
      addField(out.fields, "phase", state.value.phase);
      addField(out.fields, "wave", state.value.wave);
      addField(out.fields, "intermissionms", state.value.intermissionMs);
      addField(out.fields, "count", state.value.players.size());
      keepEncoded(net::encodeMatchState(packet.value.header, packet.value.reliableExt, state.value,
                                        buffer.data(), buffer.size()),
                  buffer, out.reencoded);
      break;
    }
    case PacketType::kDisconnect:
    default: {
      const auto payload = net::decodeDisconnect(bytes, size);
      if (!payload.isOk) {
        out.failure = payload.failure;
        return out;
      }
      addField(out.fields, "reason", payload.value.reason);
      keepEncoded(net::encodeDisconnect(packet.value.header, packet.value.reliableExt, payload.value,
                                        buffer.data(), buffer.size()),
                  buffer, out.reencoded);
      break;
    }
  }
  out.isDecoded = true;
  return out;
}

bool isSameBytes(const std::vector<uint8_t>& actual, const std::vector<uint8_t>& expected) {
  return actual.size() == expected.size() &&
         std::equal(actual.begin(), actual.end(), expected.begin());
}

// 单个 fixture 的全部断言：expect 行逐键比对 + 重编码逐字节相等 + 打印 "hex <name> ok"。
bool runHexFixture(const char* name) {
  Fixture fixture;
  if (!loadFixtureOrFail(name, fixture)) {
    AC_FAIL("fixture 未能加载");
    return false;
  }
  bool isOk = true;
  const FixtureDecode decoded = decodeFixture(fixture);
  for (const auto& item : fixture.expect) {
    const uint64_t value = parseNumber(item.second);
    if (item.first == "bytes") {
      AC_CHECK_EQ(decoded.byteCount, value);
      isOk = isOk && decoded.byteCount == value;
      continue;
    }
    if (item.first == "failure") {
      AC_CHECK(!decoded.isDecoded);
      AC_CHECK_EQ(static_cast<uint64_t>(decoded.failure), value);
      isOk = isOk && !decoded.isDecoded && static_cast<uint64_t>(decoded.failure) == value;
      continue;
    }
    isOk = expectField(decoded.fields, item.first, value) && isOk;
  }
  if (decoded.isDecoded && decoded.hasRoundtrip) {
    const bool isRoundtrip = isSameBytes(decoded.reencoded, fixture.bytes);
    AC_CHECK(isRoundtrip);
    isOk = isOk && isRoundtrip;
  }
  if (isOk) std::printf("hex %s ok\n", name);
  return isOk;
}

}  // namespace

// ---------- §5.1 小端读写（--filter=wire）----------

AC_TEST(wire_little_endian_u16) {
  uint8_t buffer[8] = {};
  net::ByteWriter writer(buffer, sizeof buffer);
  writer.writeU16(0x1234u);
  writer.writeI16(-2);
  AC_CHECK_EQ(writer.size(), 4u);
  AC_CHECK_EQ(buffer[0], 0x34u);
  AC_CHECK_EQ(buffer[1], 0x12u);
  AC_CHECK_EQ(buffer[2], 0xFEu);
  AC_CHECK_EQ(buffer[3], 0xFFu);
  net::ByteReader reader(buffer, writer.size());
  AC_CHECK_EQ(reader.readU16(), 0x1234u);
  AC_CHECK_EQ(reader.readI16(), -2);
  AC_CHECK(!reader.isTruncated);
  AC_CHECK_EQ(reader.remaining(), 0u);
}

AC_TEST(wire_roundtrip_all_widths) {
  uint8_t buffer[64] = {};
  const uint8_t slice[3] = {0xAAu, 0xBBu, 0xCCu};
  net::ByteWriter writer(buffer, sizeof buffer);
  writer.writeU8(0xFFu);
  writer.writeI8(-1);
  writer.writeU16(0xFFFFu);
  writer.writeI16(-32768);
  writer.writeU32(0xFFFFFFFFu);
  writer.writeI32(-2147483647 - 1);
  writer.writeBytes(slice, sizeof slice);
  AC_CHECK(!writer.isOverflow);
  AC_CHECK_EQ(writer.size(), 1u + 1u + 2u + 2u + 4u + 4u + 3u);
  net::ByteReader reader(buffer, writer.size());
  AC_CHECK_EQ(reader.readU8(), 0xFFu);
  AC_CHECK_EQ(reader.readI8(), -1);
  AC_CHECK_EQ(reader.readU16(), 0xFFFFu);
  AC_CHECK_EQ(reader.readI16(), -32768);
  AC_CHECK_EQ(reader.readU32(), 0xFFFFFFFFu);
  AC_CHECK_EQ(reader.readI32(), -2147483647 - 1);
  uint8_t copy[3] = {};
  AC_CHECK(reader.readBytes(copy, sizeof copy));
  AC_CHECK_EQ(copy[0], 0xAAu);
  AC_CHECK_EQ(copy[2], 0xCCu);
  AC_CHECK(!reader.isTruncated);
  AC_CHECK_EQ(reader.remaining(), 0u);
}

AC_TEST(wire_bounds_and_payload_offset) {
  const uint8_t short3[3] = {0x11u, 0x22u, 0x33u};
  net::ByteReader reader(short3, sizeof short3);
  AC_CHECK_EQ(reader.readU16(), 0x2211u);
  AC_CHECK_EQ(reader.readU16(), 0x33u);  // 越界：缺失的高位按 0 拼，绝不越界解引用
  AC_CHECK(reader.isTruncated);         // 判据只有这个标志，调用方不得信任半读的值
  AC_CHECK_EQ(reader.offset, 3u);       // 已读出的低字节被消费，缺失的高字节不推进游标
  net::ByteReader empty(nullptr, 0u);
  AC_CHECK_EQ(empty.readU32(), 0u);
  AC_CHECK(empty.isTruncated);

  uint8_t small[3] = {};
  net::ByteWriter writer(small, sizeof small);
  writer.writeU32(0x11223344u);
  AC_CHECK(writer.isOverflow);
  AC_CHECK_EQ(writer.size(), 3u);  // 只写到容量边界，绝不越界
  AC_CHECK_EQ(small[2], 0x22u);  // 写满容量即停：第 4 个字节没有落盘

  AC_CHECK_EQ(net::payloadOffset(makeHeader(PacketType::kSnapshot, 0u, 0u, 0u)), 8u);
  AC_CHECK_EQ(net::payloadOffset(makeHeader(PacketType::kCommand, net::kFlagReliable, 0u, 0u)), 20u);
  AC_CHECK_EQ(net::payloadOffset(makeHeader(PacketType::kFragment, net::kFlagMoreFragments, 0u, 0u)), 12u);
  AC_CHECK_EQ(net::payloadOffset(makeHeader(PacketType::kFragment,
                                            net::kFlagReliable | net::kFlagMoreFragments, 0u, 0u)),
              24u);
}

// ---------- §5.2–§5.6 字段级编解码（--filter=codec）----------

AC_TEST(codec_command_roundtrip) {
  const PacketHeader header = makeHeader(PacketType::kCommand, net::kFlagReliable, 0x123u, 9u);
  const ReliableExt ext = makeExt(0x11223344u, 0x55667788u, 0x99AABBCCu);
  const CommandPayload command{-127, 127, 0xFFFFu, 0x8000u, 0xFFu, 2u, 65535u, 0xDEADBEEFu};
  uint8_t buffer[64] = {};
  const auto encoded = net::encodeCommand(header, ext, command, buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 34u);
  AC_CHECK_EQ(encoded.bytes, net::kCommandPacketBytes);
  const auto decoded = net::decodeCommand(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK(decoded.value.moveX == command.moveX);
  AC_CHECK(decoded.value.moveY == command.moveY);
  AC_CHECK_EQ(decoded.value.yaw, command.yaw);
  AC_CHECK_EQ(decoded.value.pitch, command.pitch);
  AC_CHECK_EQ(decoded.value.buttons, command.buttons);
  AC_CHECK_EQ(decoded.value.switchTo, command.switchTo);
  AC_CHECK_EQ(decoded.value.seq, command.seq);
  AC_CHECK_EQ(decoded.value.clientTick, command.clientTick);
  const auto packet = net::decodePacket(buffer, encoded.bytes);
  AC_CHECK(packet.isOk);
  AC_CHECK_EQ(packet.value.header.version, net::kProtocolVersion);
  AC_CHECK_EQ(packet.value.header.session, 0x123u);
  AC_CHECK_EQ(packet.value.header.seq, 9u);
  AC_CHECK(packet.value.hasReliableExt);
  AC_CHECK_EQ(packet.value.reliableExt.msgId, 0x11223344u);
  AC_CHECK_EQ(packet.value.reliableExt.ackBits, 0x99AABBCCu);
  AC_CHECK_EQ(packet.value.payloadBytes, net::kCommandPayloadBytes);
  AC_CHECK_EQ(net::payloadOffset(packet.value.header), 20u);
}

AC_TEST(codec_command_layout_bytes) {
  // 与 fixture 独立的第二份字节真相：字段顺序/端序改错时这里先红。
  const uint8_t expected[34] = {0x01u, 0x04u, 0x01u, 0x00u, 0x23u, 0x01u, 0x02u, 0x00u,
                                0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                                0x00u, 0x00u, 0x00u, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x40u,
                                0x00u, 0x00u, 0x09u, 0x01u, 0x07u, 0x00u, 0x39u, 0x30u,
                                0x00u, 0x00u};
  const PacketHeader header = makeHeader(PacketType::kCommand, net::kFlagReliable, 0x123u, 2u);
  const CommandPayload command{127, 0, 0x4000u, 0u, 0x09u, 1u, 7u, 12345u};
  uint8_t buffer[64] = {};
  const auto encoded = net::encodeCommand(header, makeExt(1u, 0u, 0u), command, buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, sizeof expected);
  AC_CHECK(isSameBytes(std::vector<uint8_t>(buffer, buffer + encoded.bytes),
                       std::vector<uint8_t>(std::begin(expected), std::end(expected))));
  const auto decoded = net::decodeCommand(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.yaw, 0x4000u);
  AC_CHECK_EQ(decoded.value.buttons, 0x09u);
  AC_CHECK_EQ(decoded.value.clientTick, 12345u);
}

AC_TEST(codec_command_truncated) {
  const uint8_t full[34] = {0x01u, 0x04u, 0x01u, 0x00u, 0x23u, 0x01u, 0x02u, 0x00u,
                            0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                            0x00u, 0x00u, 0x00u, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x40u,
                            0x00u, 0x00u, 0x09u, 0x01u, 0x07u, 0x00u, 0x39u, 0x30u,
                            0x00u, 0x00u};
  AC_CHECK(net::decodeCommand(full, sizeof full).isOk);
  const std::size_t truncatedLengths[6] = {0u, 4u, 7u, 8u, 12u, 19u};
  for (std::size_t length : truncatedLengths) {
    const auto decoded = net::decodeCommand(full, length);
    AC_CHECK(!decoded.isOk);
    AC_CHECK(decoded.failure == DecodeFailure::kTruncated);
  }
  const auto shortPayload = net::decodeCommand(full, 33u);
  AC_CHECK(!shortPayload.isOk);
  AC_CHECK(shortPayload.failure == DecodeFailure::kBadLength);
}

AC_TEST(codec_command_bad_version) {
  uint8_t buffer[34] = {0x01u, 0x04u, 0x01u, 0x00u, 0x23u, 0x01u, 0x02u, 0x00u,
                        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                        0x00u, 0x00u, 0x00u, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x40u,
                        0x00u, 0x00u, 0x09u, 0x01u, 0x07u, 0x00u, 0x39u, 0x30u,
                        0x00u, 0x00u};
  buffer[0] = 2u;
  const auto decoded = net::decodeCommand(buffer, sizeof buffer);
  AC_CHECK(!decoded.isOk);
  AC_CHECK(decoded.failure == DecodeFailure::kBadVersion);
  const auto packet = net::decodePacket(buffer, sizeof buffer);
  AC_CHECK(!packet.isOk);
  AC_CHECK(packet.failure == DecodeFailure::kBadVersion);
}

AC_TEST(codec_command_bad_type) {
  uint8_t buffer[34] = {0x01u, 0x04u, 0x01u, 0x00u, 0x23u, 0x01u, 0x02u, 0x00u,
                        0x01u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x00u,
                        0x00u, 0x00u, 0x00u, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x40u,
                        0x00u, 0x00u, 0x09u, 0x01u, 0x07u, 0x00u, 0x39u, 0x30u,
                        0x00u, 0x00u};
  buffer[1] = 5u;  // 换成快照类型（快照不可靠，flags 必须一起清零）
  buffer[2] = 0u;
  buffer[3] = 0u;
  const auto wrongType = net::decodeCommand(buffer, sizeof buffer);
  AC_CHECK(!wrongType.isOk);
  AC_CHECK(wrongType.failure == DecodeFailure::kBadType);
  buffer[1] = 11u;  // 未知类型码
  const auto badType = net::decodePacket(buffer, sizeof buffer);
  AC_CHECK(!badType.isOk);
  AC_CHECK(badType.failure == DecodeFailure::kBadType);
  buffer[1] = 4u;
  buffer[2] = 0u;  // 命令通道必须置 reliable
  buffer[3] = 0u;
  const auto badFlags = net::decodePacket(buffer, sizeof buffer);
  AC_CHECK(!badFlags.isOk);
  AC_CHECK(badFlags.failure == DecodeFailure::kBadValue);
  const auto badFlagsCommand = net::decodeCommand(buffer, sizeof buffer);
  AC_CHECK(!badFlagsCommand.isOk);
  AC_CHECK(badFlagsCommand.failure == DecodeFailure::kBadValue);
}

AC_TEST(codec_packet_header_offsets) {
  const PacketHeader header =
      makeHeader(PacketType::kFragment, static_cast<uint16_t>(net::kFlagReliable | net::kFlagMoreFragments),
                 0x123u, 9u);
  const ReliableExt ext = makeExt(0x11223344u, 0x55667788u, 0x99AABBCCu);
  const net::FragmentHeader fragment{7u, 1u, 2u};
  const uint8_t slice[3] = {0xAAu, 0xBBu, 0xCCu};
  uint8_t buffer[64] = {};
  net::ByteWriter writer(buffer, sizeof buffer);
  net::writeHeader(writer, header);
  net::writeReliableExt(writer, ext);
  net::writeFragmentHeader(writer, fragment);
  writer.writeBytes(slice, sizeof slice);
  AC_CHECK(!writer.isOverflow);
  AC_CHECK_EQ(writer.size(), 8u + 12u + 4u + 3u);
  AC_CHECK_EQ(net::payloadOffset(header), 24u);
  AC_CHECK_EQ(buffer[8], 0x44u);   // 可靠扩展头在前
  AC_CHECK_EQ(buffer[20], 0x07u);  // 分片头在后（fragId=7 小端）
  AC_CHECK_EQ(buffer[22], 0x01u);  // fragIndex
  AC_CHECK_EQ(buffer[23], 0x02u);  // fragCount
  const auto decoded = net::decodePacket(buffer, writer.size());
  AC_CHECK(decoded.isOk);
  AC_CHECK(decoded.value.hasReliableExt);
  AC_CHECK(decoded.value.hasFragmentHeader);
  AC_CHECK_EQ(decoded.value.reliableExt.msgId, 0x11223344u);
  AC_CHECK_EQ(decoded.value.reliableExt.ackBase, 0x55667788u);
  AC_CHECK_EQ(decoded.value.fragment.fragId, 7u);
  AC_CHECK_EQ(decoded.value.fragment.fragIndex, 1u);
  AC_CHECK_EQ(decoded.value.fragment.fragCount, 2u);
  AC_CHECK_EQ(decoded.value.payloadBytes, 3u);
  const uint8_t* payload = net::packetPayload(decoded.value, buffer, writer.size());
  AC_CHECK(payload != nullptr);
  AC_CHECK_EQ(payload[0], 0xAAu);
  // 分片头自洽性：fragCount 上限与 index < count
  uint8_t bad[64] = {};
  std::memcpy(bad, buffer, writer.size());
  bad[23] = 9u;
  AC_CHECK(net::decodePacket(bad, writer.size()).failure == DecodeFailure::kBadValue);
  bad[23] = 2u;
  bad[22] = 2u;
  AC_CHECK(net::decodePacket(bad, writer.size()).failure == DecodeFailure::kBadValue);
}

AC_TEST(codec_snapshot_full_roundtrip) {
  const std::vector<EntityRecord> records = makeRecords(2u);
  const std::vector<EventEntry> events{EventEntry{7u, net::PhaseChangeEvent{2u, 3u, 250u}}};
  const SnapshotFrame frame{100u, 5000u, 9u, nullptr, records.data(), records.size(),
                            events.data(), events.size()};
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeSnapshot(
      makeHeader(PacketType::kSnapshot, 0u, 0x123u, 5u), frame, buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 22u + 1u + 2u * 15u + 1u + 0u + 1u + 9u);  // phaseChange 条目 9 字节
  const auto decoded = net::decodeSnapshot(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.tick, 100u);
  AC_CHECK_EQ(decoded.value.serverTimeMs, 5000u);
  AC_CHECK_EQ(decoded.value.lastAckedSeq, 9u);
  AC_CHECK_EQ(decoded.value.baselineTick, 0u);
  AC_CHECK_EQ(decoded.value.records.size(), 2u);
  AC_CHECK(decoded.value.records == records);
  AC_CHECK_EQ(decoded.value.removedIds.size(), 0u);
  AC_CHECK_EQ(decoded.value.events.size(), 1u);
  AC_CHECK(decoded.value.events == events);
}

AC_TEST(codec_snapshot_diff_add_remove) {
  std::vector<EntityRecord> baselineRecords = makeRecords(3u);  // id 1..3
  const SnapshotBaseline baseline{99u, baselineRecords};
  std::vector<EntityRecord> current{baselineRecords[1]};  // id 2 保留
  current[0].xCm = static_cast<int16_t>(current[0].xCm + 7);  // 但内容变了
  current.push_back(makeRecords(4u).back());                   // 新增 id 4
  const SnapshotFrame frame{100u, 5000u, 3u, &baseline, current.data(), current.size(), nullptr, 0u};
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeSnapshot(
      makeHeader(PacketType::kSnapshot, 0u, 0x123u, 6u), frame, buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  const auto decoded = net::decodeSnapshot(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.baselineTick, 99u);
  AC_CHECK_EQ(decoded.value.records.size(), 2u);  // 变化的 id 2 + 新增的 id 4
  AC_CHECK_EQ(decoded.value.records[0].id, 2u);
  AC_CHECK_EQ(decoded.value.records[1].id, 4u);
  AC_CHECK_EQ(decoded.value.removedIds.size(), 2u);  // id 1 与 id 3 消失
  AC_CHECK_EQ(decoded.value.removedIds[0], 1u);
  AC_CHECK_EQ(decoded.value.removedIds[1], 3u);
}

AC_TEST(codec_snapshot_no_change_is_empty) {
  const std::vector<EntityRecord> records = makeRecords(4u);
  const SnapshotBaseline baseline{99u, records};
  const SnapshotFrame frame{100u, 5000u, 1u, &baseline, records.data(), records.size(), nullptr, 0u};
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeSnapshot(
      makeHeader(PacketType::kSnapshot, 0u, 0x123u, 7u), frame, buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 22u + 1u + 0u + 1u + 0u + 1u);
  const auto decoded = net::decodeSnapshot(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.records.size(), 0u);
  AC_CHECK_EQ(decoded.value.removedIds.size(), 0u);
  AC_CHECK_EQ(decoded.value.events.size(), 0u);
  AC_CHECK_EQ(decoded.value.baselineTick, 99u);
}

AC_TEST(codec_snapshot_truncated) {
  const std::vector<EntityRecord> records = makeRecords(2u);
  const std::vector<EventEntry> events{EventEntry{1u, net::WaveStartEvent{2u, 30u}}};
  const SnapshotFrame frame{100u, 5000u, 9u, nullptr, records.data(), records.size(),
                            events.data(), events.size()};
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeSnapshot(
      makeHeader(PacketType::kSnapshot, 0u, 0x123u, 5u), frame, buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK(net::decodeSnapshot(buffer, encoded.bytes).isOk);
  // 任何长度不足都必须失败（严格消费到帧尾），且不得越界
  for (std::size_t length = 0; length < encoded.bytes; ++length) {
    const auto decoded = net::decodeSnapshot(buffer, length);
    AC_CHECK(!decoded.isOk);
    AC_CHECK(decoded.failure == DecodeFailure::kTruncated ||
             decoded.failure == DecodeFailure::kBadLength);
  }
  // 帧尾多一个字节：必须拒绝（count/长度自洽）
  std::vector<uint8_t> longer(buffer, buffer + encoded.bytes);
  longer.push_back(0x00u);
  AC_CHECK(!net::decodeSnapshot(longer.data(), longer.size()).isOk);
}

AC_TEST(codec_event_entry_bytes_all_types) {
  const std::size_t payloadBytes[10] = {14u, 6u, 4u, 6u, 3u, 7u, 5u, 5u, 7u, 5u};
  const std::size_t entryBytes[10] = {18u, 10u, 8u, 10u, 7u, 11u, 9u, 9u, 11u, 9u};
  for (uint8_t type = 1u; type <= 10u; ++type) {
    AC_CHECK_EQ(net::eventPayloadWithTypeBytes(type), payloadBytes[type - 1u]);
    AC_CHECK_EQ(net::eventEntryBytes(type), entryBytes[type - 1u]);
    AC_CHECK_EQ(net::eventEntryBytes(type), payloadBytes[type - 1u] + 4u);
  }
  // 钉住「载荷类型 ↔ 线号」：eventTypeOf 取 variant 下标 + 1，重排 EventData 会立刻变红。
  const EventEntry samples[10] = {
      EventEntry{0u, net::PlayerHitEvent{}},      EventEntry{0u, net::SheepKilledEvent{}},
      EventEntry{0u, net::WaveStartEvent{}},      EventEntry{0u, net::WaveClearEvent{}},
      EventEntry{0u, net::PlayerDownedEvent{}},   EventEntry{0u, net::ReviveProgressEvent{}},
      EventEntry{0u, net::ReviveDoneEvent{}},     EventEntry{0u, net::RageActivatedEvent{}},
      EventEntry{0u, net::MatchEndedEvent{}},     EventEntry{0u, net::PhaseChangeEvent{}},
  };
  for (uint8_t type = 1u; type <= 10u; ++type) {
    AC_CHECK_EQ(net::eventTypeOf(samples[type - 1u]), type);
  }
  AC_CHECK_EQ(net::eventPayloadWithTypeBytes(0u), 0u);
  AC_CHECK_EQ(net::eventPayloadWithTypeBytes(11u), 0u);
  AC_CHECK_EQ(net::eventEntryBytes(11u), 0u);
  // 单条目事件帧总长 = 8 + 12 + 4 + 1 + 条目总长
  const EventEntry entry{1u, net::PlayerHitEvent{1u, 2u, 3u, 0u, 4u, 5u, 6u}};
  uint8_t buffer[128] = {};
  const auto encoded = net::encodeEventFrame(makeHeader(PacketType::kEvent, net::kFlagReliable, 1u, 1u),
                                             makeExt(1u, 0u, 0u), 100u, &entry, 1u, buffer,
                                             sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 8u + 12u + 4u + 1u + 18u);
}

AC_TEST(codec_event_frame_roundtrip) {
  std::vector<EventEntry> events{
      EventEntry{1u, net::PlayerHitEvent{3u, 17u, 25u, 5u, 120, 100, -250}},
      EventEntry{2u, net::SheepKilledEvent{9u, 3u, 2u}},
      EventEntry{3u, net::WaveStartEvent{4u, 250u}},
      EventEntry{4u, net::WaveClearEvent{4u, 90000u}},
      EventEntry{5u, net::PlayerDownedEvent{3u}},
      EventEntry{6u, net::ReviveProgressEvent{3u, 8u, 40000u}},
      EventEntry{7u, net::ReviveDoneEvent{3u, 8u}},
      EventEntry{8u, net::RageActivatedEvent{3u, 8000u}},
      EventEntry{9u, net::MatchEndedEvent{1u, 10u, 600000u}},
      EventEntry{10u, net::PhaseChangeEvent{2u, 5u, 4500u}},
  };
  uint8_t buffer[512] = {};
  const auto encoded = net::encodeEventFrame(makeHeader(PacketType::kEvent, net::kFlagReliable, 1u, 4u),
                                             makeExt(9u, 8u, 7u), 100u, events.data(), events.size(),
                                             buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 8u + 12u + 4u + 1u + (18u + 10u + 8u + 10u + 7u + 11u + 9u + 9u + 11u + 9u));
  const auto decoded = net::decodeEventFrame(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.tick, 100u);
  AC_CHECK_EQ(decoded.value.events.size(), 10u);
  AC_CHECK(decoded.value.events == events);
  AC_CHECK_EQ(decoded.value.duplicateCount, 0u);
  // 单个条目字段级抽查（类型 1 的命中点与类型 10 的相位）
  const auto* hit = std::get_if<net::PlayerHitEvent>(&decoded.value.events[0].data);
  AC_CHECK(hit != nullptr);
  if (hit != nullptr) {
    AC_CHECK_EQ(hit->subjectId, 3u);
    AC_CHECK_EQ(hit->value, 25u);
    AC_CHECK_EQ(hit->hitX, 120);
    AC_CHECK_EQ(hit->hitZ, -250);
    AC_CHECK_EQ(hit->flags, 5u);
  }
  const auto* phase = std::get_if<net::PhaseChangeEvent>(&decoded.value.events[9].data);
  AC_CHECK(phase != nullptr);
  if (phase != nullptr) AC_CHECK_EQ(phase->intermissionMs, 4500u);
}

AC_TEST(codec_event_unknown_type) {
  uint8_t buffer[64] = {};
  net::ByteWriter writer(buffer, sizeof buffer);
  net::writeHeader(writer, makeHeader(PacketType::kEvent, net::kFlagReliable, 1u, 1u));
  net::writeReliableExt(writer, makeExt(1u, 0u, 0u));
  writer.writeU32(100u);
  writer.writeU8(1u);
  writer.writeU32(1u);
  writer.writeU8(11u);  // 未知事件类型
  AC_CHECK(!writer.isOverflow);
  const auto decoded = net::decodeEventFrame(buffer, writer.size());
  AC_CHECK(!decoded.isOk);
  AC_CHECK(decoded.failure == DecodeFailure::kUnknownEvent);
  buffer[28] = 0u;  // 类型 0 同样未知
  AC_CHECK(net::decodeEventFrame(buffer, writer.size()).failure == DecodeFailure::kUnknownEvent);
}

AC_TEST(codec_event_id_dedup) {
  const std::vector<EventEntry> events{
      EventEntry{5u, net::WaveStartEvent{1u, 10u}},
      EventEntry{5u, net::WaveStartEvent{1u, 10u}},  // 同帧内重复
      EventEntry{6u, net::WaveClearEvent{1u, 60000u}},
      EventEntry{4u, net::PlayerDownedEvent{3u}},  // 倒退（已应用过更大的 id）
      EventEntry{6u, net::WaveClearEvent{1u, 60000u}},
  };
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeEventFrame(makeHeader(PacketType::kEvent, net::kFlagReliable, 1u, 1u),
                                             makeExt(1u, 0u, 0u), 100u, events.data(), events.size(),
                                             buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  const auto withoutTracker = net::decodeEventFrame(buffer, encoded.bytes);
  AC_CHECK(withoutTracker.isOk);
  AC_CHECK_EQ(withoutTracker.value.events.size(), 5u);
  net::EventIdTracker tracker{0u};
  const auto withTracker = net::decodeEventFrame(buffer, encoded.bytes, &tracker);
  AC_CHECK(withTracker.isOk);  // 重复不是错误：静默丢弃
  AC_CHECK_EQ(withTracker.value.events.size(), 2u);
  AC_CHECK_EQ(withTracker.value.duplicateCount, 3u);
  AC_CHECK_EQ(withTracker.value.events[0].eventId, 5u);
  AC_CHECK_EQ(withTracker.value.events[1].eventId, 6u);
  AC_CHECK_EQ(tracker.maxEventId, 6u);
  // 快照事件块走同一条去重路径
  const std::vector<EventEntry> repeated{EventEntry{1u, net::WaveStartEvent{1u, 10u}},
                                         EventEntry{1u, net::WaveStartEvent{1u, 10u}}};
  const SnapshotFrame frame{100u, 5000u, 1u, nullptr, nullptr, 0u, repeated.data(), repeated.size()};
  uint8_t snapshotBuffer[256] = {};
  const auto snapshotEncoded = net::encodeSnapshot(
      makeHeader(PacketType::kSnapshot, 0u, 1u, 1u), frame, snapshotBuffer, sizeof snapshotBuffer);
  AC_CHECK(snapshotEncoded.isOk);
  net::EventIdTracker snapshotTracker{0u};
  const auto snapshotDecoded = net::decodeSnapshot(snapshotBuffer, snapshotEncoded.bytes, &snapshotTracker);
  AC_CHECK(snapshotDecoded.isOk);
  AC_CHECK_EQ(snapshotDecoded.value.events.size(), 1u);
  AC_CHECK_EQ(snapshotDecoded.value.duplicateEventCount, 1u);
}

// ---------- §5.5 字节级 fixture（--filter=hex）----------

AC_TEST(hex_hello) { AC_CHECK(runHexFixture("hello")); }
AC_TEST(hex_hello_ack) { AC_CHECK(runHexFixture("hello_ack")); }
AC_TEST(hex_resume) {
  AC_CHECK(runHexFixture("resume"));
  // 令牌必须是小写十六进制 ASCII（ADR-009）：大写或非十六进制字符一律 kBadValue
  Fixture fixture;
  if (!loadFixtureOrFail("resume", fixture)) return;
  std::vector<uint8_t> tampered = fixture.bytes;
  tampered[tampered.size() - 8u] = static_cast<uint8_t>('A');
  const auto upper = net::decodeResume(tampered.data(), tampered.size());
  AC_CHECK(!upper.isOk);
  AC_CHECK(upper.failure == DecodeFailure::kBadValue);
  tampered[tampered.size() - 1u] = static_cast<uint8_t>('z');
  const auto notHex = net::decodeResume(tampered.data(), tampered.size());
  AC_CHECK(!notHex.isOk);
  AC_CHECK(notHex.failure == DecodeFailure::kBadValue);
}
AC_TEST(hex_keepalive) { AC_CHECK(runHexFixture("keepalive")); }
AC_TEST(hex_command) { AC_CHECK(runHexFixture("command")); }
AC_TEST(hex_snapshot_full) { AC_CHECK(runHexFixture("snapshot_full")); }
AC_TEST(hex_event_hit) { AC_CHECK(runHexFixture("event_hit")); }
AC_TEST(hex_fragment) { AC_CHECK(runHexFixture("fragment")); }
AC_TEST(hex_truncated_command) { AC_CHECK(runHexFixture("truncated_command")); }

AC_TEST(hex_snapshot_diff) {
  AC_CHECK(runHexFixture("snapshot_diff"));
  // 差分包的重编码需要基线：该 fixture 相对 tick 99 只移除 id=3，所以基线 = {tick 99, [id 3]}。
  Fixture fixture;
  if (!loadFixtureOrFail("snapshot_diff", fixture)) return;
  const FixtureDecode decoded = decodeFixture(fixture);
  AC_CHECK(decoded.isDecoded);
  SnapshotBaseline baseline{99u, {}};
  EntityRecord removed{};
  removed.id = 3u;
  baseline.records.push_back(removed);
  std::vector<uint8_t> buffer(net::kMaxSnapshotBytes, 0u);
  const SnapshotFrame frame{decoded.snapshot.tick,         decoded.snapshot.serverTimeMs,
                            decoded.snapshot.lastAckedSeq, &baseline,
                            nullptr,                       0u,
                            nullptr,                       0u};
  const auto encoded = net::encodeSnapshot(decoded.info.header, frame, buffer.data(), buffer.size());
  AC_CHECK(encoded.isOk);
  buffer.resize(encoded.bytes);
  AC_CHECK(isSameBytes(buffer, fixture.bytes));
  AC_CHECK_EQ(encoded.bytes, fixture.bytes.size());
}

// ---------- 随机损坏（--filter=fuzz）----------

AC_TEST(fuzz_command_byte_flip) {
  Fixture fixture;
  if (!loadFixtureOrFail("command", fixture)) return;
  ac::Rng rng = makeFuzzRng();
  std::size_t successes = 0;
  for (int iteration = 0; iteration < 1200; ++iteration) {
    std::vector<uint8_t> corrupted = fixture.bytes;
    const std::size_t index = below(rng, static_cast<uint32_t>(corrupted.size()));
    corrupted[index] =
        static_cast<uint8_t>(corrupted[index] ^ static_cast<uint8_t>(1u + below(rng, 255u)));
    const auto decoded = net::decodeCommand(corrupted.data(), corrupted.size());
    if (!decoded.isOk) {
      AC_CHECK(decoded.failure != DecodeFailure::kOk);
      continue;
    }
    ++successes;
    const auto packet = net::decodePacket(corrupted.data(), corrupted.size());
    AC_CHECK(packet.isOk);
    if (!packet.isOk) continue;
    uint8_t buffer[64] = {};
    const auto reencoded = net::encodeCommand(packet.value.header, packet.value.reliableExt,
                                              decoded.value, buffer, sizeof buffer);
    AC_CHECK(reencoded.isOk);
    if (reencoded.isOk) {
      AC_CHECK(isSameBytes(std::vector<uint8_t>(buffer, buffer + reencoded.bytes), corrupted));
    }
  }
  AC_CHECK(successes > 0u);  // 绝大多数翻字节会失败，但必须存在成功路径（否则用例是假绿）
}

AC_TEST(fuzz_snapshot_multi_byte_flip) {
  Fixture fixture;
  if (!loadFixtureOrFail("snapshot_full", fixture)) return;
  ac::Rng rng = makeFuzzRng();
  std::size_t successes = 0;
  for (int iteration = 0; iteration < 1200; ++iteration) {
    std::vector<uint8_t> corrupted = fixture.bytes;
    const uint32_t flips = 1u + below(rng, 3u);
    for (uint32_t i = 0; i < flips; ++i) {
      const std::size_t index = below(rng, static_cast<uint32_t>(corrupted.size()));
      corrupted[index] =
          static_cast<uint8_t>(corrupted[index] ^ static_cast<uint8_t>(1u + below(rng, 255u)));
    }
    const auto decoded = net::decodeSnapshot(corrupted.data(), corrupted.size());
    if (!decoded.isOk) {
      AC_CHECK(decoded.failure != DecodeFailure::kOk);
      AC_CHECK(decoded.failure == DecodeFailure::kTruncated ||
               decoded.failure == DecodeFailure::kBadLength ||
               decoded.failure == DecodeFailure::kBadValue ||
               decoded.failure == DecodeFailure::kUnknownEvent ||
               decoded.failure == DecodeFailure::kBadType ||
               decoded.failure == DecodeFailure::kBadVersion);
      continue;
    }
    ++successes;
    // 结构自洽：解出的段长必须正好等于输入长度（说明没有越界读、也没有"补 0 继续"）
    std::size_t expected = net::kSnapshotHeadBytes + 1u +
                           net::kEntityRecordBytes * decoded.value.records.size() + 1u +
                           2u * decoded.value.removedIds.size() + 1u;
    for (const EventEntry& entry : decoded.value.events) {
      expected += net::eventEntryBytes(net::eventTypeOf(entry));
    }
    AC_CHECK_EQ(expected, corrupted.size());
    AC_CHECK(decoded.value.records.size() <= 255u);
    AC_CHECK(decoded.value.events.size() <= 255u);
  }
  AC_CHECK(successes > 0u);
}

// 三种载荷各自的"载荷解码必须失败"判据（用具名类型而不是字符串派发）。
bool rejectsCommand(const uint8_t* bytes, std::size_t size) {
  return !net::decodeCommand(bytes, size).isOk;
}
bool rejectsSnapshot(const uint8_t* bytes, std::size_t size) {
  return !net::decodeSnapshot(bytes, size).isOk;
}
bool rejectsEventFrame(const uint8_t* bytes, std::size_t size) {
  return !net::decodeEventFrame(bytes, size).isOk;
}

struct FuzzTarget {
  const char* name;
  bool (*rejects)(const uint8_t* bytes, std::size_t size);
};

const FuzzTarget kFuzzTargets[3] = {
    {"command", rejectsCommand},
    {"snapshot_full", rejectsSnapshot},
    {"event_hit", rejectsEventFrame},
};

AC_TEST(fuzz_truncated_prefixes) {
  for (const FuzzTarget& target : kFuzzTargets) {
    Fixture fixture;
    if (!loadFixtureOrFail(target.name, fixture)) continue;
    const auto whole = net::decodePacket(fixture.bytes.data(), fixture.bytes.size());
    AC_CHECK(whole.isOk);
    if (!whole.isOk) continue;
    for (std::size_t length = 0; length < fixture.bytes.size(); ++length) {
      const auto packet = net::decodePacket(fixture.bytes.data(), length);
      if (length < whole.value.payloadOffset) {
        AC_CHECK(!packet.isOk);
        AC_CHECK(packet.failure == DecodeFailure::kTruncated);
      }
      AC_CHECK(target.rejects(fixture.bytes.data(), length));
    }
  }
  // 随机前缀：任何长度都必须只返回失败原因
  ac::Rng rng = makeFuzzRng();
  Fixture fixtures[3];
  for (std::size_t i = 0; i < 3u; ++i) (void)loadFixtureOrFail(kFuzzTargets[i].name, fixtures[i]);
  std::size_t iterations = 0;
  for (int round = 0; round < 1200; ++round) {
    const std::size_t pick = below(rng, 3u);
    Fixture& fixture = fixtures[pick];
    if (fixture.bytes.empty()) continue;
    const std::size_t length = below(rng, static_cast<uint32_t>(fixture.bytes.size()));
    const auto packet = net::decodePacket(fixture.bytes.data(), length);
    if (!packet.isOk) {
      AC_CHECK(packet.failure == DecodeFailure::kTruncated ||
               packet.failure == DecodeFailure::kBadValue);
    }
    AC_CHECK(kFuzzTargets[pick].rejects(fixture.bytes.data(), length));
    ++iterations;
  }
  AC_CHECK(iterations >= 1000u);
}

// ---------- 尺寸预算（--filter=size）----------

AC_TEST(size_command_packet_is_34) {
  const CommandPayload command{0, 0, 0u, 0u, 0u, 0u, 0u, 0u};
  uint8_t buffer[64] = {};
  const auto encoded = net::encodeCommand(makeHeader(PacketType::kCommand, net::kFlagReliable, 1u, 1u),
                                          makeExt(1u, 0u, 0u), command, buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 34u);
  AC_CHECK_EQ(net::kCommandPacketBytes, 34u);
  AC_CHECK(encoded.bytes <= net::kMaxPacketBytes);
}

AC_TEST(size_full_single_entity_snapshot_is_40) {
  const std::vector<EntityRecord> records = makeRecords(1u);
  const SnapshotFrame frame{100u, 5000u, 7u, nullptr, records.data(), records.size(), nullptr, 0u};
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeSnapshot(makeHeader(PacketType::kSnapshot, 0u, 0x123u, 5u), frame,
                                           buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 40u);
  AC_CHECK_EQ(net::kSnapshotHeadBytes + 1u + net::kEntityRecordBytes + 1u + 1u, 40u);
  AC_CHECK(encoded.bytes <= net::kMaxPacketBytes);
}

AC_TEST(size_steady_snapshot_within_1228) {
  const std::vector<EntityRecord> full = makeRecords(128u);
  const PacketHeader header = makeHeader(PacketType::kSnapshot, 0u, 7u, 1u);
  std::vector<uint8_t> buffer(net::kMaxSnapshotBytes + 64u, 0u);
  const SnapshotFrame fullFrame{100u, 5000u, 0u, nullptr, full.data(), full.size(), nullptr, 0u};
  const auto fullResult = net::encodeSnapshot(header, fullFrame, buffer.data(), buffer.size());
  AC_CHECK(fullResult.isOk);
  AC_CHECK_EQ(fullResult.bytes, 22u + 1u + 128u * 15u + 1u + 1u);
  AC_CHECK(fullResult.bytes <= net::kMaxSnapshotBytes);
  AC_CHECK(fullResult.bytes > net::kMaxPacketBytes);  // 必须分片（S04）

  // 稳态：基线 128 实体，其中 40 个变化、无移除、2 个事件
  const SnapshotBaseline baseline{99u, full};
  std::vector<EntityRecord> steady = full;
  for (std::size_t i = 0; i < 40u; ++i) {
    steady[i].xCm = static_cast<int16_t>(steady[i].xCm + 1);
  }
  const std::vector<EventEntry> events{EventEntry{1u, net::PhaseChangeEvent{2u, 3u, 250u}},
                                       EventEntry{2u, net::WaveStartEvent{3u, 12u}}};
  const SnapshotFrame steadyFrame{100u,      5000u,         7u,           &baseline,
                                  steady.data(), steady.size(), events.data(), events.size()};
  const auto steadyResult = net::encodeSnapshot(header, steadyFrame, buffer.data(), buffer.size());
  AC_CHECK(steadyResult.isOk);
  AC_CHECK(steadyResult.bytes <= net::kSteadySnapshotBytes);
  const auto decoded = net::decodeSnapshot(buffer.data(), steadyResult.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.records.size(), 40u);  // 确实是差分而不是全量
  AC_CHECK_EQ(decoded.value.events.size(), 2u);
  AC_CHECK_EQ(decoded.value.baselineTick, 99u);
}

AC_TEST(size_single_packet_needs_fragments) {
  const std::vector<EntityRecord> full = makeRecords(128u);
  const PacketHeader header = makeHeader(PacketType::kSnapshot, 0u, 7u, 1u);
  std::vector<uint8_t> buffer(net::kMaxSnapshotBytes + 64u, 0u);
  const SnapshotFrame fullFrame{100u, 5000u, 0u, nullptr, full.data(), full.size(), nullptr, 0u};
  const auto fullResult = net::encodeSnapshot(header, fullFrame, buffer.data(), buffer.size());
  AC_CHECK(fullResult.isOk);
  const std::size_t fragments = (fullResult.bytes + net::kMaxPacketBytes - 1u) / net::kMaxPacketBytes;
  AC_CHECK_EQ(fragments, 2u);
  AC_CHECK(net::kMaxLogicalMessageBytes >= fragments * net::kMaxPacketBytes);
  // 容量不足：必须拒绝而不是写越界
  std::vector<uint8_t> small(fullResult.bytes - 1u, 0u);
  AC_CHECK(!net::encodeSnapshot(header, fullFrame, small.data(), small.size()).isOk);
  // 超过单快照上限（128 实体 + 64 个最大事件）：必须拒绝
  std::vector<EventEntry> manyEvents;
  for (uint32_t i = 0; i < 64u; ++i) {
    manyEvents.push_back(EventEntry{i + 1u, net::PlayerHitEvent{1u, 2u, 3u, 0u, 4, 5, 6}});
  }
  const SnapshotFrame oversizeFrame{100u, 5000u, 0u, nullptr, full.data(), full.size(),
                                    manyEvents.data(), manyEvents.size()};
  AC_CHECK(!net::encodeSnapshot(header, oversizeFrame, buffer.data(), buffer.size()).isOk);
  // 单帧事件上限
  AC_CHECK(!net::encodeEventFrame(makeHeader(PacketType::kEvent, net::kFlagReliable, 1u, 1u),
                                  makeExt(1u, 0u, 0u), 1u, manyEvents.data(), 65u, buffer.data(),
                                  buffer.size())
                .isOk);
  // 单帧实体记录上限
  std::vector<EntityRecord> tooMany = makeRecords(129u);
  const SnapshotFrame tooManyFrame{100u, 5000u, 0u, nullptr, tooMany.data(), tooMany.size(), nullptr, 0u};
  AC_CHECK(!net::encodeSnapshot(header, tooManyFrame, buffer.data(), buffer.size()).isOk);
}

// ---------- §5.7 MatchState（--filter=match）----------

namespace {

MatchStatePlayer makePlayer(uint16_t pid, const char* name, uint8_t weapon) {
  MatchStatePlayer player{};
  player.pid = pid;
  player.name = name;
  player.ready = 1u;
  player.weapon = weapon;
  player.hpRatio = 180u;
  player.kills = 12u;
  player.mag = 30u;
  player.reserve = 90u;
  player.reloadLeft10Ms = 7u;
  player.rage = 1u;
  player.rageLeft100Ms = 55u;
  player.downed = 0u;
  player.reviveRatio255 = 255u;
  return player;
}

std::size_t matchStateFrameBytes(const net::MatchState& state) {
  std::size_t total = 8u + 12u + 5u;
  for (const MatchStatePlayer& player : state.players) total += net::matchStatePlayerBytes(player);
  return total;
}

}  // namespace

AC_TEST(match_roundtrip_two_players) {
  net::MatchState state{2u, 3u, 4500u, {}};
  state.players.push_back(makePlayer(7u, "alice", 2u));
  state.players.push_back(makePlayer(9u, "bob", 0u));
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeMatchState(
      makeHeader(PacketType::kMatchState, net::kFlagReliable, 0x123u, 4u), makeExt(1u, 0u, 0u), state,
      buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 8u + 12u + 5u + (16u + 5u) + (16u + 3u));
  AC_CHECK_EQ(encoded.bytes, matchStateFrameBytes(state));
  AC_CHECK(encoded.bytes <= net::kMatchStateMaxBytes);
  const auto decoded = net::decodeMatchState(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK_EQ(decoded.value.phase, 2u);
  AC_CHECK_EQ(decoded.value.wave, 3u);
  AC_CHECK_EQ(decoded.value.intermissionMs, 4500u);
  AC_CHECK(decoded.value == state);
  AC_CHECK(decoded.value.players[0].name == "alice");
  AC_CHECK_EQ(decoded.value.players[0].weapon, 2u);
  AC_CHECK_EQ(decoded.value.players[1].kills, 12u);
  AC_CHECK_EQ(decoded.value.players[1].reviveRatio255, 255u);
}

AC_TEST(match_name_one_byte_boundary) {
  net::MatchState state{0u, 0u, 0u, {}};
  state.players.push_back(makePlayer(1u, "a", 0u));
  uint8_t buffer[128] = {};
  const auto encoded = net::encodeMatchState(
      makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u), makeExt(1u, 0u, 0u), state,
      buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 8u + 12u + 5u + 16u + 1u);
  const auto decoded = net::decodeMatchState(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK(decoded.value == state);
  AC_CHECK_EQ(decoded.value.players[0].name.size(), 1u);
  // 名称为空：拒收（§5.7）
  net::MatchState empty{0u, 0u, 0u, {}};
  empty.players.push_back(makePlayer(1u, "", 0u));
  AC_CHECK(!net::encodeMatchState(makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u),
                                  makeExt(1u, 0u, 0u), empty, buffer, sizeof buffer)
                .isOk);
  uint8_t crafted[64] = {};
  net::ByteWriter writer(crafted, sizeof crafted);
  net::writeHeader(writer, makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u));
  net::writeReliableExt(writer, makeExt(1u, 0u, 0u));
  writer.writeU8(0u);
  writer.writeU8(0u);
  writer.writeU16(0u);
  writer.writeU8(1u);
  writer.writeU16(1u);
  writer.writeU8(0u);  // nameLen = 0
  const auto badName = net::decodeMatchState(crafted, writer.size());
  AC_CHECK(!badName.isOk);
  AC_CHECK(badName.failure == DecodeFailure::kBadValue);
}

AC_TEST(match_name_twelve_byte_boundary) {
  net::MatchState state{1u, 2u, 0u, {}};
  state.players.push_back(makePlayer(1u, "abcdefghijkl", 1u));   // 12 字节 ASCII 上限
  state.players.push_back(makePlayer(2u, "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD", 2u));  // 12 字节 UTF-8
  uint8_t buffer[256] = {};
  const auto encoded = net::encodeMatchState(
      makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u), makeExt(1u, 0u, 0u), state,
      buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK_EQ(encoded.bytes, 8u + 12u + 5u + (16u + 12u) + (16u + 12u));
  const auto decoded = net::decodeMatchState(buffer, encoded.bytes);
  AC_CHECK(decoded.isOk);
  AC_CHECK(decoded.value == state);
  AC_CHECK_EQ(decoded.value.players[0].name.size(), 12u);
  AC_CHECK_EQ(decoded.value.players[1].name.size(), 12u);
  // 13 字节名称：编码与解码两侧都拒收
  net::MatchState tooLong{1u, 2u, 0u, {}};
  tooLong.players.push_back(makePlayer(1u, "abcdefghijklm", 1u));
  AC_CHECK(!net::encodeMatchState(makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u),
                                  makeExt(1u, 0u, 0u), tooLong, buffer, sizeof buffer)
                .isOk);
  uint8_t crafted[64] = {};
  net::ByteWriter writer(crafted, sizeof crafted);
  net::writeHeader(writer, makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u));
  net::writeReliableExt(writer, makeExt(1u, 0u, 0u));
  writer.writeU8(1u);
  writer.writeU8(2u);
  writer.writeU16(0u);
  writer.writeU8(1u);
  writer.writeU16(1u);
  writer.writeU8(13u);  // nameLen = 13
  const auto badName = net::decodeMatchState(crafted, writer.size());
  AC_CHECK(!badName.isOk);
  AC_CHECK(badName.failure == DecodeFailure::kBadValue);
}

AC_TEST(match_count_above_four_rejected) {
  uint8_t crafted[64] = {};
  net::ByteWriter writer(crafted, sizeof crafted);
  net::writeHeader(writer, makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u));
  net::writeReliableExt(writer, makeExt(1u, 0u, 0u));
  writer.writeU8(2u);
  writer.writeU8(3u);
  writer.writeU16(1000u);
  writer.writeU8(5u);  // count = 5 > maxPlayersPerRoom
  const auto decoded = net::decodeMatchState(crafted, writer.size());
  AC_CHECK(!decoded.isOk);
  AC_CHECK(decoded.failure == DecodeFailure::kBadValue);
  net::MatchState state{2u, 3u, 1000u, {}};
  for (uint16_t i = 0; i < 5u; ++i) state.players.push_back(makePlayer(static_cast<uint16_t>(i + 1u), "p", 0u));
  uint8_t buffer[256] = {};
  AC_CHECK(!net::encodeMatchState(makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u),
                                  makeExt(1u, 0u, 0u), state, buffer, sizeof buffer)
                .isOk);
}

AC_TEST(match_bad_weapon_rejected) {
  uint8_t crafted[128] = {};
  net::ByteWriter writer(crafted, sizeof crafted);
  net::writeHeader(writer, makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u));
  net::writeReliableExt(writer, makeExt(1u, 0u, 0u));
  writer.writeU8(2u);
  writer.writeU8(1u);
  writer.writeU16(0u);
  writer.writeU8(1u);
  writer.writeU16(4u);
  writer.writeU8(2u);
  writer.writeBytes("ok", 2u);
  writer.writeU8(1u);  // ready
  writer.writeU8(3u);  // weapon = 3 非法
  writer.writeU8(255u);
  writer.writeU16(1u);
  writer.writeU8(10u);
  writer.writeU16(20u);
  writer.writeU8(0u);
  writer.writeU8(0u);
  writer.writeU8(0u);
  writer.writeU8(0u);
  writer.writeU8(0u);
  const auto badWeapon = net::decodeMatchState(crafted, writer.size());
  AC_CHECK(!badWeapon.isOk);
  AC_CHECK(badWeapon.failure == DecodeFailure::kBadValue);
}

AC_TEST(match_truncated_and_trailing_rejected) {
  net::MatchState good{2u, 1u, 0u, {}};
  good.players.push_back(makePlayer(4u, "ok", 1u));
  uint8_t buffer[128] = {};
  const auto encoded = net::encodeMatchState(
      makeHeader(PacketType::kMatchState, net::kFlagReliable, 1u, 1u), makeExt(1u, 0u, 0u), good,
      buffer, sizeof buffer);
  AC_CHECK(encoded.isOk);
  AC_CHECK(net::decodeMatchState(buffer, encoded.bytes).isOk);
  AC_CHECK_EQ(net::decodeMatchState(buffer, encoded.bytes - 1u).failure, DecodeFailure::kTruncated);
  // 帧尾多余字节：拒收
  std::vector<uint8_t> longer(buffer, buffer + encoded.bytes);
  longer.push_back(0u);
  AC_CHECK(net::decodeMatchState(longer.data(), longer.size()).failure == DecodeFailure::kBadLength);
}


