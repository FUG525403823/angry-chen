#pragma once
// S03 §5.1：小端定长读写与三个包头。全部 inline，热点路径不额外分配。
//
// 读取纪律（§5.6）：任何读取先查剩余长度，越界只把 isTruncated 置位并返回 0，绝不越界解引用；
// 调用方看到 isTruncated 后必须停止解析，不得"补 0 继续"。
// 写入纪律：容量不足只把 isOverflow 置位并停止推进，不写越界。

#include <cstddef>
#include <cstdint>

namespace ac::net {

inline constexpr uint8_t kProtocolVersion = 1u;

inline constexpr std::size_t kCommonHeaderBytes = 8u;
inline constexpr std::size_t kReliableExtBytes = 12u;
inline constexpr std::size_t kFragmentHeaderBytes = 4u;

inline constexpr uint16_t kFlagReliable = 1u;
inline constexpr uint16_t kFlagMoreFragments = 2u;
inline constexpr uint16_t kFlagAckOnly = 4u;

// ADR-009 报文上限与预算
inline constexpr std::size_t kMaxPacketBytes = 1200u;          // 单包载荷上限（含头），超过必须分片
inline constexpr std::size_t kMaxSnapshotBytes = 2048u;        // 单快照上限
inline constexpr std::size_t kSteadySnapshotBytes = 1228u;     // 稳态快照上限
inline constexpr std::size_t kMaxEntityRecordsPerFrame = 128u; // 单帧实体记录（count 为 u8，硬上限 255）
inline constexpr std::size_t kMaxRemovedPerFrame = 255u;
inline constexpr std::size_t kMaxEventsPerFrame = 64u;         // 超出走 EventChannel
inline constexpr std::size_t kMaxFragments = 8u;
inline constexpr std::size_t kMaxLogicalMessageBytes = 9408u;  // 8 分片

// 类型码（§5.1）：1 Hello / 2 HelloAck / 3 Resume / 4 Command / 5 Snapshot / 6 Event /
// 7 KeepAlive / 8 Disconnect / 9 Fragment / 10 MatchState
enum class PacketType : uint8_t {
  kHello = 1,
  kHelloAck = 2,
  kResume = 3,
  kCommand = 4,
  kSnapshot = 5,
  kEvent = 6,
  kKeepAlive = 7,
  kDisconnect = 8,
  kFragment = 9,
  kMatchState = 10,
};

inline constexpr uint8_t kMinPacketType = 1u;
inline constexpr uint8_t kMaxPacketType = 10u;

struct PacketHeader {
  uint8_t version;
  uint8_t type;  // PacketType 的线上值
  uint16_t flags;
  uint16_t session;
  uint16_t seq;

  bool isReliable() const noexcept { return (flags & kFlagReliable) != 0u; }
  bool hasMoreFragments() const noexcept { return (flags & kFlagMoreFragments) != 0u; }
  bool isAckOnly() const noexcept { return (flags & kFlagAckOnly) != 0u; }
};

struct ReliableExt {
  uint32_t msgId;
  uint32_t ackBase;
  uint32_t ackBits;
};

struct FragmentHeader {
  uint16_t fragId;
  uint8_t fragIndex;
  uint8_t fragCount;
};

// §5.1：可靠扩展头恒定在前、分片头在后（两个 flag 同时置位时不得交换顺序）。
inline std::size_t payloadOffset(const PacketHeader& header) noexcept {
  return kCommonHeaderBytes + (header.isReliable() ? kReliableExtBytes : 0u) +
         (header.hasMoreFragments() ? kFragmentHeaderBytes : 0u);
}

struct ByteWriter {
  uint8_t* data;
  std::size_t capacity;
  std::size_t offset;
  bool isOverflow;

  ByteWriter(uint8_t* out, std::size_t cap) noexcept
      : data(out), capacity(cap), offset(0), isOverflow(false) {}

  std::size_t size() const noexcept { return offset; }

  void writeU8(uint8_t value) noexcept {
    if (isOverflow || offset + 1u > capacity) {
      isOverflow = true;
      return;
    }
    data[offset++] = value;
  }

  void writeI8(int8_t value) noexcept { writeU8(static_cast<uint8_t>(value)); }

  void writeU16(uint16_t value) noexcept {
    writeU8(static_cast<uint8_t>(value & 0xFFu));
    writeU8(static_cast<uint8_t>((value >> 8) & 0xFFu));
  }

  void writeI16(int16_t value) noexcept { writeU16(static_cast<uint16_t>(value)); }

  void writeU32(uint32_t value) noexcept {
    writeU16(static_cast<uint16_t>(value & 0xFFFFu));
    writeU16(static_cast<uint16_t>((value >> 16) & 0xFFFFu));
  }

  void writeI32(int32_t value) noexcept { writeU32(static_cast<uint32_t>(value)); }

  void writeBytes(const void* source, std::size_t count) noexcept {
    if (isOverflow || offset + count > capacity) {
      isOverflow = true;
      return;
    }
    const auto* bytes = static_cast<const uint8_t*>(source);
    for (std::size_t i = 0; i < count; ++i) data[offset + i] = bytes[i];
    offset += count;
  }
};

struct ByteReader {
  const uint8_t* data;
  std::size_t size;
  std::size_t offset;
  bool isTruncated;

  ByteReader(const uint8_t* bytes, std::size_t length) noexcept
      : data(bytes), size(length), offset(0), isTruncated(false) {}

  std::size_t remaining() const noexcept { return offset <= size ? size - offset : 0u; }
  bool hasBytes(std::size_t count) const noexcept { return remaining() >= count; }
  const uint8_t* cursor() const noexcept { return data + offset; }

  bool readBytes(void* destination, std::size_t count) noexcept {
    if (isTruncated || remaining() < count) {
      isTruncated = true;
      return false;
    }
    auto* out = static_cast<uint8_t*>(destination);
    for (std::size_t i = 0; i < count; ++i) out[i] = data[offset + i];
    offset += count;
    return true;
  }

  void skip(std::size_t count) noexcept {
    if (isTruncated || remaining() < count) {
      isTruncated = true;
      return;
    }
    offset += count;
  }

  uint8_t readU8() noexcept {
    if (isTruncated || remaining() < 1u) {
      isTruncated = true;
      return 0u;
    }
    return data[offset++];
  }

  int8_t readI8() noexcept { return static_cast<int8_t>(readU8()); }

  uint16_t readU16() noexcept {
    const uint16_t low = readU8();
    const uint16_t high = readU8();
    return static_cast<uint16_t>(low | (high << 8));
  }

  int16_t readI16() noexcept { return static_cast<int16_t>(readU16()); }

  uint32_t readU32() noexcept {
    const uint32_t low = readU16();
    const uint32_t high = readU16();
    return low | (high << 16);
  }

  int32_t readI32() noexcept { return static_cast<int32_t>(readU32()); }
};

inline void writeHeader(ByteWriter& writer, const PacketHeader& header) noexcept {
  writer.writeU8(header.version);
  writer.writeU8(header.type);
  writer.writeU16(header.flags);
  writer.writeU16(header.session);
  writer.writeU16(header.seq);
}

// 只读字节，不做版本/类型校验（校验由 codec 的 decode* 负责）。
inline PacketHeader readHeader(ByteReader& reader) noexcept {
  PacketHeader header{};
  header.version = reader.readU8();
  header.type = reader.readU8();
  header.flags = reader.readU16();
  header.session = reader.readU16();
  header.seq = reader.readU16();
  return header;
}

inline void writeReliableExt(ByteWriter& writer, const ReliableExt& ext) noexcept {
  writer.writeU32(ext.msgId);
  writer.writeU32(ext.ackBase);
  writer.writeU32(ext.ackBits);
}

inline ReliableExt readReliableExt(ByteReader& reader) noexcept {
  ReliableExt ext{};
  ext.msgId = reader.readU32();
  ext.ackBase = reader.readU32();
  ext.ackBits = reader.readU32();
  return ext;
}

inline void writeFragmentHeader(ByteWriter& writer, const FragmentHeader& fragment) noexcept {
  writer.writeU16(fragment.fragId);
  writer.writeU8(fragment.fragIndex);
  writer.writeU8(fragment.fragCount);
}

inline FragmentHeader readFragmentHeader(ByteReader& reader) noexcept {
  FragmentHeader fragment{};
  fragment.fragId = reader.readU16();
  fragment.fragIndex = reader.readU8();
  fragment.fragCount = reader.readU8();
  return fragment;
}

// §5.1 的通道与类型映射：编码器只接受与类型一致的 flags。
inline uint16_t requiredFlags(PacketType type) noexcept {
  switch (type) {
    case PacketType::kHello:
      return 0u;
    case PacketType::kSnapshot:
      return 0u;
    case PacketType::kKeepAlive:
      return static_cast<uint16_t>(kFlagReliable | kFlagAckOnly);
    default:
      return kFlagReliable;
  }
}

}  // namespace ac::net
