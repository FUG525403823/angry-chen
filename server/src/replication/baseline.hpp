#pragma once
// S12 §5（§3 交付物 1）：每客户端复制基线 = 「客户端已有」的记录镜像 + 基线 tick。
//
// 冻结语义：快照通道不可靠，基线在**编码完成时**随镜像一起前进，**不等客户端 ack**；
// 客户端丢失恢复靠 baselineTick = 0（强制全量）语义 + 每 40 tick 的强制全量节拍。
#include <cstddef>
#include <cstdint>

#include "net/codec.hpp"

namespace ac::replication {

inline constexpr uint32_t kFullSnapshotIntervalTicks = 40u;  // 2 s（§5：与档位解耦，按 tick 计数）
inline constexpr std::size_t kBaselineCapacity = 256u;       // §5：presentIds u16[256]
inline constexpr std::size_t kMaxRecordsPerFrame = 255u;     // §5：单帧实体记录由 u8 count 决定

// 镜像直接复用 S03 §5.3 已冻结的 net::SnapshotBaseline（tick + id 升序的 EntityRecord 列表）。
// 不再另建一套定长 presentIds/record 数组：两份状态必然漂移，且编码器本来就吃这个类型。
// 零分配前提：每个会话建立时 reserveBaseline() 一次（容量 256），此后 upsert 都在容量内。
struct ClientBaseline {
  net::SnapshotBaseline mirror{};   // mirror.tick == 0 → 强制全量
  uint32_t lastFullTick = 0u;       // 上一次全量编码的 tick
  uint32_t framesSinceFull = 0u;    // 自上次全量以来编码成功的帧数（诊断用）
};

void reserveBaseline(ClientBaseline& baseline) noexcept;
void resetBaseline(ClientBaseline& baseline) noexcept;  // 新客户端 / 丢帧自愈：baselineTick = 0
bool shouldForceFull(const ClientBaseline& baseline, uint32_t tick) noexcept;
void advanceBaseline(ClientBaseline& baseline, uint32_t tick, const net::EntityRecord* records,
                     std::size_t recordCount, const uint16_t* removedIds,
                     std::size_t removedCount) noexcept;
std::size_t baselineRecordCount(const ClientBaseline& baseline) noexcept;
const net::EntityRecord* baselineFind(const ClientBaseline& baseline, uint16_t id) noexcept;

// 移除列表 = 基线里存在、本帧不存在的 id（升序；最多 255 条）。
std::size_t collectRemovedIds(const ClientBaseline& baseline, const net::EntityRecord* records,
                              std::size_t recordCount, uint16_t* out,
                              std::size_t capacity) noexcept;

}  // namespace ac::replication
