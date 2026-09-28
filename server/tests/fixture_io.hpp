#pragma once
// S07 §3/§5：fixture（跨语言对拍向量）的固定 schema 读取、configHash 重算、逐位比较与 DIFF 报告。
// 只服务测试：允许 std::string / std::vector / 文件 IO；sim 热路径的零分配约束与本文件无关。
#include <cstdint>
#include <string>
#include <vector>

#include "sim/entity_table.hpp"

namespace ac::test {

// §5.1 冻结 schema（字段顺序 = 写盘顺序；未知键、缺键、dtMs != 50 一律失败）。
struct FixtureCommand {
  uint16_t id = 0u;
  uint16_t seq = 0u;
  uint32_t clientTick = 0u;
  double moveX = 0.0;
  double moveY = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
  uint8_t buttons = 0u;
  uint8_t switchTo = 0u;
};

struct FixtureEntity {
  uint16_t id = 0u;
  std::string kind;
  double posX = 0.0;
  double posY = 0.0;
  double posZ = 0.0;
  double yaw = 0.0;
  double pitch = 0.0;
  // hp 是 double：v1 的护甲吸收把玩家 hp 打成小数（例：步枪命中后 96.8），整数读取器会拒绝整份向量。
  // 比较仍是逐位（diff.doubleField），比原来的整数截断更严 —— 既有 4 份的 hp 恒为整数，逐字节结果不变。
  double hp = 0.0;
  uint8_t flags = 0u;
};

struct FixtureEvent {
  uint32_t tick = 0u;
  std::string type;
  uint8_t flags = 0u;
  uint16_t subjectId = 0u;
  uint16_t targetId = 0u;
  int32_t value = 0;
};

struct FixtureRng {
  uint32_t ai = 0u;
  uint32_t spawn = 0u;
  uint32_t fx = 0u;
};

struct FixtureTick {
  uint32_t dtMs = 0u;
  std::vector<FixtureCommand> commands;
  std::vector<FixtureEntity> entities;
  std::vector<FixtureEvent> events;
  FixtureRng rng{};
};

struct Fixture {
  std::string name;
  uint32_t seed = 0u;
  std::string configHash;
  std::vector<FixtureTick> ticks;
};

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
// 与导出侧（tools/export-fixtures.mjs 的 flagsOf）同口径：bit0/1/2/5 有来源，bit3/4 只出现在 v1 的快照
// 位表（net.ts），两侧都不产出（恒 0）。nowMs 取该 tick 结束后的 world.timeMs（v1 的 isRageActive 用它）。
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
