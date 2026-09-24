// S14 §3 交付物 2：无网络微基准 —— 固定步长与差分编码的 CPU 定位（不替代 gate 的端到端门槛）。
//
// 输出 step / encode 两个阶段的 p50/p95/max 与快照字节分布，用于回答「CPU 花在模拟还是编码上」。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "config/waves.hpp"
#include "core/quantize.hpp"
#include "core/rng.hpp"
#include "core/version.hpp"
#include "replication/baseline.hpp"
#include "replication/delta.hpp"
#include "sim/arena.hpp"
#include "sim/step.hpp"
#include "sim/world.hpp"
#include "waves/director.hpp"

namespace {

struct Options {
  std::uint32_t ticks = 6000u;
  int sheep = 60;
  std::uint32_t seed = 20260101u;
  int players = 4;
  int warmupTicks = 100;
};

void printUsage(const char* argv0) {
  std::printf("usage: %s [--ticks N] [--sheep M] [--seed S] [--players P] [--warmup N]\n", argv0);
}

bool parseOptions(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const std::size_t eq = arg.find('=');
    const std::string key = eq == std::string::npos ? arg : arg.substr(0u, eq);
    const std::string value = eq == std::string::npos ? std::string() : arg.substr(eq + 1u);
    if (key == "--help" || key == "-h") {
      printUsage(argv[0]);
      return false;
    }
    if (key == "--ticks") options.ticks = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    else if (key == "--sheep") options.sheep = std::atoi(value.c_str());
    else if (key == "--seed") options.seed = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    else if (key == "--players") options.players = std::atoi(value.c_str());
    else if (key == "--warmup") options.warmupTicks = std::atoi(value.c_str());
    else {
      std::fprintf(stderr, "unknown argument: %s\n", arg.c_str());
      return false;
    }
  }
  return true;
}

// 与 S12/S13 同口径的近邻秩分位（升序输入）：sorted[ceil(q * N) - 1]。
double percentile(const std::vector<double>& sorted, double q) {
  if (sorted.empty()) return 0.0;
  std::size_t rank = (sorted.size() * static_cast<std::size_t>(q * 100.0) + 99u) / 100u;
  if (rank == 0u) rank = 1u;
  if (rank > sorted.size()) rank = sorted.size();
  return sorted[rank - 1u];
}

std::uint32_t microsSince(std::chrono::steady_clock::time_point from) {
  return static_cast<std::uint32_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - from)
          .count());
}

}  // namespace

int main(int argc, char** argv) {
  Options options{};
  if (!parseOptions(argc, argv, options)) return 2;

  std::unique_ptr<ac::sim::World> world = ac::sim::createWorld(options.seed);
  if (world == nullptr) {
    std::fprintf(stderr, "createWorld failed\n");
    return 1;
  }
  // 玩家实体：取世界里已有的 player 实体（createWorld 的占位玩家），最多 options.players 个。
  std::vector<std::uint16_t> playerIds{};
  constexpr std::size_t kEntityCapacity = sizeof(world->entities) / sizeof(world->entities[0]);
  for (std::size_t i = 0u; i < kEntityCapacity && playerIds.size() < static_cast<std::size_t>(options.players); ++i) {
    if (world->entities[i].kind == ac::sim::EntityKind::kPlayer) {
      playerIds.push_back(static_cast<std::uint16_t>(world->entities[i].id));
    }
  }
  for (int i = 0; i < options.sheep; ++i) {
    const ac::Vec3 point = ac::sim::arena::kSpawnPoints[static_cast<std::size_t>(i) % ac::sim::arena::kSpawnPoints.size()];
    (void)ac::waves::spawnSheepAt(*world, ac::config::SheepKind::kGrunt, point.x, point.z);
  }
  world->stats.aliveSheep += static_cast<std::uint32_t>(options.sheep);

  std::vector<ac::sim::Command> commands(playerIds.size());
  for (std::size_t i = 0u; i < commands.size(); ++i) {
    commands[i].moveX = static_cast<double>(i % 3u) - 1.0;
    commands[i].yaw = static_cast<double>(i) * 0.5;
    commands[i].buttons = static_cast<std::uint8_t>(i % 2u == 0u ? 0x09u : 0x00u);
  }
  std::vector<ac::replication::ClientBaseline> baselines(playerIds.size());
  for (ac::replication::ClientBaseline& baseline : baselines) ac::replication::reserveBaseline(baseline);

  std::vector<double> stepMicros{};
  std::vector<double> encodeMicros{};
  std::vector<double> snapshotBytes{};
  std::vector<double> projectMicros{};
  stepMicros.reserve(options.ticks);
  encodeMicros.reserve(options.ticks);
  snapshotBytes.reserve(options.ticks * playerIds.size());
  std::uint8_t buffer[ac::net::kMaxSnapshotBytes] = {};
  // §4-2 的「投影」段：世界 → 线上记录的镜像数组（定长，循环外分配一次）。
  std::vector<ac::net::EntityRecord> records(kEntityCapacity);
  const auto started = std::chrono::steady_clock::now();

  for (std::uint32_t tick = 0u; tick < options.ticks; ++tick) {
    auto mark = std::chrono::steady_clock::now();
    (void)ac::sim::stepWorld(*world, commands.data(), static_cast<std::uint32_t>(commands.size()),
                             ac::version::kTickMs);
    const std::uint32_t stepUs = microsSince(mark);
    mark = std::chrono::steady_clock::now();
    (void)ac::replication::projectWorld(*world, records.data(), records.size());
    const std::uint32_t projectUs = microsSince(mark);
    mark = std::chrono::steady_clock::now();
    for (std::size_t i = 0u; i < playerIds.size(); ++i) {
      ac::replication::DeltaInput input{};
      input.world = world.get();
      input.session = static_cast<std::uint16_t>(i);
      input.seq = static_cast<std::uint16_t>(tick);
      input.isForceFull = ac::replication::shouldForceFull(baselines[i], world->tick);
      const ac::replication::DeltaOutcome outcome =
          ac::replication::encodeDelta(input, baselines[i], buffer, sizeof(buffer));
      if (outcome.isOk && tick >= static_cast<std::uint32_t>(options.warmupTicks)) {
        snapshotBytes.push_back(static_cast<double>(outcome.bytes));
      }
    }
    const std::uint32_t encodeUs = microsSince(mark);
    if (tick >= static_cast<std::uint32_t>(options.warmupTicks)) {
      stepMicros.push_back(static_cast<double>(stepUs));
      projectMicros.push_back(static_cast<double>(projectUs));
      encodeMicros.push_back(static_cast<double>(encodeUs));
    }
  }
  const double totalSeconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

  std::sort(stepMicros.begin(), stepMicros.end());
  std::sort(projectMicros.begin(), projectMicros.end());
  std::sort(encodeMicros.begin(), encodeMicros.end());
  std::sort(snapshotBytes.begin(), snapshotBytes.end());
  std::printf("bench ticks=%u sheep=%d players=%zu entities=%zu wall=%.3fs (%.0f ticks/s)\n",
              options.ticks, options.sheep, playerIds.size(), kEntityCapacity, totalSeconds,
              static_cast<double>(options.ticks) / (totalSeconds > 0.0 ? totalSeconds : 1.0));
  std::printf("step   p50=%.0fus p95=%.0fus max=%.0fus\n", percentile(stepMicros, 0.50),
              percentile(stepMicros, 0.95), stepMicros.empty() ? 0.0 : stepMicros.back());
  std::printf("project p50=%.0fus p95=%.0fus max=%.0fus\n", percentile(projectMicros, 0.50),
              percentile(projectMicros, 0.95), projectMicros.empty() ? 0.0 : projectMicros.back());
  std::printf("encode p50=%.0fus p95=%.0fus max=%.0fus\n", percentile(encodeMicros, 0.50),
              percentile(encodeMicros, 0.95), encodeMicros.empty() ? 0.0 : encodeMicros.back());
  std::printf("snapshot bytes avg=%.1f p95=%.0f max=%.0f samples=%zu\n",
              snapshotBytes.empty()
                  ? 0.0
                  : std::accumulate(snapshotBytes.begin(), snapshotBytes.end(), 0.0) /
                        static_cast<double>(snapshotBytes.size()),
              percentile(snapshotBytes, 0.95), snapshotBytes.empty() ? 0.0 : snapshotBytes.back(),
              snapshotBytes.size());
  return 0;
}
