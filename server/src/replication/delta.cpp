#include "replication/delta.hpp"

#include "core/quantize.hpp"

namespace ac::replication {
namespace {

bool isPresentInWorld(const sim::World& world, uint16_t id) noexcept {
  const sim::Entity* entity = sim::entityById(world, id);
  return entity != nullptr && entity->active;
}

}  // namespace

uint8_t kindFlagsOf(const sim::Entity& entity) noexcept {
  const bool isPlayer = entity.kind == sim::EntityKind::kPlayer;
  uint8_t flags = 0u;
  if (isPlayer && entity.hp <= 0.0) flags |= kSnapshotFlagDowned;
  if (isPlayer && entity.idle) flags |= kSnapshotFlagIdle;
  const uint8_t kind = static_cast<uint8_t>(entity.kind);  // §5.2：kind 线上编号 0/1/2/3
  return static_cast<uint8_t>(kind | static_cast<uint8_t>((flags & 0x3Fu) << 2));
}

std::size_t projectWorld(const sim::World& world, net::EntityRecord* out,
                         std::size_t capacity) noexcept {
  std::size_t count = 0u;
  for (std::size_t i = 0u; i < world.activeCount && count < capacity; ++i) {
    const sim::Entity& entity = world.entities[world.activeIds[i] - 1u];
    if (!entity.active) continue;
    net::EntityRecord& record = out[count];
    record.id = entity.id;
    record.xCm = ac::quantizePosition(entity.pos.x);
    record.yCm = ac::quantizePosition(entity.pos.y);
    record.zCm = ac::quantizePosition(entity.pos.z);
    record.yawUnits = ac::quantizeAngle(entity.yaw);
    record.pitchUnits = ac::quantizeAngle(entity.pitch);
    record.hpRatioUnits = ac::quantizeRatio(entity.maxHp > 0.0 ? entity.hp / entity.maxHp : 0.0);
    record.kindFlags = kindFlagsOf(entity);
    record.state = entity.state;
    ++count;
  }
  return count;
}

std::size_t collectRemovedIds(const ClientBaseline& baseline, const sim::World& world,
                              uint16_t* out, std::size_t capacity) noexcept {
  std::size_t count = 0u;
  for (const net::EntityRecord& previous : baseline.mirror.records) {
    if (count >= capacity) break;
    if (!isPresentInWorld(world, previous.id)) out[count++] = previous.id;
  }
  return count;
}

DeltaOutcome encodeDelta(const DeltaInput& input, ClientBaseline& baseline, uint8_t* out,
                         std::size_t capacity) noexcept {
  DeltaOutcome result{};
  if (input.world == nullptr || out == nullptr || capacity == 0u) return result;
  const sim::World& world = *input.world;

  net::EntityRecord records[kMaxRecordsPerFrame] = {};
  const std::size_t recordCount = projectWorld(world, records, kMaxRecordsPerFrame);
  const std::size_t truncatedCount =
      static_cast<std::size_t>(world.activeCount) > recordCount
          ? static_cast<std::size_t>(world.activeCount) - recordCount
          : 0u;
  const bool isFull = input.isForceFull || baseline.mirror.tick == 0u;
  uint16_t removed[kBaselineCapacity] = {};
  const std::size_t removedCount =
      isFull ? 0u : collectRemovedIds(baseline, world, removed, kBaselineCapacity);

  net::PacketHeader header{};
  header.version = net::kProtocolVersion;
  header.type = static_cast<uint8_t>(net::PacketType::kSnapshot);
  header.flags = 0u;  // 快照走不可靠通道：无可靠扩展头
  header.session = input.session;
  header.seq = input.seq;

  net::SnapshotFrame frame{};
  frame.tick = world.tick;
  frame.serverTimeMs = world.timeMs;
  frame.lastAckedSeq = input.lastAckedSeq;
  frame.baseline = isFull ? nullptr : &baseline.mirror;
  frame.records = records;
  frame.recordCount = recordCount;
  frame.events = input.events;
  frame.eventCount = input.eventCount;

  const net::EncodeResult encoded = net::encodeSnapshot(header, frame, out, capacity);
  if (!encoded.isOk) return result;

  advanceBaseline(baseline, world.tick, records, recordCount, removed, removedCount, isFull);
  if (isFull) {
    baseline.lastFullTick = world.tick;
    baseline.framesSinceFull = 0u;
  }

  result.isOk = true;
  result.isFull = isFull;
  result.bytes = encoded.bytes;
  result.recordCount = recordCount;
  result.removedCount = removedCount;
  result.eventCount = input.eventCount;
  result.truncatedCount = truncatedCount;
  result.tick = world.tick;
  return result;
}

}  // namespace ac::replication
