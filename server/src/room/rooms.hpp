#pragma once
// S10 §5.3：房间注册表（建房 / 加入 / 离开 / 断线 / 按令牌重连 / 空闲回收）。
#include <cstdint>
#include <memory>
#include <string_view>

#include "core/rng.hpp"
#include "room/room.hpp"

namespace ac::room {

struct RoomRegistry {
  std::unique_ptr<Room> rooms[kMaxRooms] = {};
  int32_t count = 0;
  // §5.3：房间码用注册表自带的非模拟实例（种子 = 服务器启动毫秒），不触碰 world 的三条流。
  ac::Rng codeRng{};
  uint32_t worldSeed = 0u;
  uint32_t reclaimedCount = 0u;
  uint32_t createFailureCount = 0u;  // 32 次冲突重试仍失败的次数（调用方回 room-full）
};

// §5.3：种子 = 启动毫秒；worldSeed 用于给每间房派生世界种子。
void initRoomRegistry(RoomRegistry& registry, uint32_t startMs) noexcept;

// §5.3：逐字符 floor(rng * 31) 并夹取；冲突或等于保留码则重试，最多 32 次。
void pickRoomCode(RoomRegistry& registry, char* out) noexcept;

Room* findRoom(const RoomRegistry& registry, std::string_view code) noexcept;

// §5.4 的加入：先判保留码 0000，再判格式（长度 4 与字母表），都通过才查表。
JoinOutcome join(RoomRegistry& registry, std::string_view code, Session& session,
                 uint64_t nowMs) noexcept;

// §5.5：按令牌在全部房间里找宽限期会话；找到则接管同一 pid。
bool reconnect(RoomRegistry& registry, uint32_t token, Session& incoming) noexcept;

Session* findGraceSession(RoomRegistry& registry, uint32_t token) noexcept;

bool leave(RoomRegistry& registry, Session& session, uint64_t nowMs) noexcept;
bool disconnect(RoomRegistry& registry, Session& session, uint64_t nowMs) noexcept;

// §5.5：连续空闲 60000ms 未被认领 → 清空会话并从注册表删除；返回回收数。
int32_t reclaimIdle(RoomRegistry& registry, uint64_t nowMs) noexcept;

}  // namespace ac::room
