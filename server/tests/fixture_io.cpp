// S07 §5.1/§5.4/§5.6：fixture 的固定 schema 解析、configHash 重算与 DIFF 报告。
#include "fixture_io.hpp"

#include <bit>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "config/combat.hpp"
#include "config/player.hpp"
#include "config/weapons.hpp"
#include "core/hash.hpp"
#include "sim/arena.hpp"

namespace ac::test {
namespace {

constexpr char kSpace = 32;
constexpr char kTab = 9;
constexpr char kLf = 10;
constexpr char kCr = 13;
constexpr char kBackslash = 92;

// §5.1：浮点统一 %.17g（与导出脚本的 g17 同口径；含 -0 的符号位）。
std::string g17(double value) {
  char buffer[64];
  const int written = std::snprintf(buffer, sizeof buffer, "%.17g", value);
  if (written <= 0 || written >= static_cast<int>(sizeof buffer)) return std::string("g17-overflow");
  return std::string(buffer, static_cast<std::size_t>(written));
}

std::string join17(const std::vector<double>& values) {
  std::string out;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i != 0u) out.push_back(',');
    out += g17(values[i]);
  }
  return out;
}

// 极简顺序读取器：schema 的键序即解析顺序，出现别的键就直接失败（§3 的「只认固定 schema」）。
struct Reader {
  const char* cursor = nullptr;
  const char* end = nullptr;
  std::string error;

  void fail(const std::string& message) {
    if (error.empty()) error = message;
  }

  void skipWhitespace() {
    while (cursor < end) {
      const char c = *cursor;
      if (c == kSpace || c == kTab || c == kLf || c == kCr) {
        ++cursor;
      } else {
        return;
      }
    }
  }

  bool literal(char expected) {
    skipWhitespace();
    if (cursor >= end || *cursor != expected) {
      fail(std::string("expected literal ") + expected);
      return false;
    }
    ++cursor;
    return true;
  }

  bool key(const char* name) {
    skipWhitespace();
    if (cursor >= end || *cursor != '"') {
      fail(std::string("expected key ") + name);
      return false;
    }
    ++cursor;
    const char* begin = cursor;
    while (cursor < end && *cursor != '"') {
      if (*cursor == kBackslash) {
        fail("escapes are not part of the frozen schema");
        return false;
      }
      ++cursor;
    }
    if (cursor >= end) {
      fail("unterminated key");
      return false;
    }
    const std::string found(begin, static_cast<std::size_t>(cursor - begin));
    ++cursor;
    if (found != name) {
      fail(std::string("unexpected key ") + found + ", expected " + name);
      return false;
    }
    return literal(':');
  }

  bool stringValue(std::string& out) {
    skipWhitespace();
    if (cursor >= end || *cursor != '"') {
      fail("expected string value");
      return false;
    }
    ++cursor;
    const char* begin = cursor;
    while (cursor < end && *cursor != '"') {
      if (*cursor == kBackslash) {
        fail("escapes are not part of the frozen schema");
        return false;
      }
      ++cursor;
    }
    if (cursor >= end) {
      fail("unterminated string value");
      return false;
    }
    out.assign(begin, static_cast<std::size_t>(cursor - begin));
    ++cursor;
    return true;
  }

  bool unsignedValue(uint64_t limit, uint64_t& out) {
    skipWhitespace();
    if (cursor >= end || *cursor == '-') {
      fail("expected unsigned number");
      return false;
    }
    char* stop = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(cursor, &stop, 10);
    if (stop == cursor || errno != 0 || parsed > limit) {
      fail("unsigned number out of range");
      return false;
    }
    out = static_cast<uint64_t>(parsed);
    cursor = stop;
    return true;
  }

  bool signedValue(int64_t& out) {
    skipWhitespace();
    if (cursor >= end) {
      fail("expected number");
      return false;
    }
    char* stop = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(cursor, &stop, 10);
    if (stop == cursor || errno != 0) {
      fail("number out of range");
      return false;
    }
    out = static_cast<int64_t>(parsed);
    cursor = stop;
    return true;
  }

  bool doubleValue(double& out) {
    skipWhitespace();
    if (cursor >= end) {
      fail("expected number");
      return false;
    }
    char* stop = nullptr;
    errno = 0;
    const double parsed = std::strtod(cursor, &stop);
    if (stop == cursor || errno != 0) {
      fail("number out of range");
      return false;
    }
    out = parsed;
    cursor = stop;
    return true;
  }

  bool finish() {
    skipWhitespace();
    if (cursor != end) {
      fail("trailing content after the fixture object");
      return false;
    }
    return true;
  }
};

bool readUint32(Reader& reader, uint32_t& out, const char* what) {
  uint64_t value = 0u;
  if (!reader.unsignedValue(0xFFFFFFFFull, value)) {
    reader.fail(std::string(what) + ": " + reader.error);
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool readUint16(Reader& reader, uint16_t& out, const char* what) {
  uint64_t value = 0u;
  if (!reader.unsignedValue(0xFFFFull, value)) {
    reader.fail(std::string(what) + ": " + reader.error);
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

bool readUint8(Reader& reader, uint8_t& out, const char* what) {
  uint64_t value = 0u;
  if (!reader.unsignedValue(0xFFull, value)) {
    reader.fail(std::string(what) + ": " + reader.error);
    return false;
  }
  out = static_cast<uint8_t>(value);
  return true;
}

bool readCommand(Reader& reader, FixtureCommand& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("id")) return false;
  if (!readUint16(reader, out.id, "commands[].id")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("seq")) return false;
  if (!readUint16(reader, out.seq, "commands[].seq")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("clientTick")) return false;
  if (!readUint32(reader, out.clientTick, "commands[].clientTick")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("moveX")) return false;
  if (!reader.doubleValue(out.moveX)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("moveY")) return false;
  if (!reader.doubleValue(out.moveY)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("yaw")) return false;
  if (!reader.doubleValue(out.yaw)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("pitch")) return false;
  if (!reader.doubleValue(out.pitch)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("buttons")) return false;
  if (!readUint8(reader, out.buttons, "commands[].buttons")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("switchTo")) return false;
  if (!readUint8(reader, out.switchTo, "commands[].switchTo")) return false;
  return reader.literal('}');
}

bool readEntity(Reader& reader, FixtureEntity& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("id")) return false;
  if (!readUint16(reader, out.id, "entities[].id")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("kind")) return false;
  if (!reader.stringValue(out.kind)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("pos")) return false;
  if (!reader.literal('[')) return false;
  if (!reader.doubleValue(out.posX)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.doubleValue(out.posY)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.doubleValue(out.posZ)) return false;
  if (!reader.literal(']')) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("yaw")) return false;
  if (!reader.doubleValue(out.yaw)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("pitch")) return false;
  if (!reader.doubleValue(out.pitch)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("hp")) return false;
  int64_t hp = 0;
  if (!reader.signedValue(hp)) return false;
  out.hp = static_cast<int32_t>(hp);
  if (!reader.literal(',')) return false;
  if (!reader.key("flags")) return false;
  if (!readUint8(reader, out.flags, "entities[].flags")) return false;
  return reader.literal('}');
}

bool readEvent(Reader& reader, FixtureEvent& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("tick")) return false;
  if (!readUint32(reader, out.tick, "events[].tick")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("type")) return false;
  if (!reader.stringValue(out.type)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("flags")) return false;
  if (!readUint8(reader, out.flags, "events[].flags")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("subjectId")) return false;
  if (!readUint16(reader, out.subjectId, "events[].subjectId")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("targetId")) return false;
  if (!readUint16(reader, out.targetId, "events[].targetId")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("value")) return false;
  int64_t value = 0;
  if (!reader.signedValue(value)) return false;
  out.value = static_cast<int32_t>(value);
  return reader.literal('}');
}

bool readTick(Reader& reader, FixtureTick& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("dtMs")) return false;
  if (!readUint32(reader, out.dtMs, "ticks[].dtMs")) return false;
  if (out.dtMs != ac::config::kStepDtMs) {
    reader.fail("ticks[].dtMs must be 50 (S06 §5.1 固定步长)");
    return false;
  }
  if (!reader.literal(',')) return false;
  if (!reader.key("commands")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixtureCommand command;
      if (!readCommand(reader, command)) return false;
      if (!out.commands.empty() && command.id <= out.commands.back().id) {
        reader.fail("ticks[].commands must be ascending by id");
        return false;
      }
      out.commands.push_back(command);
      reader.skipWhitespace();
      if (reader.cursor < reader.end && *reader.cursor == ',') {
        ++reader.cursor;
        continue;
      }
      break;
    }
  }
  if (!reader.literal(']')) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("expected")) return false;
  if (!reader.literal('{')) return false;
  if (!reader.key("entities")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixtureEntity entity;
      if (!readEntity(reader, entity)) return false;
      if (!out.entities.empty() && entity.id <= out.entities.back().id) {
        reader.fail("expected.entities must be ascending by id");
        return false;
      }
      out.entities.push_back(entity);
      reader.skipWhitespace();
      if (reader.cursor < reader.end && *reader.cursor == ',') {
        ++reader.cursor;
        continue;
      }
      break;
    }
  }
  if (!reader.literal(']')) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("events")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixtureEvent event;
      if (!readEvent(reader, event)) return false;
      out.events.push_back(event);
      reader.skipWhitespace();
      if (reader.cursor < reader.end && *reader.cursor == ',') {
        ++reader.cursor;
        continue;
      }
      break;
    }
  }
  if (!reader.literal(']')) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("rngState")) return false;
  if (!reader.literal('{')) return false;
  if (!reader.key("ai")) return false;
  if (!readUint32(reader, out.rng.ai, "rngState.ai")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("spawn")) return false;
  if (!readUint32(reader, out.rng.spawn, "rngState.spawn")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("fx")) return false;
  if (!readUint32(reader, out.rng.fx, "rngState.fx")) return false;
  if (!reader.literal('}')) return false;
  if (!reader.literal('}')) return false;
  return reader.literal('}');
}

bool readFixture(Reader& reader, Fixture& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("name")) return false;
  if (!reader.stringValue(out.name)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("seed")) return false;
  if (!readUint32(reader, out.seed, "seed")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("configHash")) return false;
  if (!reader.stringValue(out.configHash)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("ticks")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor == ']') {
    reader.fail("ticks must not be empty (S07 §5.4 禁止抽样跳过)");
    return false;
  }
  while (true) {
    FixtureTick tick;
    if (!readTick(reader, tick)) return false;
    out.ticks.push_back(std::move(tick));
    reader.skipWhitespace();
    if (reader.cursor < reader.end && *reader.cursor == ',') {
      ++reader.cursor;
      continue;
    }
    break;
  }
  if (!reader.literal(']')) return false;
  if (!reader.literal('}')) return false;
  return reader.finish();
}

std::string baseName(const std::string& path) {
  const std::size_t slash = path.find_last_of("/");
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1u);
  if (name.size() > 5u && name.compare(name.size() - 5u, 5u, ".json") == 0) name.resize(name.size() - 5u);
  return name;
}

}  // namespace

std::string fixtureDir() {
#ifdef AC_EVIDENCE_FIXTURE_DIR
  return std::string(AC_EVIDENCE_FIXTURE_DIR);
#else
  return std::string("docs/evidence/fixtures");
#endif
}

bool loadFixtureFile(const std::string& path, Fixture& out, std::string& error) {
  std::string content;
  {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) {
      error = "cannot open " + path;
      return false;
    }
    char buffer[4096];
    std::size_t read = 0u;
    while ((read = std::fread(buffer, 1u, sizeof buffer, file)) > 0u) content.append(buffer, read);
    std::fclose(file);
  }
  if (content.empty()) {
    error = "empty fixture " + path;
    return false;
  }
  Reader reader;
  reader.cursor = content.data();
  reader.end = content.data() + content.size();
  if (!readFixture(reader, out)) {
    error = path + ": " + reader.error;
    return false;
  }
  if (out.name != baseName(path)) {
    error = path + ": name " + out.name + " does not match the file name";
    return false;
  }
  return true;
}

bool loadFixtureByName(const std::string& name, Fixture& out, std::string& error) {
  return loadFixtureFile(fixtureDir() + "/" + name + ".json", out, error);
}

std::string configHashText() {
  using ac::sim::arena::kArenaHalfSizeMeters;
  using ac::sim::arena::kBarn;
  using ac::sim::arena::kFenceHeightMeters;
  using ac::sim::arena::kFenceThicknessMeters;
  using ac::sim::arena::kKindDimensions;
  using ac::sim::arena::kPlayerSpawns;
  using ac::sim::arena::kSpawnPoints;

  std::vector<double> spawns;
  for (const ac::Vec3& point : kPlayerSpawns) {
    spawns.push_back(point.x);
    spawns.push_back(point.z);
  }
  std::vector<double> enemySpawns;
  for (const ac::Vec3& point : kSpawnPoints) {
    enemySpawns.push_back(point.x);
    enemySpawns.push_back(point.z);
  }
  std::vector<double> limits{static_cast<double>(ac::kMaxEntities),
                             static_cast<double>(ac::config::kSeparatePasses),
                             ac::config::kSeparateZeroDistanceM};
  std::vector<double> radius;
  std::vector<double> height;
  for (const double value : ac::config::kKindRadiusM) radius.push_back(value);
  for (const ac::sim::arena::EntityDimensions& dimensions : kKindDimensions) height.push_back(dimensions.height);
  std::vector<double> baseStats;
  for (const ac::config::BaseStats& stats : ac::config::kKindBaseStats) {
    baseStats.push_back(static_cast<double>(stats.hp));
    baseStats.push_back(static_cast<double>(stats.armor));
  }
  std::vector<double> player{ac::config::kMoveSpeedMps,      ac::config::kSprintSpeedMps,
                             ac::config::kJumpSpeedMps,      ac::config::kGravityMps2,
                             static_cast<double>(ac::config::kMaxHp),
                             static_cast<double>(ac::config::kMaxArmor),
                             ac::config::kArmorAbsorbRatio,  ac::config::kPlayerRadiusM,
                             ac::config::kPlayerHeightM,     ac::config::kEyeHeightM};
  std::vector<double> input{ac::config::kMoveAxisLimit, ac::config::kPitchLimitRad,
                            static_cast<double>(ac::config::kButtonFire | ac::config::kButtonSprint |
                                                ac::config::kButtonJump | ac::config::kButtonReload |
                                                ac::config::kButtonInteract | ac::config::kButtonRage |
                                                ac::config::kButtonSwitchWeapon),
                            static_cast<double>(ac::config::kButtonFire),
                            static_cast<double>(ac::config::kButtonSprint),
                            static_cast<double>(ac::config::kButtonJump),
                            static_cast<double>(ac::config::kButtonReload),
                            static_cast<double>(ac::config::kButtonInteract),
                            static_cast<double>(ac::config::kButtonRage),
                            static_cast<double>(ac::config::kButtonSwitchWeapon)};

  std::string text;
  text += "arena.size=";
  text += join17({kArenaHalfSizeMeters, kFenceHeightMeters, kFenceThicknessMeters, kBarn.min.x, kBarn.max.x,
                  kBarn.min.y, kBarn.max.y, kBarn.min.z, kBarn.max.z});
  text.push_back(kLf);
  text += "arena.playerSpawns=" + join17(spawns);
  text.push_back(kLf);
  text += "arena.enemySpawns=" + join17(enemySpawns);
  text.push_back(kLf);
  text += "entity.limits=" + join17(limits);
  text.push_back(kLf);
  text += "entity.radiusByKind=" + join17(radius);
  text.push_back(kLf);
  text += "entity.heightByKind=" + join17(height);
  text.push_back(kLf);
  text += "entity.baseStats=" + join17(baseStats);
  text.push_back(kLf);
  text += "player=" + join17(player);
  text.push_back(kLf);
  text += "input=" + join17(input);
  text.push_back(kLf);

  // S08 §5：武器/散布/弹道/伤害/怒气/救援/命中盒（与 tools/export-fixtures.mjs 的 configHashText 逐组对应）
  std::vector<double> weapons;
  for (const ac::config::WeaponDef& def : ac::config::kWeapons) {
    weapons.push_back(static_cast<double>(def.damage));
    weapons.push_back(static_cast<double>(def.pellets));
    weapons.push_back(static_cast<double>(def.rpm));
    weapons.push_back(def.isAuto ? 1.0 : 0.0);
    weapons.push_back(static_cast<double>(def.mag));
    weapons.push_back(static_cast<double>(def.reloadMs));
    weapons.push_back(def.spreadDeg);
    weapons.push_back(def.falloffStartM);
    weapons.push_back(def.falloffPerM);
    weapons.push_back(def.headshotMultiplier);
  }
  text += "weapons=" + join17(weapons);
  text.push_back(kLf);
  text += "weapon.rules=" + join17({static_cast<double>(ac::config::kReserveAmmoInitial),
                                     ac::config::kSpreadGrowthPerShotDeg, ac::config::kSpreadMaxDeg,
                                     static_cast<double>(ac::config::kSpreadDecayDelayMs),
                                     ac::config::kSpreadDecayPerSecondDeg,
                                     ac::config::kRecoilPitchPerShotDeg, ac::config::kRecoilYawJitterDeg});
  text.push_back(kLf);
  text += "shot=" + join17({ac::config::kShotMaxDistanceM, ac::config::kDegToRad,
                             ac::config::kAimPitchLimitRad,
                             static_cast<double>(ac::config::kPelletYawStride),
                             static_cast<double>(ac::config::kPelletPitchStride),
                             static_cast<double>(ac::config::kJitterYawSalt),
                             static_cast<double>(ac::config::kJitterPitchSalt)});
  text.push_back(kLf);
  text += "damage=" + join17({ac::config::kHeadMinHeightRatio, ac::config::kTorsoMinHeightRatio,
                               ac::config::kBodyPartMultiplierLimb, ac::config::kArmorAbsorbRatio,
                               static_cast<double>(ac::config::kMaxArmor),
                               static_cast<double>(ac::config::kMaxHp),
                               ac::config::kFalloffMinMultiplier,
                               ac::config::kFriendlyFire ? 1.0 : 0.0,
                               static_cast<double>(ac::config::kSheepEliteState)});
  text.push_back(kLf);
  text += "rage=" + join17({static_cast<double>(ac::config::kRageMax),
                             static_cast<double>(ac::config::kRagePerKill),
                             static_cast<double>(ac::config::kRagePerEliteKill),
                             static_cast<double>(ac::config::kRageHeadshotKillMultiplier),
                             static_cast<double>(ac::config::kRageIdleDecayDelayMs),
                             static_cast<double>(ac::config::kRageDecayPerSecond),
                             static_cast<double>(ac::config::kRageDurationMs),
                             ac::config::kRageDamageMultiplier, ac::config::kRageFireRateMultiplier,
                             ac::config::kRageMoveSpeedMultiplier});
  text.push_back(kLf);
  text += "revive=" + join17({ac::config::kReviveRangeM, static_cast<double>(ac::config::kReviveDurationMs),
                               static_cast<double>(ac::config::kReviveResetDelayMs),
                               ac::config::kRescueSpeedClampMps, ac::config::kRevivedHpRatio,
                               ac::config::kWaveReviveHpRatio, ac::config::kProgressEventStepRatio});
  text.push_back(kLf);
  std::vector<double> sheepHit;
  for (const ac::config::SheepHitProfile& profile : ac::config::kSheepHit) {
    sheepHit.push_back(profile.halfWidthM);
    sheepHit.push_back(profile.halfDepthM);
    sheepHit.push_back(profile.topM);
    sheepHit.push_back(profile.headHalfWidthM);
    sheepHit.push_back(profile.headMinYM);
    sheepHit.push_back(profile.headMaxYM);
    sheepHit.push_back(profile.headMinZM);
    sheepHit.push_back(profile.headMaxZM);
    sheepHit.push_back(profile.headMinM);
    sheepHit.push_back(profile.torsoMinM);
  }
  text += "sheepHit=" + join17(sheepHit);
  return text;
}

uint32_t configHash() {
  const std::string text = configHashText();
  return ac::crc32c(text.data(), text.size());
}

std::string hashHex(uint32_t hash) {
  char buffer[16];
  const int written = std::snprintf(buffer, sizeof buffer, "%08x", hash);
  return std::string(buffer, written > 0 ? static_cast<std::size_t>(written) : 0u);
}

std::string doubleHex(double value) {
  char buffer[32];
  const int written = std::snprintf(buffer, sizeof buffer, "0x%016llx",
                                    static_cast<unsigned long long>(std::bit_cast<uint64_t>(value)));
  return std::string(buffer, written > 0 ? static_cast<std::size_t>(written) : 0u);
}

uint8_t entityFlagsOf(const ac::sim::Entity& entity) noexcept {
  uint8_t flags = 0u;
  if (entity.idle) flags |= kFlagIdle;
  return flags;
}

const char* kindName(ac::sim::EntityKind kind) noexcept {
  switch (kind) {
    case ac::sim::EntityKind::kPlayer:
      return "player";
    case ac::sim::EntityKind::kSheep:
      return "sheep";
    case ac::sim::EntityKind::kProjectile:
      return "projectile";
    case ac::sim::EntityKind::kPickup:
      return "pickup";
    default:
      return "unknown";
  }
}

void DiffSink::doubleField(uint32_t tickIndex, const std::string& path, double expectedValue, double actualValue) {
  if (has) return;
  if (std::bit_cast<uint64_t>(expectedValue) == std::bit_cast<uint64_t>(actualValue)) return;
  has = true;
  tick = tickIndex;
  field = path;
  expected = doubleHex(expectedValue);
  actual = doubleHex(actualValue);
}

void DiffSink::intField(uint32_t tickIndex, const std::string& path, int64_t expectedValue, int64_t actualValue) {
  if (has) return;
  if (expectedValue == actualValue) return;
  has = true;
  tick = tickIndex;
  field = path;
  expected = std::to_string(expectedValue);
  actual = std::to_string(actualValue);
}

void DiffSink::stringField(uint32_t tickIndex, const std::string& path, const std::string& expectedValue,
                           const std::string& actualValue) {
  if (has) return;
  if (expectedValue == actualValue) return;
  has = true;
  tick = tickIndex;
  field = path;
  expected = expectedValue;
  actual = actualValue;
}

std::string DiffSink::report() const {
  if (!has) return std::string();
  return "DIFF " + fixture + " tick=" + std::to_string(tick) + " field=" + field + " expected=" + expected +
         " actual=" + actual;
}

}  // namespace ac::test
