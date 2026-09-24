#pragma once
// S10 §5.3/§5.4/§5.5/§5.7/§5.8：单个房间（成员、宽限、每 tick 推进、MatchState 单播）。
#include <cstddef>
#include <cstdint>
#include <memory>

#include "net/codec.hpp"
#include "room/match_controller.hpp"
#include "room/session.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"
#include "waves/director.hpp"

namespace ac::room {

struct RoomRegistry;

inline constexpr int32_t kMaxRooms = 64;
inline constexpr std::size_t kRoomCodeLength = 4u;
inline constexpr char kRoomCodeAlphabet[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
inline constexpr std::size_t kRoomCodeAlphabetSize = 31u;
inline constexpr char kNewRoomCode[] = "0000";  // 保留码：表示「新建房间」，绝不是真实房间码
inline constexpr int32_t kRoomCodeAttempts = 32;
inline constexpr std::size_t kMatchStateFrameMaxBytes =
    ac::net::kCommonHeaderBytes + ac::net::kReliableExtBytes + ac::net::kMatchStateMaxBytes;

// §5.4 的加入四态；kOk 之外都是合法的返回值，不是异常。
enum class JoinOutcome : uint8_t {
  kOk = 0,
  kRoomNotFound = 1,
  kRoomFull = 2,
  kMatchInProgress = 3,
};

const char* joinOutcomeName(JoinOutcome outcome) noexcept;

// 注入点：房间不认识传输层，出站只有这两个回调（§5.8 单播；§5.7-5 快照与事件留给 S12）。
struct RoomDeps {
  void* user = nullptr;
  void (*sendMatchState)(void* user, const Room& room, Session& session,
                         const ac::net::MatchState& state) = nullptr;
  void (*replicate)(void* user, Room& room) = nullptr;
};

struct Room {
  char code[kRoomCodeLength + 1u] = {};
  // 世界按房间堆分配（1 MB），不进静态区；分配/释放只发生在房间的生命周期边界上。
  std::unique_ptr<ac::sim::World> world{};
  uint32_t seed = 0u;
  Session* sessions[kMaxPlayersPerRoom] = {};
  uint8_t sessionCount = 0u;
  uint8_t capacity = static_cast<uint8_t>(kMaxPlayersPerRoom);
  ac::sim::Command commands[kMaxPlayersPerRoom] = {};
  uint8_t commandCount = 0u;
  MatchRuntime match{};
  ac::waves::DirectorState director{};
  MatchPhase phase = MatchPhase::kLobby;
  int32_t wave = 0;
  int64_t intermissionMs = 0;
  int64_t matchStateTimerMs = 0;
  int64_t emptySinceMs = -1;  // -1 = 非空闲
  uint64_t lastUpdateMs = 0u;
  uint32_t stateSignature = 0u;
  bool hasStateSignature = false;
  // §5.8 的单播缓冲：房间自持一份、反复重写（热路径零分配）。
  ac::net::MatchState matchState{};
  int64_t accumulatorMs = 0;     // §5.7-3 的固定步长累加器
  uint32_t unicastCount = 0u;    // 累计单播次数（节拍 + 立即补发）
  uint32_t immediateCount = 0u;  // 其中由签名变化触发的立即补发次数
};

// 建房：世界 = createWorld(seed) 后清掉占位玩家（v1 createWorldForRoom，pid 从 1 起）。
Room* createRoom(RoomRegistry& registry, uint64_t nowMs) noexcept;
void destroyRoom(RoomRegistry& registry, Room& room) noexcept;

// §5.4：加入（不校验令牌；带令牌的重连走 roomReconnect）。
JoinOutcome roomJoin(Room& room, Session& session, uint64_t nowMs) noexcept;

// §5.5：只按 token 匹配的接管（existing 是宽限期里的旧会话）。
bool roomReconnect(Room& room, Session& existing, Session& incoming) noexcept;

bool roomDisconnect(Room& room, Session& session, uint64_t nowMs) noexcept;
bool roomLeave(Room& room, Session& session, uint64_t nowMs) noexcept;
bool removeMember(Room& room, Session& session, uint64_t nowMs, bool leftMidMatch) noexcept;

bool roomIsIdle(const Room& room) noexcept;
uint8_t connectedSessionCount(const Room& room) noexcept;

// ready / 选枪：变化时立即补发 MatchState（§5.8）。
bool roomSetReady(Room& room, Session& session, bool ready, uint8_t weapon,
                  const RoomDeps& deps) noexcept;

// §5.4：写入「最近一条已净化命令」。
bool roomApplyCommand(Room& room, Session& session, const ac::sim::Command& command) noexcept;

Session* sessionByPid(Room& room, uint32_t pid) noexcept;

// §5.7：每 tick 推进。返回本次调用是否至少走了一个 tick。
bool updateRoom(Room& room, const RoomDeps& deps, uint64_t nowMs) noexcept;

// §5.7-3：装配本 tick 的命令（按 activeIds 升序，每个活动玩家一条）。
void buildCommands(Room& room) noexcept;

// 单 tick（v1 runTick 的 S10 部分）：命令 → stepWorld → 统计 → 清波/结束 → 复制 → MatchState。
bool roomTick(Room& room, const RoomDeps& deps, uint64_t nowMs) noexcept;

// §5.8：组装到 room.matchState（热路径零分配），返回写入的成员条数。
std::size_t buildMatchState(Room& room) noexcept;
// §5.8：组装 + 逐个已连接会话调用 deps.sendMatchState；顺带刷新签名（立即补发的判据）。
bool broadcastMatchState(Room& room, const RoomDeps& deps) noexcept;

}  // namespace ac::room
