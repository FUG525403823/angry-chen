#pragma once
// S10 §5.2/§5.5/§5.6/§5.7：对局阶段推进、开局、清波、结束判定、补给与统计累积。
// 冻结时长逐条对应 v1 packages/server/src/match/controller.ts 的常量。
#include <cstdint>

#include "room/phase.hpp"
#include "room/stats.hpp"

// 前置声明必须在 namespace ac::room 之外：在 ac::room 内写 namespace ac::sim 会创建
// ac::room::ac::sim，从而把后面所有 ac::xxx 限定名解析成 ac::room::ac::xxx。
namespace ac::sim {
struct Entity;
}  // namespace ac::sim

namespace ac::room {

struct Room;
struct RoomDeps;
struct Session;

// v1 的 room.world.entities[pid - 1]：pid 就是玩家实体的 EntityId；非活动玩家返回空。
ac::sim::Entity* playerEntityAt(Room& room, uint32_t pid) noexcept;
const ac::sim::Entity* playerEntityAt(const Room& room, uint32_t pid) noexcept;

inline constexpr int64_t kLoadingMs = 1500;              // v1 LOADING_MS（30 tick）
inline constexpr int64_t kIntermissionMs = 20000;        // v1 INTERMISSION_MS（400 tick）
inline constexpr int64_t kIntermissionSkipMinMs = 5000;  // v1 INTERMISSION_SKIP_MIN_MS
inline constexpr int64_t kGracePeriodMs = 30000;         // v1 GRACE_PERIOD_MS
inline constexpr int64_t kEmptyRoomReclaimMs = 60000;    // v1 ALL_DISCONNECTED_RECLAIM_MS
inline constexpr double kReconnectMinHpRatio = 0.5;      // v1 RECONNECT_MIN_HP_RATIO
inline constexpr int64_t kMatchStateIntervalMs = 1000;   // §5.8 节拍（20 tick）
inline constexpr uint32_t kTickCatchUpLimit = 5u;        // v1 LIMITS.tickCatchUpLimit

// v1 match/controller.ts 的 MatchRuntime（records 用定长表替 Map，容量 = 4）。
struct MatchRuntime {
  uint32_t hostId = 0u;
  uint8_t winnerTeam = 0u;
  uint64_t startedAtMs = 0u;
  uint64_t endedAtMs = 0u;
  int64_t loadingMs = 0;
  PlayerRecord records[kMaxPlayersPerRoom] = {};
  uint8_t recordCount = 0u;
  MatchCounters counters{};
  int32_t shotMagBefore[kMaxPlayersPerRoom] = {-1, -1, -1, -1};
  int32_t shotSlotBefore[kMaxPlayersPerRoom] = {-1, -1, -1, -1};
  // endMatch 的产出（S13 落盘前先留在房间上，也是用例的稳定出口）。
  MatchResultRecord lastResult{};
  bool hasResult = false;
};

// v1 tryStartMatch 的四态返回值。
enum class StartOutcome : uint8_t {
  kOk = 0,
  kNotHost = 1,
  kNotReady = 2,
  kWrongPhase = 3,
};

const char* startOutcomeName(StartOutcome outcome) noexcept;

// §5.1：phase == next 视为已达成（返回 true，不记警告）；其余走 canMatchTransition 一张表，
// 非法时 room.match.counters.deniedTransitions += 1 并返回 false（绝不静默改状态）。
bool applyMatchTransition(Room& room, MatchPhase next) noexcept;

// §5.2：至少 1 名已分配 pid 的成员且全部 ready。
bool areAllPlayersReady(const Room& room) noexcept;

// §5.2/§5.7-2：开局的唯一入口。
StartOutcome tryStartMatch(Room& room, Session& session) noexcept;

// §5.2：loading / intermission 计时与全准备跳过；返回是否发生了阶段变化（需要立即补发 MatchState）。
bool updateMatch(Room& room, uint64_t nowMs, uint32_t elapsedMs) noexcept;

// §5.7-3：清波（复苏倒地玩家 → 进 intermission 或结束）。返回是否处理了本 tick。
bool handleWaveCleared(Room& room, uint64_t nowMs) noexcept;

// §5.7-4：仅 playing，无已连接玩家或全部已连接玩家倒地 → winnerTeam = 1。
bool checkMatchEnd(Room& room, uint64_t nowMs) noexcept;

// §5.6：结束对局并留下结算。winnerTeam：0 = 玩家方，1 = 羊群。
void endMatch(Room& room, uint64_t nowMs, int winnerTeam) noexcept;

// §5.6：结算记录（不改房间状态）。
MatchResultRecord buildMatchResult(const Room& room) noexcept;

// §5.2：全体补给（生命/护甲/三弹匣/备弹/换弹/倒地/怒气）。
void refillPlayers(Room& room) noexcept;

void joinMatchRecord(Room& room, uint32_t pid, const char* name, uint8_t nameBytes) noexcept;
PlayerRecord* playerRecordFor(Room& room, uint32_t pid) noexcept;

// §5.6 的 shotsFired 差分：切枪跨帧不补记。
void captureShotBaseline(Room& room) noexcept;
void applyShotDeltas(Room& room) noexcept;

// §5.6：只读当 tick 的事件，跨 tick 不重放。
void accumulateMatchEvents(Room& room) noexcept;
void accumulateMatchTime(Room& room, uint32_t dtMs) noexcept;

// ended → lobby 的重置（清战绩、波次、导演，回收羊与投射物，全体补满）。
void resetMatchForRestart(Room& room) noexcept;

uint32_t activePlayerCount(const Room& room) noexcept;

// §5.8：立即补发的触发判据（相位 / 波次 / 成员 / 准备 / 倒地 / 复活）。
uint32_t matchStateSignature(const Room& room) noexcept;

}  // namespace ac::room
