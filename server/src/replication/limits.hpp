#pragma once
// S12 §5 的容量/字节上限：**唯一来源**是 S03/S04 已冻结的常量，本文件只做派生 + static_assert。
// （S11 §12.2 的先例：同一个数值不许有两个名字；字面量只留在 net 那一处。）
#include <cstddef>

#include "net/keepalive.hpp"
#include "net/wire.hpp"

namespace ac::replication {

inline constexpr std::size_t kMaxSnapshotBytes = ac::net::kMaxSnapshotBytes;            // 2048 B（含包头）
inline constexpr std::size_t kSteadySnapshotBudgetBytes = ac::net::kSteadySnapshotBytes; // 1228 B（稳态均值）
inline constexpr std::size_t kMaxRecordsPerFrame = ac::net::kMaxEntityRecordsPerFrame;  // 128（S03 编码器硬上限）
inline constexpr std::size_t kMaxRemovedPerFrame = ac::net::kMaxRemovedPerFrame;        // 255
inline constexpr std::size_t kBaselineCapacity = 256u;                                  // §5：presentIds u16[256]
inline constexpr std::size_t kOutboundBacklogBytes = ac::net::kOutboundBacklogBytes;    // 65536
inline constexpr std::size_t kBacklogDownshiftBytes = kOutboundBacklogBytes / 2u;       // 32768

static_assert(kMaxSnapshotBytes == 2048u, "§5：单快照上限 2048 B");
static_assert(kSteadySnapshotBudgetBytes == 1228u, "§5：稳态 ≤ 1228 B");
static_assert(kMaxRecordsPerFrame == 128u,
              "§5 行 62 的 u8 count（≤255）与 S03 编码器上限（128）冲突：取编码器上限，见 README §13.1");
static_assert(kBaselineCapacity == 256u, "§5：presentIds u16[256]");
static_assert(kOutboundBacklogBytes == 65536u, "§5：单连接队列预算 64 KiB");
static_assert(kBacklogDownshiftBytes == 32768u, "§5：积压 > 32 KiB 触发降档");

}  // namespace ac::replication
