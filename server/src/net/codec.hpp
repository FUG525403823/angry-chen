#pragma once
// S03 §5.2–§5.7：命令 / 快照 / 事件 / 握手 / MatchState 的编解码声明。
//
// 约定（§5.6）：解码一律返回值、绝不抛异常、绝不越界；编码失败返回 isOk=false（bytes=0），
// 调用方计数并丢弃坏消息（工程约定 §7），不得让进程退出。
// 线上字节是唯一真相：内存结构只做字段级读写，不做 memcpy（记录线上 15 字节，内存布局可不同）。

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "net/wire.hpp"

namespace ac::net {

enum class DecodeFailure : uint8_t {
  kOk = 0,
  kTruncated = 1,
  kBadVersion = 2,
  kBadType = 3,
  kBadLength = 4,
  kBadValue = 5,
  kBadSession = 6,
  kUnknownEvent = 7,
  // §5.4 要求重复 eventId「静默丢弃」，所以解码器不返回它，只累加后文的 duplicateCount；
  // 该值留给选择拒收的调用方（S04 通道层）。
  kDuplicateEventId = 8,
};

template <typename T>
struct DecodeResult {
  bool isOk;
  DecodeFailure failure;
  T value;
};

template <typename T>
inline DecodeResult<T> decodedOk(T value) noexcept {
  return DecodeResult<T>{true, DecodeFailure::kOk, static_cast<T&&>(value)};
}

template <typename T>
inline DecodeResult<T> decodedFail(DecodeFailure failure) noexcept {
  return DecodeResult<T>{false, failure, T{}};
}

struct EncodeResult {
  bool isOk;
  std::size_t bytes;
};

inline EncodeResult encodeOk(std::size_t bytes) noexcept { return EncodeResult{true, bytes}; }
inline EncodeResult encodeFail() noexcept { return EncodeResult{false, 0u}; }

// ---- 通用包头解析（只解头，载荷交给具体解码器）----
struct PacketInfo {
  PacketHeader header;
  ReliableExt reliableExt;
  FragmentHeader fragment;
  bool hasReliableExt;
  bool hasFragmentHeader;
  std::size_t payloadOffset;
  std::size_t payloadBytes;
};

DecodeResult<PacketInfo> decodePacket(const uint8_t* bytes, std::size_t size) noexcept;

// 载荷起点：类型/长度/版本都合法时返回 bytes + payloadOffset，否则返回 nullptr。
const uint8_t* packetPayload(const PacketInfo& info, const uint8_t* bytes, std::size_t size) noexcept;

// ---- §5.2 命令（type 4，reliable，14 字节载荷）----
inline constexpr std::size_t kCommandPayloadBytes = 14u;
inline constexpr std::size_t kCommandPacketBytes =
    kCommonHeaderBytes + kReliableExtBytes + kCommandPayloadBytes;  // 34

struct CommandPayload {
  int8_t moveX;
  int8_t moveY;
  uint16_t yaw;
  uint16_t pitch;
  uint8_t buttons;
  uint8_t switchTo;
  uint16_t seq;
  uint32_t clientTick;
};

EncodeResult encodeCommand(const PacketHeader& header, const ReliableExt& ext,
                           const CommandPayload& command, uint8_t* out,
                           std::size_t capacity) noexcept;
DecodeResult<CommandPayload> decodeCommand(const uint8_t* bytes, std::size_t size) noexcept;

// ---- §5.3 快照（type 5，不可靠）----
inline constexpr std::size_t kEntityRecordBytes = 15u;
inline constexpr std::size_t kSnapshotHeadBytes = kCommonHeaderBytes + 4u + 4u + 2u + 4u;  // 22
inline constexpr uint16_t kEntityIdMax = 1024u;
inline constexpr std::size_t kMinSnapshotBytes = kSnapshotHeadBytes + 3u;  // 22 + 空实体块 + 空移除 + 空事件

// 字段顺序按「避免内存填充」排列（u16 在前、u8 在后），线上顺序见 §5.3，两者由字段级读写连接。
struct EntityRecord {
  uint16_t id;
  int16_t xCm;
  int16_t yCm;
  int16_t zCm;
  uint16_t yawUnits;
  uint16_t pitchUnits;
  uint8_t hpRatioUnits;
  uint8_t kindFlags;
  uint8_t state;

  bool operator==(const EntityRecord& other) const noexcept = default;

  uint8_t kind() const noexcept { return static_cast<uint8_t>(kindFlags & 0x03u); }
  uint8_t flags() const noexcept { return static_cast<uint8_t>((kindFlags >> 2) & 0x3Fu); }
};

// 注意：内存结构允许有尾部填充（sizeof 可能是 16），线上 15 字节由字段级读写保证，
// 差分比较走 operator== 的逐字段比较，因此不做 memcpy、也不断言 sizeof。

// 差分基线：按 id 升序、id 唯一的记录表（发送侧保存「客户端已有」的镜像）。
struct SnapshotBaseline {
  uint32_t tick;
  std::vector<EntityRecord> records;

  const EntityRecord* find(uint16_t id) const noexcept;
};

// ---- §5.4 事件（type 6 EventChannel / 快照事件块共用）----
enum class EventType : uint8_t {
  kPlayerHit = 1,
  kSheepKilled = 2,
  kWaveStart = 3,
  kWaveClear = 4,
  kPlayerDowned = 5,
  kReviveProgress = 6,
  kReviveDone = 7,
  kRageActivated = 8,
  kMatchEnded = 9,
  kPhaseChange = 10,
};

inline constexpr uint8_t kMinEventType = 1u;
inline constexpr uint8_t kMaxEventType = 10u;
inline constexpr std::size_t kEventTypeCount = 10u;
inline constexpr std::size_t kEventIdHeaderBytes = 4u;

// 各类型载荷（不含 type 字节），字段次序与 §5.4 表一致。
struct PlayerHitEvent {
  uint16_t subjectId;
  uint16_t targetId;
  uint16_t value;
  uint8_t flags;  // headshot=1 / downed=2 / killed=4
  int16_t hitX;
  int16_t hitY;
  int16_t hitZ;

  bool operator==(const PlayerHitEvent& other) const noexcept = default;
};
struct SheepKilledEvent {
  uint16_t targetId;
  uint16_t subjectId;
  uint8_t kind;  // 0 grunt / 1 ram / 2 elite / 3 king

  bool operator==(const SheepKilledEvent& other) const noexcept = default;
};
struct WaveStartEvent {
  uint8_t wave;
  uint16_t budget;

  bool operator==(const WaveStartEvent& other) const noexcept = default;
};
struct WaveClearEvent {
  uint8_t wave;
  uint32_t elapsedMs;

  bool operator==(const WaveClearEvent& other) const noexcept = default;
};
struct PlayerDownedEvent {
  uint16_t subjectId;

  bool operator==(const PlayerDownedEvent& other) const noexcept = default;
};
struct ReviveProgressEvent {
  uint16_t targetId;
  uint16_t subjectId;
  uint16_t ratio255;

  bool operator==(const ReviveProgressEvent& other) const noexcept = default;
};
struct ReviveDoneEvent {
  uint16_t targetId;
  uint16_t subjectId;

  bool operator==(const ReviveDoneEvent& other) const noexcept = default;
};
struct RageActivatedEvent {
  uint16_t subjectId;
  uint16_t durationMs;

  bool operator==(const RageActivatedEvent& other) const noexcept = default;
};
struct MatchEndedEvent {
  uint8_t reason;
  uint8_t wave;
  uint32_t durationMs;

  bool operator==(const MatchEndedEvent& other) const noexcept = default;
};
struct PhaseChangeEvent {
  uint8_t phase;
  uint8_t wave;
  uint16_t intermissionMs;

  bool operator==(const PhaseChangeEvent& other) const noexcept = default;
};

// 变体下标 = type - 1（类型 1..10 与声明次序一一对应）。
using EventData =
    std::variant<PlayerHitEvent, SheepKilledEvent, WaveStartEvent, WaveClearEvent, PlayerDownedEvent,
                 ReviveProgressEvent, ReviveDoneEvent, RageActivatedEvent, MatchEndedEvent,
                 PhaseChangeEvent>;

struct EventEntry {
  uint32_t eventId;
  EventData data;

  bool operator==(const EventEntry& other) const noexcept = default;
};

// type + 载荷 的字节数（§5.4 第 4 列）：14/6/4/6/3/7/5/5/7/5；未知类型返回 0。
std::size_t eventPayloadWithTypeBytes(uint8_t type) noexcept;
// 条目总长 = 4 + 上一个（§5.4 第 5 列）：18/10/8/10/7/11/9/9/11/9；未知类型返回 0。
std::size_t eventEntryBytes(uint8_t type) noexcept;
uint8_t eventTypeOf(const EventEntry& entry) noexcept;

// eventId 单调去重（幂等键）：isNew 接受并推进水位，重复/倒退返回 false（静默丢弃）。
struct EventIdTracker {
  uint32_t maxEventId;
  bool isNew(uint32_t eventId) noexcept;
};

struct EventFrame {
  uint32_t tick;
  std::vector<EventEntry> events;
  std::size_t duplicateCount;
};

EncodeResult encodeEventFrame(const PacketHeader& header, const ReliableExt& ext, uint32_t tick,
                              const EventEntry* events, std::size_t eventCount, uint8_t* out,
                              std::size_t capacity) noexcept;
// tracker 非空时按幂等键丢弃重复 eventId（§5.4），并累加 duplicateCount。
DecodeResult<EventFrame> decodeEventFrame(const uint8_t* bytes, std::size_t size,
                                          EventIdTracker* tracker = nullptr) noexcept;

// ---- 快照视图与差分编码 ----
struct SnapshotView {
  uint32_t tick;
  uint32_t serverTimeMs;
  uint16_t lastAckedSeq;
  uint32_t baselineTick;
  std::vector<EntityRecord> records;
  std::vector<uint16_t> removedIds;
  std::vector<EventEntry> events;
  std::size_t duplicateEventCount;
};

// 编码输入：baseline 为 nullptr 或 baseline->tick == 0 时发全量；records 必须按 id 升序且唯一。
struct SnapshotFrame {
  uint32_t tick;
  uint32_t serverTimeMs;
  uint16_t lastAckedSeq;
  const SnapshotBaseline* baseline;
  const EntityRecord* records;
  std::size_t recordCount;
  const EventEntry* events;
  std::size_t eventCount;
};

// 差分判据（§5.3）：基线中不存在该 id，或该 id 的 15 字节记录与基线不同 → 进入实体块；
// 移除列表只列「基线存在且本帧不再存在」的 id（升序）。
EncodeResult encodeSnapshot(const PacketHeader& header, const SnapshotFrame& frame, uint8_t* out,
                            std::size_t capacity) noexcept;
DecodeResult<SnapshotView> decodeSnapshot(const uint8_t* bytes, std::size_t size,
                                          EventIdTracker* tracker = nullptr) noexcept;

// ---- 握手载荷（ADR-009 握手时序）----
// reconnectToken 的**值**是 u32（S04 §5.5：salt ^ clientNonce），但线上是 8 位小写十六进制 ASCII
// （ADR-009「握手时序」、C03 §5.5 逐字相同），所以 Hello 载荷 4+8、Resume 载荷 8 字节。
inline constexpr std::size_t kTokenBytes = 8u;              // 8 位小写十六进制 ASCII
inline constexpr std::size_t kHelloPayloadBytes = 12u;      // clientNonce u32 + reconnectToken(ASCII)
inline constexpr std::size_t kHelloAckPayloadBytes = 8u;    // serverTick u32 + salt u32
inline constexpr std::size_t kResumePayloadBytes = 8u;      // reconnectToken(ASCII)
inline constexpr std::size_t kDisconnectPayloadBytes = 1u;  // reason u8

struct HelloPayload {
  uint32_t nonce;
  uint32_t token;
};
struct HelloAckPayload {
  uint32_t serverTick;
  uint32_t salt;
};
struct ResumePayload {
  uint32_t token;
};
struct DisconnectPayload {
  uint8_t reason;
};

EncodeResult encodeHello(const PacketHeader& header, const HelloPayload& payload, uint8_t* out,
                         std::size_t capacity) noexcept;
DecodeResult<HelloPayload> decodeHello(const uint8_t* bytes, std::size_t size) noexcept;
EncodeResult encodeHelloAck(const PacketHeader& header, const ReliableExt& ext,
                            const HelloAckPayload& payload, uint8_t* out,
                            std::size_t capacity) noexcept;
DecodeResult<HelloAckPayload> decodeHelloAck(const uint8_t* bytes, std::size_t size) noexcept;
EncodeResult encodeResume(const PacketHeader& header, const ReliableExt& ext,
                          const ResumePayload& payload, uint8_t* out,
                          std::size_t capacity) noexcept;
DecodeResult<ResumePayload> decodeResume(const uint8_t* bytes, std::size_t size) noexcept;
// KeepAlive 无载荷：写端只写 8 + 12 字节头（§5.1）。
EncodeResult encodeKeepAlive(const PacketHeader& header, const ReliableExt& ext, uint8_t* out,
                             std::size_t capacity) noexcept;
DecodeFailure decodeKeepAlive(const uint8_t* bytes, std::size_t size) noexcept;
EncodeResult encodeDisconnect(const PacketHeader& header, const ReliableExt& ext,
                              const DisconnectPayload& payload, uint8_t* out,
                              std::size_t capacity) noexcept;
DecodeResult<DisconnectPayload> decodeDisconnect(const uint8_t* bytes, std::size_t size) noexcept;

// ---- §5.7 MatchState（type 10，reliable，单播）----
inline constexpr std::size_t kMatchStateMaxPlayers = 4u;
inline constexpr std::size_t kNameMinBytes = 1u;
inline constexpr std::size_t kNameMaxBytes = 12u;
inline constexpr std::size_t kMatchStatePlayerFixedBytes = 16u;  // 3 + nameLen + 13，不含名称
inline constexpr std::size_t kMatchStateMaxBytes = 117u;         // 5 + 4 * (16 + 12)

struct MatchStatePlayer {
  uint16_t pid;
  std::string name;  // UTF-8，1..12 字节
  uint8_t ready;
  uint8_t weapon;  // 0/1/2
  uint8_t hpRatio;
  uint16_t kills;
  uint8_t mag;
  uint16_t reserve;
  uint8_t reloadLeft10Ms;
  uint8_t rage;
  uint8_t rageLeft100Ms;
  uint8_t downed;
  uint8_t reviveRatio255;

  bool operator==(const MatchStatePlayer& other) const noexcept = default;
};

struct MatchState {
  uint8_t phase;
  uint8_t wave;
  uint16_t intermissionMs;
  std::vector<MatchStatePlayer> players;

  bool operator==(const MatchState& other) const noexcept = default;
};

EncodeResult encodeMatchState(const PacketHeader& header, const ReliableExt& ext,
                              const MatchState& state, uint8_t* out,
                              std::size_t capacity) noexcept;
DecodeResult<MatchState> decodeMatchState(const uint8_t* bytes, std::size_t size) noexcept;

// 单个玩家记录的线上字节数（不含名称：16 + nameLen）。
std::size_t matchStatePlayerBytes(const MatchStatePlayer& player) noexcept;

}  // namespace ac::net
