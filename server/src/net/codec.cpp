#include "net/codec.hpp"

#include <algorithm>
#include <type_traits>

namespace ac::net {
namespace {

bool isPacketTypeValid(uint8_t type) noexcept {
  return type >= kMinPacketType && type <= kMaxPacketType;
}

// §5.1 通道与类型映射：flags 必须与类型一致。分片包（type 9）例外：必须置 moreFragments，
// reliable 由被分片的通道决定（快照不可靠 / 事件可靠）。
bool isFlagsValidForType(uint8_t type, uint16_t flags) noexcept {
  const auto packetType = static_cast<PacketType>(type);
  if (packetType == PacketType::kFragment) {
    const uint16_t allowed = static_cast<uint16_t>(~static_cast<uint16_t>(kFlagReliable | kFlagMoreFragments));
    return (flags & kFlagMoreFragments) != 0u && (flags & allowed) == 0u;
  }
  return flags == requiredFlags(packetType);
}

// 编码入口的包头校验：版本、类型、flags 三者必须自洽（§5.1）。
bool isEncodeHeaderValid(const PacketHeader& header, PacketType expectedType) noexcept {
  if (header.version != kProtocolVersion) return false;
  if (header.type != static_cast<uint8_t>(expectedType)) return false;
  if (!isPacketTypeValid(header.type)) return false;
  return isFlagsValidForType(header.type, header.flags);
}

ByteWriter payloadWriter(const PacketHeader& header, uint8_t* out, std::size_t capacity) noexcept {
  ByteWriter writer(out, capacity);
  writeHeader(writer, header);
  return writer;
}

// 头 + 扩展头：可靠包的固定前缀（命令/事件/MatchState/HelloAck/Resume/Disconnect/KeepAlive）。
ByteWriter reliableWriter(const PacketHeader& header, const ReliableExt& ext, uint8_t* out,
                          std::size_t capacity) noexcept {
  ByteWriter writer = payloadWriter(header, out, capacity);
  writeReliableExt(writer, ext);
  return writer;
}

// 载荷读取器：只在载荷长度已知且合法时构造（调用方先做长度/类型检查）。
ByteReader payloadReader(const PacketInfo& info, const uint8_t* bytes, std::size_t size) noexcept {
  const uint8_t* payload = packetPayload(info, bytes, size);
  if (payload == nullptr) return ByteReader(nullptr, 0u);
  return ByteReader(payload, info.payloadBytes);
}

EntityRecord readEntityRecord(ByteReader& reader) noexcept {
  EntityRecord record{};
  record.id = reader.readU16();
  record.kindFlags = reader.readU8();
  record.xCm = reader.readI16();
  record.yCm = reader.readI16();
  record.zCm = reader.readI16();
  record.yawUnits = reader.readU16();
  record.pitchUnits = reader.readU16();
  record.hpRatioUnits = reader.readU8();
  record.state = reader.readU8();
  return record;
}

void writeEntityRecord(ByteWriter& writer, const EntityRecord& record) noexcept {
  writer.writeU16(record.id);
  writer.writeU8(record.kindFlags);
  writer.writeI16(record.xCm);
  writer.writeI16(record.yCm);
  writer.writeI16(record.zCm);
  writer.writeU16(record.yawUnits);
  writer.writeU16(record.pitchUnits);
  writer.writeU8(record.hpRatioUnits);
  writer.writeU8(record.state);
}

// reconnectToken 的线上形：8 位小写十六进制 ASCII（ADR-009）。大写或非十六进制一律拒收。
void writeToken(ByteWriter& writer, uint32_t token) noexcept {
  constexpr char kHexDigits[] = "0123456789abcdef";
  for (int shift = 28; shift >= 0; shift -= 4) {
    writer.writeU8(static_cast<uint8_t>(kHexDigits[(token >> static_cast<unsigned>(shift)) & 0x0Fu]));
  }
}

bool readToken(ByteReader& reader, uint32_t& token) noexcept {
  token = 0u;
  for (std::size_t i = 0; i < kTokenBytes; ++i) {
    const uint8_t byte = reader.readU8();
    if (reader.isTruncated) return false;
    uint32_t digit = 0u;
    if (byte >= '0' && byte <= '9') {
      digit = static_cast<uint32_t>(byte - '0');
    } else if (byte >= 'a' && byte <= 'f') {
      digit = static_cast<uint32_t>(byte - 'a' + 10);
    } else {
      return false;  // 大写或非十六进制字符：拒收
    }
    token = (token << 4) | digit;
  }
  return true;
}

DecodeResult<EventEntry> decodeEventEntry(ByteReader& reader) noexcept {
  EventEntry entry{};
  entry.eventId = reader.readU32();
  const uint8_t type = reader.readU8();
  if (reader.isTruncated) return decodedFail<EventEntry>(DecodeFailure::kTruncated);
  switch (static_cast<EventType>(type)) {
    case EventType::kPlayerHit: {
      PlayerHitEvent payload{};
      payload.subjectId = reader.readU16();
      payload.targetId = reader.readU16();
      payload.value = reader.readU16();
      payload.flags = reader.readU8();
      payload.hitX = reader.readI16();
      payload.hitY = reader.readI16();
      payload.hitZ = reader.readI16();
      entry.data = payload;
      break;
    }
    case EventType::kSheepKilled: {
      SheepKilledEvent payload{};
      payload.targetId = reader.readU16();
      payload.subjectId = reader.readU16();
      payload.kind = reader.readU8();
      entry.data = payload;
      break;
    }
    case EventType::kWaveStart: {
      WaveStartEvent payload{};
      payload.wave = reader.readU8();
      payload.budget = reader.readU16();
      entry.data = payload;
      break;
    }
    case EventType::kWaveClear: {
      WaveClearEvent payload{};
      payload.wave = reader.readU8();
      payload.elapsedMs = reader.readU32();
      entry.data = payload;
      break;
    }
    case EventType::kPlayerDowned: {
      PlayerDownedEvent payload{};
      payload.subjectId = reader.readU16();
      entry.data = payload;
      break;
    }
    case EventType::kReviveProgress: {
      ReviveProgressEvent payload{};
      payload.targetId = reader.readU16();
      payload.subjectId = reader.readU16();
      payload.ratio255 = reader.readU16();
      entry.data = payload;
      break;
    }
    case EventType::kReviveDone: {
      ReviveDoneEvent payload{};
      payload.targetId = reader.readU16();
      payload.subjectId = reader.readU16();
      entry.data = payload;
      break;
    }
    case EventType::kRageActivated: {
      RageActivatedEvent payload{};
      payload.subjectId = reader.readU16();
      payload.durationMs = reader.readU16();
      entry.data = payload;
      break;
    }
    case EventType::kMatchEnded: {
      MatchEndedEvent payload{};
      payload.reason = reader.readU8();
      payload.wave = reader.readU8();
      payload.durationMs = reader.readU32();
      entry.data = payload;
      break;
    }
    case EventType::kPhaseChange: {
      PhaseChangeEvent payload{};
      payload.phase = reader.readU8();
      payload.wave = reader.readU8();
      payload.intermissionMs = reader.readU16();
      entry.data = payload;
      break;
    }
    default:
      // §5.4 / C02 §5.2：未知事件类型终止本帧解析。
      return decodedFail<EventEntry>(DecodeFailure::kUnknownEvent);
  }
  if (reader.isTruncated) return decodedFail<EventEntry>(DecodeFailure::kTruncated);
  return decodedOk(entry);
}

void writeEventEntry(ByteWriter& writer, const EventEntry& entry) noexcept {
  writer.writeU32(entry.eventId);
  writer.writeU8(eventTypeOf(entry));
  std::visit(
      [&writer](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, PlayerHitEvent>) {
          writer.writeU16(payload.subjectId);
          writer.writeU16(payload.targetId);
          writer.writeU16(payload.value);
          writer.writeU8(payload.flags);
          writer.writeI16(payload.hitX);
          writer.writeI16(payload.hitY);
          writer.writeI16(payload.hitZ);
        } else if constexpr (std::is_same_v<Payload, SheepKilledEvent>) {
          writer.writeU16(payload.targetId);
          writer.writeU16(payload.subjectId);
          writer.writeU8(payload.kind);
        } else if constexpr (std::is_same_v<Payload, WaveStartEvent>) {
          writer.writeU8(payload.wave);
          writer.writeU16(payload.budget);
        } else if constexpr (std::is_same_v<Payload, WaveClearEvent>) {
          writer.writeU8(payload.wave);
          writer.writeU32(payload.elapsedMs);
        } else if constexpr (std::is_same_v<Payload, PlayerDownedEvent>) {
          writer.writeU16(payload.subjectId);
        } else if constexpr (std::is_same_v<Payload, ReviveProgressEvent>) {
          writer.writeU16(payload.targetId);
          writer.writeU16(payload.subjectId);
          writer.writeU16(payload.ratio255);
        } else if constexpr (std::is_same_v<Payload, ReviveDoneEvent>) {
          writer.writeU16(payload.targetId);
          writer.writeU16(payload.subjectId);
        } else if constexpr (std::is_same_v<Payload, RageActivatedEvent>) {
          writer.writeU16(payload.subjectId);
          writer.writeU16(payload.durationMs);
        } else if constexpr (std::is_same_v<Payload, MatchEndedEvent>) {
          writer.writeU8(payload.reason);
          writer.writeU8(payload.wave);
          writer.writeU32(payload.durationMs);
        } else {
          static_assert(std::is_same_v<Payload, PhaseChangeEvent>, "事件载荷变体新增分支时必须同步 writeEventEntry");
          writer.writeU8(payload.phase);
          writer.writeU8(payload.wave);
          writer.writeU16(payload.intermissionMs);
        }
      },
      entry.data);
}

// 差分判据（§5.3）：基线不存在该 id，或 15 字节记录与基线不同。
bool isRecordChanged(const SnapshotBaseline* baseline, const EntityRecord& record) noexcept {
  if (baseline == nullptr) return true;
  const EntityRecord* previous = baseline->find(record.id);
  return previous == nullptr || !(*previous == record);
}

// 当前帧是否仍存在该 id（records 已由编码器校验为按 id 升序）。
bool isIdPresent(const EntityRecord* records, std::size_t count, uint16_t id) noexcept {
  std::size_t low = 0;
  std::size_t high = count;
  while (low < high) {
    const std::size_t mid = low + (high - low) / 2u;
    if (records[mid].id < id) {
      low = mid + 1u;
    } else {
      high = mid;
    }
  }
  return low < count && records[low].id == id;
}

}  // namespace

bool EventIdTracker::isNew(uint32_t eventId) noexcept {
  if (eventId == 0u || eventId <= maxEventId) return false;
  maxEventId = eventId;
  return true;
}

const EntityRecord* SnapshotBaseline::find(uint16_t id) const noexcept {
  for (const EntityRecord& record : records) {
    if (record.id == id) return &record;
  }
  return nullptr;
}

std::size_t eventPayloadWithTypeBytes(uint8_t type) noexcept {
  switch (static_cast<EventType>(type)) {
    case EventType::kPlayerHit:
      return 14u;
    case EventType::kSheepKilled:
      return 6u;
    case EventType::kWaveStart:
      return 4u;
    case EventType::kWaveClear:
      return 6u;
    case EventType::kPlayerDowned:
      return 3u;
    case EventType::kReviveProgress:
      return 7u;
    case EventType::kReviveDone:
      return 5u;
    case EventType::kRageActivated:
      return 5u;
    case EventType::kMatchEnded:
      return 7u;
    case EventType::kPhaseChange:
      return 5u;
    default:
      return 0u;
  }
}

std::size_t eventEntryBytes(uint8_t type) noexcept {
  const std::size_t payload = eventPayloadWithTypeBytes(type);
  return payload == 0u ? 0u : payload + kEventIdHeaderBytes;
}

uint8_t eventTypeOf(const EventEntry& entry) noexcept {
  return static_cast<uint8_t>(entry.data.index() + 1u);
}

DecodeResult<PacketInfo> decodePacket(const uint8_t* bytes, std::size_t size) noexcept {
  if (bytes == nullptr || size < kCommonHeaderBytes) {
    return decodedFail<PacketInfo>(DecodeFailure::kTruncated);
  }
  PacketInfo info{};
  ByteReader reader(bytes, size);
  info.header = readHeader(reader);
  if (info.header.version != kProtocolVersion) {
    return decodedFail<PacketInfo>(DecodeFailure::kBadVersion);
  }
  if (!isPacketTypeValid(info.header.type)) {
    return decodedFail<PacketInfo>(DecodeFailure::kBadType);
  }
  if (!isFlagsValidForType(info.header.type, info.header.flags)) {
    return decodedFail<PacketInfo>(DecodeFailure::kBadValue);
  }
  info.hasReliableExt = info.header.isReliable();
  info.hasFragmentHeader = info.header.hasMoreFragments();
  info.payloadOffset = payloadOffset(info.header);
  if (size < info.payloadOffset) {
    return decodedFail<PacketInfo>(DecodeFailure::kTruncated);
  }
  if (info.hasReliableExt) info.reliableExt = readReliableExt(reader);
  if (info.hasFragmentHeader) {
    info.fragment = readFragmentHeader(reader);
    if (info.fragment.fragCount == 0u || info.fragment.fragCount > kMaxFragments) {
      return decodedFail<PacketInfo>(DecodeFailure::kBadValue);
    }
    if (info.fragment.fragIndex >= info.fragment.fragCount) {
      return decodedFail<PacketInfo>(DecodeFailure::kBadValue);
    }
  }
  info.payloadBytes = size - info.payloadOffset;
  return decodedOk(info);
}

const uint8_t* packetPayload(const PacketInfo& info, const uint8_t* bytes, std::size_t size) noexcept {
  if (bytes == nullptr || size < info.payloadOffset) return nullptr;
  return bytes + info.payloadOffset;
}

EncodeResult encodeCommand(const PacketHeader& header, const ReliableExt& ext,
                           const CommandPayload& command, uint8_t* out,
                           std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kCommand)) return encodeFail();
  if (capacity < kCommandPacketBytes) return encodeFail();
  ByteWriter writer = reliableWriter(header, ext, out, capacity);
  writer.writeI8(command.moveX);
  writer.writeI8(command.moveY);
  writer.writeU16(command.yaw);
  writer.writeU16(command.pitch);
  writer.writeU8(static_cast<uint8_t>(command.buttons & 0xFFu));
  writer.writeU8(command.switchTo);
  writer.writeU16(command.seq);
  writer.writeU32(command.clientTick);
  if (writer.isOverflow || writer.size() != kCommandPacketBytes) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<CommandPayload> decodeCommand(const uint8_t* bytes, std::size_t size) noexcept {
  const auto packet = decodePacket(bytes, size);
  if (!packet.isOk) return decodedFail<CommandPayload>(packet.failure);
  if (packet.value.header.type != static_cast<uint8_t>(PacketType::kCommand)) {
    return decodedFail<CommandPayload>(DecodeFailure::kBadType);
  }
  if (packet.value.payloadBytes != kCommandPayloadBytes) {
    return decodedFail<CommandPayload>(DecodeFailure::kBadLength);
  }
  ByteReader reader = payloadReader(packet.value, bytes, size);
  CommandPayload command{};
  command.moveX = reader.readI8();
  command.moveY = reader.readI8();
  command.yaw = reader.readU16();
  command.pitch = reader.readU16();
  command.buttons = reader.readU8();
  command.switchTo = reader.readU8();
  command.seq = reader.readU16();
  command.clientTick = reader.readU32();
  if (reader.isTruncated) return decodedFail<CommandPayload>(DecodeFailure::kTruncated);
  return decodedOk(command);
}

EncodeResult encodeEventFrame(const PacketHeader& header, const ReliableExt& ext, uint32_t tick,
                              const EventEntry* events, std::size_t eventCount, uint8_t* out,
                              std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kEvent)) return encodeFail();
  if (eventCount > kMaxEventsPerFrame) return encodeFail();
  if (events == nullptr && eventCount > 0u) return encodeFail();
  std::size_t payloadBytes = 4u + 1u;
  for (std::size_t i = 0; i < eventCount; ++i) {
    const std::size_t entryBytes = eventEntryBytes(eventTypeOf(events[i]));
    if (entryBytes == 0u) return encodeFail();
    payloadBytes += entryBytes;
  }
  const std::size_t total = kCommonHeaderBytes + kReliableExtBytes + payloadBytes;
  if (total > capacity || total > kMaxPacketBytes) return encodeFail();
  ByteWriter writer = reliableWriter(header, ext, out, capacity);
  writer.writeU32(tick);
  writer.writeU8(static_cast<uint8_t>(eventCount));
  for (std::size_t i = 0; i < eventCount; ++i) writeEventEntry(writer, events[i]);
  if (writer.isOverflow || writer.size() != total) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<EventFrame> decodeEventFrame(const uint8_t* bytes, std::size_t size,
                                          EventIdTracker* tracker) noexcept {
  const auto packet = decodePacket(bytes, size);
  if (!packet.isOk) return decodedFail<EventFrame>(packet.failure);
  if (packet.value.header.type != static_cast<uint8_t>(PacketType::kEvent)) {
    return decodedFail<EventFrame>(DecodeFailure::kBadType);
  }
  if (packet.value.payloadBytes < 5u) return decodedFail<EventFrame>(DecodeFailure::kTruncated);
  ByteReader reader = payloadReader(packet.value, bytes, size);
  EventFrame frame{};
  frame.duplicateCount = 0u;
  frame.tick = reader.readU32();
  const uint8_t count = reader.readU8();
  if (reader.isTruncated) return decodedFail<EventFrame>(DecodeFailure::kTruncated);
  frame.events.reserve(count);
  for (uint8_t i = 0; i < count; ++i) {
    const auto entry = decodeEventEntry(reader);
    if (!entry.isOk) return decodedFail<EventFrame>(entry.failure);
    if (tracker != nullptr && !tracker->isNew(entry.value.eventId)) {
      ++frame.duplicateCount;
      continue;
    }
    frame.events.push_back(entry.value);
  }
  if (reader.isTruncated) return decodedFail<EventFrame>(DecodeFailure::kTruncated);
  if (reader.remaining() != 0u) return decodedFail<EventFrame>(DecodeFailure::kBadLength);
  return decodedOk(frame);
}

EncodeResult encodeSnapshot(const PacketHeader& header, const SnapshotFrame& frame, uint8_t* out,
                            std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kSnapshot)) return encodeFail();
  if (frame.recordCount > kMaxEntityRecordsPerFrame) return encodeFail();
  if (frame.eventCount > kMaxEventsPerFrame) return encodeFail();
  if (frame.records == nullptr && frame.recordCount > 0u) return encodeFail();
  if (frame.events == nullptr && frame.eventCount > 0u) return encodeFail();
  for (std::size_t i = 1; i < frame.recordCount; ++i) {
    if (frame.records[i].id <= frame.records[i - 1u].id) return encodeFail();  // 必须按 id 升序且唯一
  }

  const SnapshotBaseline* baseline = frame.baseline;
  if (baseline != nullptr && baseline->tick == 0u) baseline = nullptr;  // tick 0 = 全量
  const uint32_t baselineTick = baseline == nullptr ? 0u : baseline->tick;

  std::size_t changedCount = 0;
  for (std::size_t i = 0; i < frame.recordCount; ++i) {
    if (isRecordChanged(baseline, frame.records[i])) ++changedCount;
  }
  std::size_t removedCount = 0;
  if (baseline != nullptr) {
    for (const EntityRecord& previous : baseline->records) {
      if (!isIdPresent(frame.records, frame.recordCount, previous.id)) ++removedCount;
    }
  }
  if (removedCount > kMaxRemovedPerFrame) return encodeFail();

  std::size_t payloadBytes = 4u + 4u + 2u + 4u + 1u + kEntityRecordBytes * changedCount + 1u +
                             2u * removedCount + 1u;
  for (std::size_t i = 0; i < frame.eventCount; ++i) {
    const std::size_t entryBytes = eventEntryBytes(eventTypeOf(frame.events[i]));
    if (entryBytes == 0u) return encodeFail();
    payloadBytes += entryBytes;
  }
  const std::size_t total = kCommonHeaderBytes + payloadBytes;
  if (total > capacity || total > kMaxSnapshotBytes) return encodeFail();

  ByteWriter writer = payloadWriter(header, out, capacity);
  writer.writeU32(frame.tick);
  writer.writeU32(frame.serverTimeMs);
  writer.writeU16(frame.lastAckedSeq);
  writer.writeU32(baselineTick);
  writer.writeU8(static_cast<uint8_t>(changedCount));
  for (std::size_t i = 0; i < frame.recordCount; ++i) {
    if (isRecordChanged(baseline, frame.records[i])) writeEntityRecord(writer, frame.records[i]);
  }
  writer.writeU8(static_cast<uint8_t>(removedCount));
  if (baseline != nullptr) {
    for (const EntityRecord& previous : baseline->records) {
      if (!isIdPresent(frame.records, frame.recordCount, previous.id)) writer.writeU16(previous.id);
    }
  }
  writer.writeU8(static_cast<uint8_t>(frame.eventCount));
  for (std::size_t i = 0; i < frame.eventCount; ++i) writeEventEntry(writer, frame.events[i]);
  if (writer.isOverflow || writer.size() != total) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<SnapshotView> decodeSnapshot(const uint8_t* bytes, std::size_t size,
                                          EventIdTracker* tracker) noexcept {
  const auto packet = decodePacket(bytes, size);
  if (!packet.isOk) return decodedFail<SnapshotView>(packet.failure);
  if (packet.value.header.type != static_cast<uint8_t>(PacketType::kSnapshot)) {
    return decodedFail<SnapshotView>(DecodeFailure::kBadType);
  }
  if (packet.value.payloadBytes < (kSnapshotHeadBytes - kCommonHeaderBytes) + 3u) {
    return decodedFail<SnapshotView>(DecodeFailure::kTruncated);
  }
  ByteReader reader = payloadReader(packet.value, bytes, size);
  SnapshotView view{};
  view.duplicateEventCount = 0u;
  view.tick = reader.readU32();
  view.serverTimeMs = reader.readU32();
  view.lastAckedSeq = reader.readU16();
  view.baselineTick = reader.readU32();
  const uint8_t recordCount = reader.readU8();
  if (reader.isTruncated) return decodedFail<SnapshotView>(DecodeFailure::kTruncated);
  view.records.reserve(recordCount);
  for (uint8_t i = 0; i < recordCount; ++i) {
    view.records.push_back(readEntityRecord(reader));
  }
  if (reader.isTruncated) return decodedFail<SnapshotView>(DecodeFailure::kTruncated);
  const uint8_t removedCount = reader.readU8();
  if (reader.isTruncated) return decodedFail<SnapshotView>(DecodeFailure::kTruncated);
  view.removedIds.reserve(removedCount);
  for (uint8_t i = 0; i < removedCount; ++i) {
    view.removedIds.push_back(reader.readU16());
  }
  if (reader.isTruncated) return decodedFail<SnapshotView>(DecodeFailure::kTruncated);
  const uint8_t eventCount = reader.readU8();
  if (reader.isTruncated) return decodedFail<SnapshotView>(DecodeFailure::kTruncated);
  view.events.reserve(eventCount);
  for (uint8_t i = 0; i < eventCount; ++i) {
    const auto entry = decodeEventEntry(reader);
    if (!entry.isOk) return decodedFail<SnapshotView>(entry.failure);
    if (tracker != nullptr && !tracker->isNew(entry.value.eventId)) {
      ++view.duplicateEventCount;
      continue;
    }
    view.events.push_back(entry.value);
  }
  if (reader.isTruncated) return decodedFail<SnapshotView>(DecodeFailure::kTruncated);
  if (reader.remaining() != 0u) return decodedFail<SnapshotView>(DecodeFailure::kBadLength);
  return decodedOk(view);
}

namespace {

// 握手包的公共前缀：通用包头（+ 可靠扩展头）。
ByteWriter handshakeWriter(const PacketHeader& header, const ReliableExt& ext, bool isReliable,
                           uint8_t* out, std::size_t capacity) noexcept {
  return isReliable ? reliableWriter(header, ext, out, capacity)
                    : payloadWriter(header, out, capacity);
}

bool checkPayload(DecodeResult<PacketInfo>& packet, PacketType expectedType,
                  std::size_t expectedPayloadBytes, DecodeFailure& failure) noexcept {
  if (!packet.isOk) {
    failure = packet.failure;
    return false;
  }
  if (packet.value.header.type != static_cast<uint8_t>(expectedType)) {
    failure = DecodeFailure::kBadType;
    return false;
  }
  if (packet.value.payloadBytes != expectedPayloadBytes) {
    failure = DecodeFailure::kBadLength;
    return false;
  }
  return true;
}

}  // namespace

EncodeResult encodeHello(const PacketHeader& header, const HelloPayload& payload, uint8_t* out,
                         std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kHello)) return encodeFail();
  const std::size_t total = kCommonHeaderBytes + kHelloPayloadBytes;
  if (total > capacity) return encodeFail();
  ByteWriter writer = handshakeWriter(header, ReliableExt{}, false, out, capacity);
  writer.writeU32(payload.nonce);
  writeToken(writer, payload.token);
  if (writer.isOverflow) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<HelloPayload> decodeHello(const uint8_t* bytes, std::size_t size) noexcept {
  auto packet = decodePacket(bytes, size);
  DecodeFailure failure = DecodeFailure::kOk;
  if (!checkPayload(packet, PacketType::kHello, kHelloPayloadBytes, failure)) {
    return decodedFail<HelloPayload>(failure);
  }
  ByteReader reader = payloadReader(packet.value, bytes, size);
  HelloPayload payload{};
  payload.nonce = reader.readU32();
  if (!readToken(reader, payload.token)) {
    return decodedFail<HelloPayload>(reader.isTruncated ? DecodeFailure::kTruncated
                                                       : DecodeFailure::kBadValue);
  }
  return decodedOk(payload);
}

EncodeResult encodeHelloAck(const PacketHeader& header, const ReliableExt& ext,
                            const HelloAckPayload& payload, uint8_t* out,
                            std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kHelloAck)) return encodeFail();
  const std::size_t total = kCommonHeaderBytes + kReliableExtBytes + kHelloAckPayloadBytes;
  if (total > capacity) return encodeFail();
  ByteWriter writer = handshakeWriter(header, ext, true, out, capacity);
  writer.writeU32(payload.serverTick);
  writer.writeU32(payload.salt);
  if (writer.isOverflow) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<HelloAckPayload> decodeHelloAck(const uint8_t* bytes, std::size_t size) noexcept {
  auto packet = decodePacket(bytes, size);
  DecodeFailure failure = DecodeFailure::kOk;
  if (!checkPayload(packet, PacketType::kHelloAck, kHelloAckPayloadBytes, failure)) {
    return decodedFail<HelloAckPayload>(failure);
  }
  ByteReader reader = payloadReader(packet.value, bytes, size);
  HelloAckPayload payload{};
  payload.serverTick = reader.readU32();
  payload.salt = reader.readU32();
  if (reader.isTruncated) return decodedFail<HelloAckPayload>(DecodeFailure::kTruncated);
  return decodedOk(payload);
}

EncodeResult encodeResume(const PacketHeader& header, const ReliableExt& ext,
                          const ResumePayload& payload, uint8_t* out,
                          std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kResume)) return encodeFail();
  const std::size_t total = kCommonHeaderBytes + kReliableExtBytes + kResumePayloadBytes;
  if (total > capacity) return encodeFail();
  ByteWriter writer = handshakeWriter(header, ext, true, out, capacity);
  writeToken(writer, payload.token);
  if (writer.isOverflow) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<ResumePayload> decodeResume(const uint8_t* bytes, std::size_t size) noexcept {
  auto packet = decodePacket(bytes, size);
  DecodeFailure failure = DecodeFailure::kOk;
  if (!checkPayload(packet, PacketType::kResume, kResumePayloadBytes, failure)) {
    return decodedFail<ResumePayload>(failure);
  }
  ByteReader reader = payloadReader(packet.value, bytes, size);
  ResumePayload payload{};
  if (!readToken(reader, payload.token)) {
    return decodedFail<ResumePayload>(reader.isTruncated ? DecodeFailure::kTruncated
                                                        : DecodeFailure::kBadValue);
  }
  return decodedOk(payload);
}

EncodeResult encodeKeepAlive(const PacketHeader& header, const ReliableExt& ext, uint8_t* out,
                             std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kKeepAlive)) return encodeFail();
  const std::size_t total = kCommonHeaderBytes + kReliableExtBytes;
  if (total > capacity) return encodeFail();
  ByteWriter writer = handshakeWriter(header, ext, true, out, capacity);
  if (writer.isOverflow) return encodeFail();
  return encodeOk(writer.size());
}

DecodeFailure decodeKeepAlive(const uint8_t* bytes, std::size_t size) noexcept {
  auto packet = decodePacket(bytes, size);
  DecodeFailure failure = DecodeFailure::kOk;
  if (!checkPayload(packet, PacketType::kKeepAlive, 0u, failure)) return failure;
  return DecodeFailure::kOk;  // 无载荷，没有可返回的值
}

EncodeResult encodeDisconnect(const PacketHeader& header, const ReliableExt& ext,
                              const DisconnectPayload& payload, uint8_t* out,
                              std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kDisconnect)) return encodeFail();
  const std::size_t total = kCommonHeaderBytes + kReliableExtBytes + kDisconnectPayloadBytes;
  if (total > capacity) return encodeFail();
  ByteWriter writer = handshakeWriter(header, ext, true, out, capacity);
  writer.writeU8(payload.reason);
  if (writer.isOverflow) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<DisconnectPayload> decodeDisconnect(const uint8_t* bytes, std::size_t size) noexcept {
  auto packet = decodePacket(bytes, size);
  DecodeFailure failure = DecodeFailure::kOk;
  if (!checkPayload(packet, PacketType::kDisconnect, kDisconnectPayloadBytes, failure)) {
    return decodedFail<DisconnectPayload>(failure);
  }
  ByteReader reader = payloadReader(packet.value, bytes, size);
  DisconnectPayload payload{};
  payload.reason = reader.readU8();
  if (reader.isTruncated) return decodedFail<DisconnectPayload>(DecodeFailure::kTruncated);
  return decodedOk(payload);
}

std::size_t matchStatePlayerBytes(const MatchStatePlayer& player) noexcept {
  return kMatchStatePlayerFixedBytes + player.name.size();
}

namespace {

bool isValidMatchStatePlayer(const MatchStatePlayer& player) noexcept {
  if (player.name.size() < kNameMinBytes || player.name.size() > kNameMaxBytes) return false;
  return player.weapon <= 2u;
}

}  // namespace

EncodeResult encodeMatchState(const PacketHeader& header, const ReliableExt& ext,
                              const MatchState& state, uint8_t* out,
                              std::size_t capacity) noexcept {
  if (out == nullptr || !isEncodeHeaderValid(header, PacketType::kMatchState)) return encodeFail();
  if (state.players.size() > kMatchStateMaxPlayers) return encodeFail();
  std::size_t payloadBytes = 5u;
  for (const MatchStatePlayer& player : state.players) {
    if (!isValidMatchStatePlayer(player)) return encodeFail();
    payloadBytes += matchStatePlayerBytes(player);
  }
  const std::size_t total = kCommonHeaderBytes + kReliableExtBytes + payloadBytes;
  if (total > capacity || total > kMatchStateMaxBytes) return encodeFail();
  ByteWriter writer = reliableWriter(header, ext, out, capacity);
  writer.writeU8(state.phase);
  writer.writeU8(state.wave);
  writer.writeU16(state.intermissionMs);
  writer.writeU8(static_cast<uint8_t>(state.players.size()));
  for (const MatchStatePlayer& player : state.players) {
    writer.writeU16(player.pid);
    writer.writeU8(static_cast<uint8_t>(player.name.size()));
    writer.writeBytes(player.name.data(), player.name.size());
    writer.writeU8(player.ready);
    writer.writeU8(player.weapon);
    writer.writeU8(player.hpRatio);
    writer.writeU16(player.kills);
    writer.writeU8(player.mag);
    writer.writeU16(player.reserve);
    writer.writeU8(player.reloadLeft10Ms);
    writer.writeU8(player.rage);
    writer.writeU8(player.rageLeft100Ms);
    writer.writeU8(player.downed);
    writer.writeU8(player.reviveRatio255);
  }
  if (writer.isOverflow || writer.size() != total) return encodeFail();
  return encodeOk(writer.size());
}

DecodeResult<MatchState> decodeMatchState(const uint8_t* bytes, std::size_t size) noexcept {
  const auto packet = decodePacket(bytes, size);
  if (!packet.isOk) return decodedFail<MatchState>(packet.failure);
  if (packet.value.header.type != static_cast<uint8_t>(PacketType::kMatchState)) {
    return decodedFail<MatchState>(DecodeFailure::kBadType);
  }
  if (packet.value.payloadBytes < 5u) return decodedFail<MatchState>(DecodeFailure::kTruncated);
  ByteReader reader = payloadReader(packet.value, bytes, size);
  MatchState state{};
  state.phase = reader.readU8();
  state.wave = reader.readU8();
  state.intermissionMs = reader.readU16();
  const uint8_t count = reader.readU8();
  if (reader.isTruncated) return decodedFail<MatchState>(DecodeFailure::kTruncated);
  if (count > kMatchStateMaxPlayers) return decodedFail<MatchState>(DecodeFailure::kBadValue);
  state.players.reserve(count);
  for (uint8_t i = 0; i < count; ++i) {
    MatchStatePlayer player{};
    player.pid = reader.readU16();
    const uint8_t nameBytes = reader.readU8();
    if (reader.isTruncated) return decodedFail<MatchState>(DecodeFailure::kTruncated);
    if (nameBytes < kNameMinBytes || nameBytes > kNameMaxBytes) {
      return decodedFail<MatchState>(DecodeFailure::kBadValue);
    }
    player.name.resize(nameBytes);
    if (!reader.readBytes(player.name.data(), nameBytes)) {
      return decodedFail<MatchState>(DecodeFailure::kTruncated);
    }
    player.ready = reader.readU8();
    player.weapon = reader.readU8();
    player.hpRatio = reader.readU8();
    player.kills = reader.readU16();
    player.mag = reader.readU8();
    player.reserve = reader.readU16();
    player.reloadLeft10Ms = reader.readU8();
    player.rage = reader.readU8();
    player.rageLeft100Ms = reader.readU8();
    player.downed = reader.readU8();
    player.reviveRatio255 = reader.readU8();
    if (reader.isTruncated) return decodedFail<MatchState>(DecodeFailure::kTruncated);
    if (player.weapon > 2u) return decodedFail<MatchState>(DecodeFailure::kBadValue);
    state.players.push_back(std::move(player));
  }
  if (reader.remaining() != 0u) return decodedFail<MatchState>(DecodeFailure::kBadLength);
  return decodedOk(state);
}

}  // namespace ac::net
