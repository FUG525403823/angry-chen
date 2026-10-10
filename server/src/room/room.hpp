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

#include "config/pickup.hpp"

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
  // §5.7-5 + S03 §5.3/§5.4：事件条目生产（房间/广播侧）+ **未确认队列**（可靠下发）。
  // `pendingEvents[0, pendingEventCount)` 是尚未确认送达的条目（FIFO、eventId 升序）；每代帧取队首
  // ≤ kMaxEventsPerFrame 条拷进 `eventEntries` 发出，但**不出队**：只有复制侧确认「本代帧真的发出去
  // 了」（confirmFrameEvents）才递减 `pendingEventPasses`，用满 `kEventSendPasses` 次才出队。
  // ⇒ 被跳过的 tick（档位降档 / 背压 / 编码失败）不清空待发队列，条目在后续帧重发；客户端按 eventId
  // 去重（§5.4 幂等键），重发对它无副作用。
  static constexpr std::size_t kPendingEventCapacity = ac::sim::kMaxEvents;
  // 重传窗口 = 每条目随帧下发的次数。冻结设计的 EventChannel 是「重传直到 ack」，但客户端不产生事件
  // 通道的回程 ack（C03 §5.4 登记缺口）⇒ 以「窗口内重复下发」近似，理由与口径见 server/README.md。
  static constexpr uint8_t kEventSendPasses = 2u;
  ac::net::EventEntry pendingEvents[kPendingEventCapacity] = {};
  uint8_t pendingEventPasses[kPendingEventCapacity] = {};  // 与 pendingEvents 同步：该条还要下发几次
  uint16_t pendingEventCount = 0u;
  ac::net::EventEntry eventEntries[ac::net::kMaxEventsPerFrame] = {};
  uint8_t eventEntryCount = 0u;
  uint32_t nextEventId = 1u;         // 幂等键水位：从 1 起单调递增、全局唯一、永不重用（S03 §5.4）
  uint32_t eventOverflowCount = 0u;  // 队列也满时才真丢：计入 ac_events_dropped_total（G8）
  uint32_t eventFrameGeneration = 0u;      // 每次舞台化 ++：本代帧的世代戳
  uint32_t eventConfirmedGeneration = 0u;  // 已确认过的代（同一代重复确认无效）
  // S16：本场比赛的弹药补给箱实体 id（loading→playing 生成，resetMatchForRestart 回收）。
  uint16_t ammoCrateIds[ac::config::kAmmoCrateCount] = {};
  uint8_t ammoCrateCount = 0u;
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

// §5.3 可靠事件通道的确认侧：复制侧在**本代帧真的发出**（交给 socket）之后调用，
// sentCount = 该帧实际带出的条目数。同一代（frameGeneration）只确认一次（多客户端同 tick 发帧不会
// 重复扣窗口）；被跳过的 tick 不调用 ⇒ 条目留在未确认队列里，下一帧续投。
void confirmFrameEvents(Room& room, uint32_t frameGeneration, std::size_t sentCount) noexcept;

// §5.8：组装到 room.matchState（热路径零分配），返回写入的成员条数。
std::size_t buildMatchState(Room& room) noexcept;
// §5.8：组装 + 逐个已连接会话调用 deps.sendMatchState；顺带刷新签名（立即补发的判据）。
bool broadcastMatchState(Room& room, const RoomDeps& deps) noexcept;

// S16：补给箱生命周期（loading→playing 生成 / resetMatchForRestart 回收；失败只跳过，不影响开局）。
void spawnAmmoCrates(Room& room) noexcept;
void despawnAmmoCrates(Room& room) noexcept;

}  // namespace ac::room
