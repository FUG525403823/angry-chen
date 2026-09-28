#include "room/event_map.hpp"

#include <cmath>

#include "core/quantize.hpp"

namespace ac::room {
namespace {

// v1 codec.ts 的 `clamp(Math.round(x), lo, hi)`：非有限输入按 quantize.hpp 的同一条规则归 0。
double roundedOrZero(double value) noexcept { return std::isfinite(value) ? std::floor(value + 0.5) : 0.0; }

double clampRange(double value, double low, double high) noexcept {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

uint8_t roundU8(double value) noexcept {
  return static_cast<uint8_t>(clampRange(roundedOrZero(value), 0.0, 255.0));
}

uint16_t roundU16(double value) noexcept {
  return static_cast<uint16_t>(clampRange(roundedOrZero(value), 0.0, 65535.0));
}

uint32_t roundU32(double value) noexcept {
  return static_cast<uint32_t>(clampRange(roundedOrZero(value), 0.0, 4294967295.0));
}

}  // namespace

bool mapSimEvent(const ac::sim::Event& event, uint32_t eventId, ac::net::EventEntry& out) noexcept {
  switch (event.type) {
    case ac::sim::kEventPlayerHit: {
      ac::net::PlayerHitEvent payload{};
      payload.subjectId = event.subjectId;
      payload.targetId = event.targetId;
      payload.value = roundU16(event.value);
      payload.flags = event.flags;
      // v2 新增的权威命中点（S08 §5.5，S03 §5.4 第 3 列）：帧内米制 → 厘米 i16。
      payload.hitX = ac::quantizePosition(event.x);
      payload.hitY = ac::quantizePosition(event.y);
      payload.hitZ = ac::quantizePosition(event.z);
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventSheepKilled: {
      ac::net::SheepKilledEvent payload{};
      payload.targetId = event.targetId;
      payload.subjectId = event.subjectId;
      // v1 把伤害写在 kind 字节上（encodeEventPayload 用 `event.value`）；v2 由 S03 §5.4 把该字节
      // 定为「羊的类型」，S08 因此新增 `Event::kind`（world.hpp:34）—— 线上取 kind，取的不是伤害。
      payload.kind = event.kind;
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventWaveStart: {
      ac::net::WaveStartEvent payload{};
      payload.wave = roundU8(event.value);
      payload.budget = roundU16(event.flags);
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventWaveClear: {
      ac::net::WaveClearEvent payload{};
      payload.wave = roundU8(event.value);
      payload.elapsedMs = roundU32(event.flags);
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventPlayerDowned: {
      ac::net::PlayerDownedEvent payload{};
      payload.subjectId = event.subjectId;
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventReviveProgress: {
      ac::net::ReviveProgressEvent payload{};
      payload.targetId = event.targetId;
      payload.subjectId = event.subjectId;
      // 线上字段名是 ratio255，值按 v1 就是 sim 的千分比（0..1000）；v2 不改这条口径。
      payload.ratio255 = roundU16(event.value);
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventReviveDone: {
      ac::net::ReviveDoneEvent payload{};
      payload.targetId = event.targetId;
      payload.subjectId = event.subjectId;
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventRageActivated: {
      ac::net::RageActivatedEvent payload{};
      payload.subjectId = event.subjectId;
      payload.durationMs = roundU16(event.value);
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    case ac::sim::kEventMatchEnded: {
      ac::net::MatchEndedEvent payload{};
      payload.reason = roundU8(event.subjectId);  // 房间侧填 winnerTeam（0/1）
      payload.wave = roundU8(event.value);
      // 时长只能来自 sim 的 u8 flags：按 W1 的裁决（B 方案）恒 0，真实时长走 MatchRuntime/结算记录。
      payload.durationMs = roundU32(event.flags);
      out.eventId = eventId;
      out.data = payload;
      return true;
    }
    default:
      return false;  // kEventPhaseChange（10）与未知类型：两侧都没有生产者
  }
}

std::size_t projectRoomEvents(ac::sim::World& world, ac::net::EventEntry* out, std::size_t offset,
                              std::size_t capacity, uint32_t& nextEventId) noexcept {
  if (out == nullptr) return 0u;
  std::size_t written = 0u;
  std::size_t cursor = world.eventCursor;
  while (cursor < world.eventCount) {
    const ac::sim::Event& event = world.events[cursor];
    ac::net::EventEntry entry{};
    if (mapSimEvent(event, nextEventId, entry)) {
      if (offset + written >= capacity) break;  // 容量不足：本条与之后的事件留在缓冲里续投
      out[offset + written] = entry;
      ++written;
      ++nextEventId;
    }
    ++cursor;
  }
  world.eventCursor = static_cast<uint16_t>(cursor);
  return written;
}

}  // namespace ac::room
