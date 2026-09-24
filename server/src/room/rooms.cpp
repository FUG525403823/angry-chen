#include "room/rooms.hpp"

#include <cstring>

#include "sim/entity_table.hpp"

namespace ac::room {
namespace {

char toUpperAscii(char value) noexcept {
  return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A') : value;
}

// §5.3：长度 4 且每个字符都在 31 字符表内（保留码 0000 不含字母表字符，天然不通过）。
bool isValidRoomCodeFormat(const char* code) noexcept {
  for (std::size_t i = 0u; i < kRoomCodeLength; ++i) {
    if (code[i] == '\0') return false;
    if (std::strchr(kRoomCodeAlphabet, code[i]) == nullptr) return false;
  }
  return code[kRoomCodeLength] == '\0';
}

}  // namespace

void initRoomRegistry(RoomRegistry& registry, uint32_t startMs) noexcept {
  for (int32_t i = 0; i < kMaxRooms; ++i) registry.rooms[i].reset();
  registry.count = 0;
  registry.worldSeed = startMs;  // §5.3：种子 = 服务器启动毫秒，逐房 +1 派生
  registry.codeRng = ac::createRng(startMs, ac::RngStream::kFx);
  registry.reclaimedCount = 0u;
  registry.createFailureCount = 0u;
}

void pickRoomCode(RoomRegistry& registry, char* out) noexcept {
  if (out == nullptr) return;
  out[0] = '\0';
  for (int32_t attempt = 0; attempt < kRoomCodeAttempts; ++attempt) {
    char candidate[kRoomCodeLength + 1u] = {};
    for (std::size_t i = 0u; i < kRoomCodeLength; ++i) {
      const double value = registry.codeRng.nextDouble();
      int32_t index = static_cast<int32_t>(value * static_cast<double>(kRoomCodeAlphabetSize));
      if (index < 0) index = 0;
      if (index >= static_cast<int32_t>(kRoomCodeAlphabetSize)) {
        index = static_cast<int32_t>(kRoomCodeAlphabetSize) - 1;
      }
      candidate[i] = kRoomCodeAlphabet[static_cast<std::size_t>(index)];
    }
    if (std::strcmp(candidate, kNewRoomCode) == 0) continue;  // 保留码绝不作为真实房间码
    if (findRoom(registry, candidate) != nullptr) continue;
    std::memcpy(out, candidate, sizeof(candidate));
    return;
  }
}

Room* findRoom(const RoomRegistry& registry, std::string_view code) noexcept {
  if (code.size() != kRoomCodeLength) return nullptr;
  char normalized[kRoomCodeLength + 1u] = {};
  for (std::size_t i = 0u; i < kRoomCodeLength; ++i) normalized[i] = toUpperAscii(code[i]);
  for (int32_t i = 0; i < registry.count; ++i) {
    const Room* const room = registry.rooms[i].get();
    if (room != nullptr && std::strcmp(room->code, normalized) == 0) {
      return registry.rooms[i].get();
    }
  }
  return nullptr;
}

Room* createRoom(RoomRegistry& registry, uint64_t nowMs) noexcept {
  if (registry.count >= kMaxRooms) {
    registry.createFailureCount += 1u;
    return nullptr;
  }
  char code[kRoomCodeLength + 1u] = {};
  pickRoomCode(registry, code);
  if (code[0] == '\0') {  // §5.3：32 次冲突仍失败 → 建房返回空（调用方回 room-full）
    registry.createFailureCount += 1u;
    return nullptr;
  }
  std::unique_ptr<Room> room = std::make_unique<Room>();
  room->seed = registry.worldSeed;
  registry.worldSeed += 1u;
  room->world = ac::sim::createWorld(room->seed);
  if (room->world == nullptr) {
    registry.createFailureCount += 1u;
    return nullptr;
  }
  // v1 createWorldForRoom：清掉占位玩家，让 pid 从 1 起分配。
  ac::sim::World& world = *room->world;
  for (std::size_t i = world.activeCount; i > 0u; --i) {
    const uint16_t id = world.activeIds[i - 1u];
    if (world.entities[id - 1u].kind == ac::sim::EntityKind::kPlayer) {
      ac::sim::despawnEntity(world, id);
    }
  }
  std::memcpy(room->code, code, sizeof(room->code));
  room->phase = MatchPhase::kLobby;
  room->director = ac::waves::createDirectorState();
  room->lastUpdateMs = nowMs;
  room->emptySinceMs = static_cast<int64_t>(nowMs);
  room->matchState.players.reserve(ac::net::kMatchStateMaxPlayers);
  Room* const raw = room.get();
  registry.rooms[registry.count] = std::move(room);
  registry.count += 1;
  return raw;
}

void destroyRoom(RoomRegistry& registry, Room& room) noexcept {
  int32_t index = -1;
  for (int32_t i = 0; i < registry.count; ++i) {
    if (registry.rooms[i].get() == &room) index = i;
  }
  if (index < 0) return;
  for (uint8_t i = 0u; i < room.sessionCount; ++i) {
    Session* const session = room.sessions[i];
    if (session != nullptr) clearSessionRoom(*session);
    room.sessions[i] = nullptr;
  }
  room.sessionCount = 0u;
  for (int32_t i = index; i + 1 < registry.count; ++i) {
    registry.rooms[i] = std::move(registry.rooms[i + 1]);
  }
  registry.count -= 1;
  registry.rooms[registry.count].reset();
}

JoinOutcome join(RoomRegistry& registry, std::string_view code, Session& session,
                 uint64_t nowMs) noexcept {
  if (code.size() != kRoomCodeLength) return JoinOutcome::kRoomNotFound;
  char normalized[kRoomCodeLength + 1u] = {};
  for (std::size_t i = 0u; i < kRoomCodeLength; ++i) normalized[i] = toUpperAscii(code[i]);
  // §5.3：先判保留码 0000（表示新建房间），再做格式校验。
  if (std::strcmp(normalized, kNewRoomCode) == 0) {
    Room* const created = createRoom(registry, nowMs);
    if (created == nullptr) return JoinOutcome::kRoomFull;
    return roomJoin(*created, session, nowMs);
  }
  if (!isValidRoomCodeFormat(normalized)) return JoinOutcome::kRoomNotFound;
  Room* const room = findRoom(registry, normalized);
  if (room == nullptr) return JoinOutcome::kRoomNotFound;
  return roomJoin(*room, session, nowMs);
}

Session* findGraceSession(RoomRegistry& registry, uint32_t token) noexcept {
  if (token == 0u) return nullptr;
  for (int32_t r = 0; r < registry.count; ++r) {
    Room& room = *registry.rooms[r];
    for (uint8_t i = 0u; i < room.sessionCount; ++i) {
      Session* const session = room.sessions[i];
      if (session == nullptr || session->token != token) continue;
      if (isConnected(*session)) continue;  // 只匹配宽限期里的会话
      return session;
    }
  }
  return nullptr;
}

bool reconnect(RoomRegistry& registry, uint32_t token, Session& incoming) noexcept {
  Session* const existing = findGraceSession(registry, token);
  if (existing == nullptr) return false;
  for (int32_t r = 0; r < registry.count; ++r) {
    Room& room = *registry.rooms[r];
    for (uint8_t i = 0u; i < room.sessionCount; ++i) {
      if (room.sessions[i] != existing) continue;
      return roomReconnect(room, *existing, incoming);
    }
  }
  return false;
}

bool leave(RoomRegistry& registry, Session& session, uint64_t nowMs) noexcept {
  for (int32_t r = 0; r < registry.count; ++r) {
    Room& room = *registry.rooms[r];
    for (uint8_t i = 0u; i < room.sessionCount; ++i) {
      if (room.sessions[i] != &session) continue;
      return roomLeave(room, session, nowMs);
    }
  }
  return false;
}

bool disconnect(RoomRegistry& registry, Session& session, uint64_t nowMs) noexcept {
  for (int32_t r = 0; r < registry.count; ++r) {
    Room& room = *registry.rooms[r];
    for (uint8_t i = 0u; i < room.sessionCount; ++i) {
      if (room.sessions[i] != &session) continue;
      return roomDisconnect(room, session, nowMs);
    }
  }
  return false;
}

int32_t reclaimIdle(RoomRegistry& registry, uint64_t nowMs) noexcept {
  int32_t reclaimed = 0;
  for (int32_t i = registry.count - 1; i >= 0; --i) {
    Room& room = *registry.rooms[i];
    if (!roomIsIdle(room) || room.emptySinceMs < 0) continue;
    if (nowMs < static_cast<uint64_t>(room.emptySinceMs)) continue;
    if (nowMs - static_cast<uint64_t>(room.emptySinceMs) <
        static_cast<uint64_t>(kEmptyRoomReclaimMs)) {
      continue;
    }
    destroyRoom(registry, room);
    registry.reclaimedCount += 1u;
    reclaimed += 1;
  }
  return reclaimed;
}

}  // namespace ac::room
