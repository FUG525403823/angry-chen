// S05 §5.6 / §6：热路径零分配的计数缝。
// 只在本翻译单元替换全局 new/delete；计数只在 AllocationScope 里读，测量窗口内不做格式化输出。
#include "allocation_probe.hpp"
#include "tiny_test.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

#include "config/player.hpp"
#include "core/math.hpp"
#include "core/rng.hpp"
#include "world_test_support.hpp"
#include "sim/entity_table.hpp"
#include "sim/step.hpp"
#include "sim/spatial_grid.hpp"
#include "sim/world.hpp"

namespace {

std::size_t allocationCounter = 0u;  // 只在本 TU 递增；对外经 ac::test::allocationCount() 读

void* allocateRaw(std::size_t size) {
  return std::malloc(size == 0u ? 1u : size);
}

void* allocateAligned(std::size_t size, std::size_t alignment) {
#if defined(_WIN32)
  return _aligned_malloc(size == 0u ? alignment : size, alignment);
#else
  return std::aligned_alloc(alignment, size == 0u ? alignment : size);
#endif
}

void freeAligned(void* memory) noexcept {
#if defined(_WIN32)
  _aligned_free(memory);
#else
  std::free(memory);
#endif
}

}  // namespace

namespace ac::test {

std::size_t allocationCount() noexcept { return allocationCounter; }

}  // namespace ac::test

void* operator new(std::size_t size) {
  ++allocationCounter;
  void* memory = allocateRaw(size);
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](std::size_t size) {
  ++allocationCounter;
  void* memory = allocateRaw(size);
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  ++allocationCounter;
  return allocateRaw(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  ++allocationCounter;
  return allocateRaw(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
  ++allocationCounter;
  void* memory = allocateAligned(size, static_cast<std::size_t>(alignment));
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  ++allocationCounter;
  void* memory = allocateAligned(size, static_cast<std::size_t>(alignment));
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  ++allocationCounter;
  return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  ++allocationCounter;
  return allocateAligned(size, static_cast<std::size_t>(alignment));
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete(void* memory, std::size_t, std::align_val_t) noexcept { freeAligned(memory); }
void operator delete[](void* memory, std::size_t, std::align_val_t) noexcept { freeAligned(memory); }

namespace {

constexpr uint32_t kSeed = 0xC0FFEEu;
namespace sim = ac::sim;
using ac::test::AllocationScope;
using ac::test::stepEmpty;

void populate(sim::World& world, std::size_t count) {
  for (std::size_t i = 0u; i < count; ++i) {
    // 铺满 [-16, 15] × [-16, -1]：邻居查询（含 z = 0 附近的点）一定能命中
    const double x = static_cast<double>(static_cast<int32_t>(i % 32u)) - 16.0;
    const double z = static_cast<double>(static_cast<int32_t>((i / 32u) % 32u)) - 16.0;
    sim::spawnEntity(world, sim::EntityKind::kSheep, ac::Vec3{x, 0.0, z});
  }
  stepEmpty(world);
}

std::size_t scanNeighbors(const sim::SpatialGrid& grid, std::size_t rounds) {
  std::size_t visited = 0u;
  for (std::size_t i = 0u; i < rounds; ++i) {
    const double x = static_cast<double>(static_cast<int32_t>(i % 79u)) - 39.0;
    sim::forEachNeighbor(grid, x, 0.0, 8.0, [&visited](uint16_t) { ++visited; });
  }
  return visited;
}

}  // namespace

// ---------- 零分配（--filter=alloc）----------

AC_TEST(alloc_steady_state_is_zero) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;

  // 稳态 tick：生成 → 局部写字段 → 推进世界 → 邻居查询 → 压事件（全部走预分配结构）
  const auto hotTick = [&world](uint32_t tick) {
    const sim::SpawnResult sheep =
        sim::spawnEntity(*world, sim::EntityKind::kSheep, ac::Vec3{2.0, 0.0, 3.0});
    const sim::SpawnResult bullet =
        sim::spawnEntity(*world, sim::EntityKind::kProjectile, ac::Vec3{2.0, 1.0, 3.0});
    if (sheep.isOk) {
      sim::Entity* entity = sim::entityById(*world, sheep.id);
      if (entity != nullptr) {
        entity->aliveMs = tick;
        entity->pos.x = 2.0 + static_cast<double>(tick % 7u);
      }
    }
    if (bullet.isOk && (tick % 5u) == 0u) sim::despawnEntity(*world, bullet.id);
    stepEmpty(*world);
    std::size_t visited = 0u;
    sim::forEachNeighbor(world->grid, 0.0, 0.0, 8.0, [&visited](uint16_t) { ++visited; });
    sim::Event event{};
    event.eventId = tick + 1u;
    event.type = 1u;
    sim::pushEvent(*world, event);
  };

  for (uint32_t tick = 0u; tick < 10u; ++tick) hotTick(tick);  // 预热 10 tick
  const AllocationScope window;
  for (uint32_t tick = 0u; tick < 600u; ++tick) hotTick(tick);  // 稳态 600 tick
  const std::size_t allocations = window.since();
  std::printf("steadyStateAllocations=%zu\n", allocations);
  AC_CHECK_EQ(allocations, 0u);
  AC_CHECK(world->tick >= 610u);
}

AC_TEST(alloc_spawn_release_is_heap_free) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;
  populate(*world, 512u);

  const AllocationScope window;
  std::size_t spawned = 0u;
  for (std::size_t i = 0u; i < 10000u; ++i) {
    const sim::SpawnResult result =
        sim::spawnEntity(*world, sim::EntityKind::kSheep, ac::Vec3{1.0, 0.0, 1.0});
    if (result.isOk) {
      ++spawned;
      sim::despawnEntity(*world, result.id);
    }
  }
  const std::size_t allocations = window.since();
  std::printf("spawnReleaseAllocations=%zu\n", allocations);
  AC_CHECK_EQ(spawned, 10000u);
  AC_CHECK_EQ(allocations, 0u);
}

AC_TEST(alloc_neighbor_scan_is_heap_free) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;
  populate(*world, 512u);

  const AllocationScope window;
  const std::size_t visited = scanNeighbors(world->grid, 20000u);
  const std::size_t allocations = window.since();
  std::printf("neighborScanAllocations=%zu\n", allocations);
  AC_CHECK(visited > 0u);
  AC_CHECK_EQ(allocations, 0u);
}

AC_TEST(alloc_tick_loop_is_heap_free) {
  std::unique_ptr<sim::World> world = sim::createWorld(kSeed);
  AC_CHECK(world != nullptr);
  if (world == nullptr) return;
  populate(*world, 512u);

  const AllocationScope window;
  for (std::size_t i = 0u; i < 600u; ++i) {
    sim::Entity* entity = sim::entityById(*world, static_cast<uint16_t>(1u + (i % 512u)));
    if (entity != nullptr) entity->pos.z = static_cast<double>(i % 17u);
    stepEmpty(*world);
  }
  const std::size_t allocations = window.since();
  std::printf("tickLoopAllocations=%zu\n", allocations);
  AC_CHECK_EQ(allocations, 0u);
  AC_CHECK_EQ(world->poseHistory.writeCount, sim::kPoseHistorySlots);
}
