#pragma once
// S07 §5：fixture（跨语言对拍向量）的 schema v2 读取、逐帧哈希链重算、逐位比较与 DIFF 报告。
// 只服务测试：允许 std::string / std::vector / 文件 IO；sim 热路径的零分配约束与本文件无关。
//
// v2 口径（已裁决，见 docs/evidence/fixtures/README.md §4 / ADR-010 §8）：盘上只存
//   场景名/版本/种子/dtMs/configHash + 初态 setup + 命令脚本 script（按 tick 折叠）+ 少量关键帧
//   + 逐帧哈希链 hashChain（每 tick 对「当帧全量投影」算 FNV-1a 64，链式累积）。
// 「全量投影」的字节表示与旧口径逐字节相同（%.17g 全字段），所以对拍强度不变，只是不落盘每帧全文。
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "sim/entity_table.hpp"

namespace ac::test {

// §5.1 冻结 schema v2。
inline constexpr uint32_t kFixtureSchemaVersion = 2u;
inline constexpr uint32_t kFixtureDtMs = 50u;

// 命令脚本里的一条命令：seq / clientTick 不落盘，由 tick 派生（seq = tick % 65536，clientTick = tick），
// 两侧同式（v1 applyCommands 只用到这些字段）。
struct FixtureCommand {
  uint16_t id = 0u;
  double moveX = 0.0;
  double moveY = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
  uint8_t buttons = 0u;
  uint8_t switchTo = 0u;
  // 投影文本用：由 tick 派生的两个字段（不参与 JSON 解析）。
  uint16_t seq = 0u;
  uint32_t clientTick = 0u;
};

struct FixtureEntity {
  uint16_t id = 0u;
  std::string kind;
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
  double hp = 0.0;
  uint8_t flags = 0u;
};

struct FixtureEvent {
  uint32_t tick = 0u;
  std::string type;
  uint8_t flags = 0u;
  uint16_t subjectId = 0u;
  uint16_t targetId = 0u;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
  double value = 0.0;
};

struct FixtureRng {
  uint32_t ai = 0u;
  uint32_t spawn = 0u;
  uint32_t fx = 0u;
};

// 关键帧：少量 tick 上的全量投影（与逐帧投影同口径，只是也落盘做「字段级首个差异」定位）。
struct FixtureKeyframe {
  uint32_t tick = 0u;
  std::vector<FixtureEntity> entities;
  std::vector<FixtureEvent> events;
  FixtureRng rng{};
};

// 命令脚本的一段：tick ∈ [from, to] 内每 tick 发出的命令逐字段相同。
struct FixtureRun {
  uint32_t from = 0u;
  uint32_t to = 0u;
  std::vector<FixtureCommand> commands;
};

// 快照字段组（snapshot-roundtrip-240t）：编码字节块的哈希 + 解码后投影文本的哈希。
struct FixtureSnapshot {
  uint32_t tick = 0u;
  uint32_t records = 0u;
  uint64_t encodeHash = 0u;
  uint64_t decodeHash = 0u;
};

struct FixturePlayerSetup {
  uint16_t id = 0u;
  double hp = 0.0;
  double armor = 0.0;
};

struct FixtureSheepSetup {
  std::string kind;
  double x = 0.0;
  double z = 0.0;
};

struct FixtureSetup {
  std::vector<FixturePlayerSetup> players;
  std::vector<FixtureSheepSetup> sheep;
};

struct Fixture {
  std::string name;
  uint32_t version = 0u;
  uint32_t seed = 0u;
  uint32_t ticks = 0u;
  std::string configHash;
  FixtureSetup setup;
  uint32_t startWave = 0u;  // 0 = 不驱动外部导演
  std::vector<FixtureRun> script;
  std::vector<FixtureKeyframe> keyframes;
  std::vector<FixtureSnapshot> snapshot;
  std::vector<uint64_t> hashChain;

  // 展开脚本：取 tick 的命令（要求脚本连续覆盖 1..ticks；缺覆盖返回 false）。
  bool commandAt(uint32_t tick, std::vector<FixtureCommand>& out) const;
  // 脚本自检：必须从 tick 1 开始、逐段连续、覆盖到 ticks。
  bool scriptCoversAllTicks() const;
};

// FNV-1a 64（与 tools/export-fixtures.mjs 的 fnv1a64 同口径；公开测试向量见 fixture_io.cpp 自检）。
inline constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
inline constexpr uint64_t kFnvPrime = 0x100000001b3ull;
uint64_t fnv1a64(const void* data, std::size_t length, uint64_t seed) noexcept;
uint64_t fnv1a64Text(const std::string& text, uint64_t seed) noexcept;
std::string hash64Hex(uint64_t value);
bool selfTestFnv();

// 当帧全量投影文本（两侧逐字节同口径）：tick / dtMs / 逐条 cmd / 逐实体 ent / 逐事件 evt / rng。
std::string projectionText(uint32_t tick, const std::vector<FixtureCommand>& commands,
                           const std::vector<FixtureEntity>& entities,
                           const std::vector<FixtureEvent>& events, const FixtureRng& rng);

// 快照字段组：S03 §5.4 的 15 字节实体记录（仅实体记录块，不含帧头）。
inline constexpr std::size_t kSnapshotRecordBytes = 15u;
inline constexpr uint32_t kSnapshotHeadBytes = 22u;

struct SnapshotRecordFields {
  uint16_t id = 0u;
  uint8_t kind = 0u;
  uint8_t flags = 0u;
  int16_t xCm = 0;
  int16_t yCm = 0;
  int16_t zCm = 0;
  uint16_t yawUnits = 0u;
  uint16_t pitchUnits = 0u;
  uint8_t hpRatioUnits = 0u;
  uint8_t state = 0u;
};

std::vector<SnapshotRecordFields> decodeSnapshotRecords(const uint8_t* block, uint32_t count);
std::string snapshotDecodeText(const std::vector<SnapshotRecordFields>& records);

// 目录来源：CMake 注入 AC_EVIDENCE_FIXTURE_DIR（绝对路径）；直编兜底回退到仓库根相对路径。
std::string fixtureDir();
bool loadFixtureByName(const std::string& name, Fixture& out, std::string& error);
bool loadFixtureFile(const std::string& path, Fixture& out, std::string& error);

// §5.6 configHash：与 tools/export-fixtures.mjs 的 configHashText 逐字节同口径（%.17g + 固定组序）。
std::string configHashText();
uint32_t configHash();
std::string hashHex(uint32_t hash);
std::string doubleHex(double value);

// §5.1 flags 位表（与 tools/export-fixtures.mjs 的 FLAGS 同名同值）。
inline constexpr uint8_t kFlagDowned = 1u;
inline constexpr uint8_t kFlagRageMode = 2u;
inline constexpr uint8_t kFlagReloading = 4u;
inline constexpr uint8_t kFlagCharging = 8u;  // 只出现在 v1 的快照位表（net.ts）→ S12
inline constexpr uint8_t kFlagFading = 16u;   // 同上
inline constexpr uint8_t kFlagIdle = 32u;

// bit0 downed / bit1 rageMode / bit2 reloading / bit3 charging / bit4 fading / bit5 idle。
uint8_t entityFlagsOf(const ac::sim::Entity& entity, double nowMs) noexcept;
const char* kindName(ac::sim::EntityKind kind) noexcept;

// §5.4 比较：只记录**首个**差异，报告格式冻结。
struct DiffSink {
  std::string fixture;
  bool has = false;
  uint32_t tick = 0u;
  std::string field;
  std::string expected;
  std::string actual;

  void doubleField(uint32_t tickIndex, const std::string& path, double expected, double actual);
  void intField(uint32_t tickIndex, const std::string& path, int64_t expected, int64_t actual);
  void stringField(uint32_t tickIndex, const std::string& path, const std::string& expected, const std::string& actual);
  std::string report() const;  // 无差异返回空串
};

}  // namespace ac::test
