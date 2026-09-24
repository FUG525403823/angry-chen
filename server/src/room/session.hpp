#pragma once
// S10 §5.4：房间内的一条连接身份。头文件里只有 POD 字段（房间热路径按值访问，不分配）。
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "net/codec.hpp"
#include "sim/movement.hpp"

namespace ac::room {

struct Session {
  uint32_t id = 0u;              // 连接短 ID（传输层分配）
  uint16_t pid = 0u;             // 人员编号 = 玩家实体的 EntityId（u16）；0 = 未分配
  char name[ac::net::kNameMaxBytes + 1u] = {};
  uint8_t nameBytes = 0u;
  uint32_t token = 0u;           // 重连令牌；0 = 尚未分配
  bool ready = false;
  uint8_t weapon = 0u;           // 大厅里选中的槽位（0/1/2）
  bool weaponApplied = false;    // 已落到实体上
  uint32_t kills = 0u;
  char roomCode[5] = {};         // 4 + NUL；空串 = 不在任何房间
  uint64_t joinedAtMs = 0u;
  int64_t disconnectedAtMs = -1;  // -1 = 连接中（v1 的 null）
  ac::sim::Command command{};     // 最近一条已净化命令
};

Session createSession(uint32_t id, uint64_t nowMs) noexcept;

bool isConnected(const Session& session) noexcept;
bool isInRoom(const Session& session) noexcept;

// 昵称净化：失败返回 false 且 name 保持原值（调用方按「超长/非法昵称」拒绝）。
bool setSessionName(Session& session, std::string_view raw) noexcept;

// 把会话从房间里摘干净（pid / 房间码 / 准备 / 断线时间戳）。
void clearSessionRoom(Session& session) noexcept;

}  // namespace ac::room
