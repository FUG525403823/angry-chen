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
#include "config/sheep.hpp"
#include "config/waves.hpp"
#include "config/weapons.hpp"
#include "core/hash.hpp"
#include "sim/arena.hpp"
#include "waves/director.hpp"

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
  if (!readUint16(reader, out.id, "script[].commands[].id")) return false;
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
  if (!readUint8(reader, out.buttons, "script[].commands[].buttons")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("switchTo")) return false;
  if (!readUint8(reader, out.switchTo, "script[].commands[].switchTo")) return false;
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
  // hp 按 double 读（v1 的护甲吸收会写出 96.8 这类值，见 fixture_io.hpp）。
  if (!reader.doubleValue(out.hp)) return false;
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
  if (!reader.key("x")) return false;
  if (!reader.doubleValue(out.x)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("y")) return false;
  if (!reader.doubleValue(out.y)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("z")) return false;
  if (!reader.doubleValue(out.z)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("value")) return false;
  // value 按 double 读：v1 的事件值有小数（护甲吸收后的伤害），整数读取器会造成假差异。
  if (!reader.doubleValue(out.value)) return false;
  // 可选键：带种类的模拟事件（`sheepKilled`）才有 `kind`（S03 §5.4 的羊种类枚举，S08）。它只能出现在
  // 事件对象末位，所以「后面还有逗号」≡「就是 kind」；其余键仍按「固定 schema」直接失败。
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor == ',') {
    ++reader.cursor;
    if (!reader.key("kind")) return false;
    if (!readUint8(reader, out.kind, "events[].kind")) return false;
  }
  return reader.literal('}');
}

// 关键帧：tick + entities + events + rngState。实体顺序 = v1 world.activeIds 的插入顺序（可能有空洞、
// 也可能不是升序：id 复用后新实体排在末尾），所以两侧都不许对它排序或要求升序。
bool readKeyframe(Reader& reader, FixtureKeyframe& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("tick")) return false;
  if (!readUint32(reader, out.tick, "keyframes[].tick")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("entities")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixtureEntity entity;
      if (!readEntity(reader, entity)) return false;
      out.entities.push_back(std::move(entity));
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
      out.events.push_back(std::move(event));
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
  return reader.literal('}');
}

bool readSetup(Reader& reader, FixtureSetup& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("players")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixturePlayerSetup player;
      if (!reader.literal('{')) return false;
      if (!reader.key("id")) return false;
      if (!readUint16(reader, player.id, "setup.players[].id")) return false;
      if (!reader.literal(',')) return false;
      if (!reader.key("hp")) return false;
      if (!reader.doubleValue(player.hp)) return false;
      if (!reader.literal(',')) return false;
      if (!reader.key("armor")) return false;
      if (!reader.doubleValue(player.armor)) return false;
      if (!reader.literal('}')) return false;
      out.players.push_back(player);
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
  if (!reader.key("sheep")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixtureSheepSetup sheep;
      if (!reader.literal('{')) return false;
      if (!reader.key("kind")) return false;
      if (!reader.stringValue(sheep.kind)) return false;
      if (!reader.literal(',')) return false;
      if (!reader.key("x")) return false;
      if (!reader.doubleValue(sheep.x)) return false;
      if (!reader.literal(',')) return false;
      if (!reader.key("z")) return false;
      if (!reader.doubleValue(sheep.z)) return false;
      if (!reader.literal('}')) return false;
      out.sheep.push_back(std::move(sheep));
      reader.skipWhitespace();
      if (reader.cursor < reader.end && *reader.cursor == ',') {
        ++reader.cursor;
        continue;
      }
      break;
    }
  }
  if (!reader.literal(']')) return false;
  return reader.literal('}');
}

// 命令脚本的一段：{ from, to, commands[] }。按 tick 折叠，段内每条命令逐字段相同。
bool readRun(Reader& reader, FixtureRun& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("from")) return false;
  if (!readUint32(reader, out.from, "script[].from")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("to")) return false;
  if (!readUint32(reader, out.to, "script[].to")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("commands")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixtureCommand command;
      if (!readCommand(reader, command)) return false;
      if (!out.commands.empty() && command.id <= out.commands.back().id) {
        reader.fail("script[].commands must be ascending by id");
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
  if (!reader.literal('}')) return false;
  if (out.from == 0u || out.to < out.from) {
    reader.fail("script[] run must satisfy 1 <= from <= to");
    return false;
  }
  return true;
}

bool readHashHex(Reader& reader, uint64_t& out, const char* what) {
  std::string text;
  if (!reader.stringValue(text)) return false;
  if (text.size() != 16u) {
    reader.fail(std::string(what) + ": expected 16 hex chars");
    return false;
  }
  uint64_t value = 0u;
  for (const char c : text) {
    uint64_t digit = 0u;
    if (c >= '0' && c <= '9') {
      digit = static_cast<uint64_t>(c - '0');
    } else if (c >= 'a' && c <= 'f') {
      digit = static_cast<uint64_t>(c - 'a') + 10u;
    } else {
      reader.fail(std::string(what) + ": expected lowercase hex");
      return false;
    }
    value = (value << 4) | digit;
  }
  out = value;
  return true;
}

bool readFixture(Reader& reader, Fixture& out) {
  if (!reader.literal('{')) return false;
  if (!reader.key("name")) return false;
  if (!reader.stringValue(out.name)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("version")) return false;
  if (!readUint32(reader, out.version, "version")) return false;
  if (out.version != kFixtureSchemaVersion) {
    reader.fail("version must be 2（对拍向量口径，见 README §4）");
    return false;
  }
  if (!reader.literal(',')) return false;
  if (!reader.key("seed")) return false;
  if (!readUint32(reader, out.seed, "seed")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("dtMs")) return false;
  uint32_t dtMs = 0u;
  if (!readUint32(reader, dtMs, "dtMs")) return false;
  if (dtMs != kFixtureDtMs) {
    reader.fail("dtMs must be 50 (S06 §5.1 固定步长)");
    return false;
  }
  if (!reader.literal(',')) return false;
  if (!reader.key("configHash")) return false;
  if (!reader.stringValue(out.configHash)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("ticks")) return false;
  if (!readUint32(reader, out.ticks, "ticks")) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("setup")) return false;
  if (!readSetup(reader, out.setup)) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("director")) return false;
  if (!reader.literal('{')) return false;
  if (!reader.key("startWave")) return false;
  if (!readUint32(reader, out.startWave, "director.startWave")) return false;
  if (!reader.literal('}')) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("script")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor == ']') {
    reader.fail("script must not be empty (每 tick 的命令脚本是必需的)");
    return false;
  }
  while (true) {
    FixtureRun run;
    if (!readRun(reader, run)) return false;
    out.script.push_back(std::move(run));
    reader.skipWhitespace();
    if (reader.cursor < reader.end && *reader.cursor == ',') {
      ++reader.cursor;
      continue;
    }
    break;
  }
  if (!reader.literal(']')) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("keyframes")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor == ']') {
    reader.fail("keyframes must not be empty");
    return false;
  }
  while (true) {
    FixtureKeyframe frame;
    if (!readKeyframe(reader, frame)) return false;
    out.keyframes.push_back(std::move(frame));
    reader.skipWhitespace();
    if (reader.cursor < reader.end && *reader.cursor == ',') {
      ++reader.cursor;
      continue;
    }
    break;
  }
  if (!reader.literal(']')) return false;
  if (!reader.literal(',')) return false;
  if (!reader.key("snapshot")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor != ']') {
    while (true) {
      FixtureSnapshot entry;
      if (!reader.literal('{')) return false;
      if (!reader.key("tick")) return false;
      if (!readUint32(reader, entry.tick, "snapshot[].tick")) return false;
      if (!reader.literal(',')) return false;
      if (!reader.key("records")) return false;
      if (!readUint32(reader, entry.records, "snapshot[].records")) return false;
      if (!reader.literal(',')) return false;
      if (!reader.key("encodeHash")) return false;
      if (!readHashHex(reader, entry.encodeHash, "snapshot[].encodeHash")) return false;
      if (!reader.literal(',')) return false;
      if (!reader.key("decodeHash")) return false;
      if (!readHashHex(reader, entry.decodeHash, "snapshot[].decodeHash")) return false;
      if (!reader.literal('}')) return false;
      out.snapshot.push_back(entry);
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
  if (!reader.key("hashChain")) return false;
  if (!reader.literal('[')) return false;
  reader.skipWhitespace();
  if (reader.cursor < reader.end && *reader.cursor == ']') {
    reader.fail("hashChain must not be empty");
    return false;
  }
  while (true) {
    uint64_t value = 0u;
    if (!readHashHex(reader, value, "hashChain[]")) return false;
    out.hashChain.push_back(value);
    reader.skipWhitespace();
    if (reader.cursor < reader.end && *reader.cursor == ',') {
      ++reader.cursor;
      continue;
    }
    break;
  }
  if (!reader.literal(']')) return false;
  if (!reader.literal('}')) return false;
  if (out.hashChain.size() != out.ticks) {
    reader.fail("hashChain length must equal ticks");
    return false;
  }
  if (!out.scriptCoversAllTicks()) {
    reader.fail("script must cover every tick in 1..ticks without gaps");
    return false;
  }
  for (const FixtureKeyframe& frame : out.keyframes) {
    if (frame.tick == 0u || frame.tick > out.ticks) {
      reader.fail("keyframes[].tick out of range");
      return false;
    }
  }
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

bool Fixture::scriptCoversAllTicks() const {
  uint32_t expected = 1u;
  for (const FixtureRun& run : script) {
    if (run.from != expected) return false;
    expected = run.to + 1u;
  }
  return expected == ticks + 1u;
}

bool Fixture::commandAt(uint32_t tick, std::vector<FixtureCommand>& out) const {
  out.clear();
  if (tick == 0u || tick > ticks) return false;
  for (const FixtureRun& run : script) {
    if (tick < run.from || tick > run.to) continue;
    out = run.commands;
    // seq / clientTick 由 tick 派生（与导出侧同式），不落盘。
    for (FixtureCommand& command : out) {
      command.seq = static_cast<uint16_t>(tick % 65536u);
      command.clientTick = tick;
    }
    return true;
  }
  return false;
}

uint64_t fnv1a64(const void* data, std::size_t length, uint64_t seed) noexcept {
  const auto* bytes = static_cast<const unsigned char*>(data);
  uint64_t hash = seed;
  for (std::size_t i = 0; i < length; ++i) {
    hash ^= static_cast<uint64_t>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

uint64_t fnv1a64Text(const std::string& text, uint64_t seed) noexcept {
  return fnv1a64(text.data(), text.size(), seed);
}

std::string hash64Hex(uint64_t value) {
  char buffer[32];
  const int written = std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(value));
  return std::string(buffer, written > 0 ? static_cast<std::size_t>(written) : 0u);
}

bool selfTestFnv() {
  // 公开的 FNV-1a 64 测试向量：钉住算法本身（与导出侧 assertFnvSelfTest 同一组），防实现漂移。
  if (fnv1a64Text("", kFnvOffsetBasis) != kFnvOffsetBasis) return false;
  if (hash64Hex(fnv1a64Text("a", kFnvOffsetBasis)) != "af63dc4c8601ec8c") return false;
  if (fnv1a64Text("foobar", kFnvOffsetBasis) != 0x85944171f73967e8ull) return false;
  return true;
}

std::string projectionText(uint32_t tick, const std::vector<FixtureCommand>& commands,
                           const std::vector<FixtureEntity>& entities,
                           const std::vector<FixtureEvent>& events, const FixtureRng& rng) {
  std::string text;
  text += "tick=" + std::to_string(tick) + "\n";
  text += "dtMs=" + std::to_string(kFixtureDtMs) + "\n";
  for (const FixtureCommand& command : commands) {
    text += "cmd=" + std::to_string(command.id) + "," + std::to_string(command.seq) + "," +
            std::to_string(command.clientTick) + "," + g17(command.moveX) + "," + g17(command.moveY) + "," +
            g17(command.yaw) + "," + g17(command.pitch) + "," + std::to_string(command.buttons) + "," +
            std::to_string(command.switchTo) + "\n";
  }
  for (const FixtureEntity& entity : entities) {
    text += "ent=" + std::to_string(entity.id) + "," + entity.kind + "," + g17(entity.posX) + "," + g17(entity.posY) +
            "," + g17(entity.posZ) + "," + g17(entity.yaw) + "," + g17(entity.pitch) + "," + g17(entity.hp) + "," +
            std::to_string(entity.flags) + "\n";
  }
  for (const FixtureEvent& event : events) {
    text += "evt=" + std::to_string(event.tick) + "," + event.type + "," + std::to_string(event.flags) + "," +
            std::to_string(event.subjectId) + "," + std::to_string(event.targetId) + "," + g17(event.x) + "," +
            g17(event.y) + "," + g17(event.z) + "," + g17(event.value) +
            (eventCarriesKind(event.type) ? "," + std::to_string(event.kind) : "") + "\n";
  }
  text += "rng=" + std::to_string(rng.ai) + "," + std::to_string(rng.spawn) + "," + std::to_string(rng.fx) + "\n";
  return text;
}

std::vector<SnapshotRecordFields> decodeSnapshotRecords(const uint8_t* block, uint32_t count) {
  std::vector<SnapshotRecordFields> out;
  out.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* record = block + static_cast<std::size_t>(i) * kSnapshotRecordBytes;
    SnapshotRecordFields fields;
    fields.id = static_cast<uint16_t>(record[0] | (static_cast<uint16_t>(record[1]) << 8));
    fields.kind = static_cast<uint8_t>(record[2] & 0x03u);
    fields.flags = static_cast<uint8_t>((record[2] >> 2) & 0x3Fu);
    fields.xCm = static_cast<int16_t>(record[3] | (static_cast<uint16_t>(record[4]) << 8));
    fields.yCm = static_cast<int16_t>(record[5] | (static_cast<uint16_t>(record[6]) << 8));
    fields.zCm = static_cast<int16_t>(record[7] | (static_cast<uint16_t>(record[8]) << 8));
    fields.yawUnits = static_cast<uint16_t>(record[9] | (static_cast<uint16_t>(record[10]) << 8));
    fields.pitchUnits = static_cast<uint16_t>(record[11] | (static_cast<uint16_t>(record[12]) << 8));
    fields.hpRatioUnits = record[13];
    fields.state = record[14];
    out.push_back(fields);
  }
  return out;
}

std::string snapshotDecodeText(const std::vector<SnapshotRecordFields>& records) {
  std::string text;
  for (const SnapshotRecordFields& record : records) {
    text += "rec=" + std::to_string(record.id) + "," + std::to_string(record.kind) + "," +
            std::to_string(record.flags) + "," + std::to_string(record.xCm) + "," + std::to_string(record.yCm) + "," +
            std::to_string(record.zCm) + "," + std::to_string(record.yawUnits) + "," +
            std::to_string(record.pitchUnits) + "," + std::to_string(record.hpRatioUnits) + "," +
            std::to_string(record.state) + "\n";
  }
  return text;
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
                               ac::config::kReviveSpeedClampMps, ac::config::kRevivedHpRatio,
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
  text.push_back(kLf);

  // S09 §5.1-§5.4：羊形表 / SHEEP_AI / 状态转移表 / 局部常量 / 攻击档案 / 波次常量
  // （与 tools/export-fixtures.mjs 的 configHashText 逐组对应）
  std::vector<double> sheep;
  for (const ac::config::SheepDef& def : ac::config::kSheep) {
    sheep.push_back(def.hp);
    sheep.push_back(def.speed);
    sheep.push_back(def.damage);
    sheep.push_back(def.price);
    sheep.push_back(def.radiusM);
    sheep.push_back(def.heightM);
  }
  text += "sheep=" + join17(sheep);
  text.push_back(kLf);
  const ac::config::SheepAiParams& sheepAi = ac::config::kSheepAi;
  text += "sheep.ai=" +
          join17({sheepAi.sightM,
                  sheepAi.attackRangeM,
                  sheepAi.attackCooldownMs,
                  sheepAi.chargeWindupMs,
                  sheepAi.chargeSpeedMps,
                  sheepAi.eliteBoltRangeM,
                  sheepAi.eliteBoltCooldownMs,
                  sheepAi.eliteKeepMinM,
                  sheepAi.eliteKeepMaxM,
                  sheepAi.boltSpeedMps,
                  static_cast<double>(sheepAi.kingSummonCount),
                  sheepAi.kingSummonIntervalMs,
                  sheepAi.kingPhase3SpeedMultiplier,
                  sheepAi.kingPhase3CooldownMultiplier,
                  sheepAi.staggerMs,
                  sheepAi.chargeStaggerMs,
                  sheepAi.deadFadeMs,
                  sheepAi.knockbackVelocityMps,
                  sheepAi.neighborRadiusM,
                  static_cast<double>(sheepAi.maxNeighbors),
                  static_cast<double>(sheepAi.aggroSlots),
                  sheepAi.aggroDecayPerTick,
                  sheepAi.aggroPerHit,
                  sheepAi.targetSwitchRatio,
                  sheepAi.grazeRadiusM});
  text.push_back(kLf);
  std::vector<double> states{static_cast<double>(ac::config::kSheepStateCount),
                             static_cast<double>(ac::config::kSheepMaxTransitions)};
  for (const ac::config::SheepTransitionRow& row : ac::config::kSheepTransitions) {
    states.push_back(static_cast<double>(row.count));
    for (int slot = 0; slot < ac::config::kSheepMaxTransitions; ++slot) {
      states.push_back(static_cast<double>(row.to[slot]));
    }
  }
  text += "sheep.states=" + join17(states);
  text.push_back(kLf);
  // 局部常量：可导入的取 v1 导出；其余是 v1 的内联字面量（没有导出），按 文件:行 抄一份钉住。
  text += "sheep.local=" +
          join17({ac::config::kSheepAlertMs,
                  ac::config::kRamChargeTriggerM,
                  ac::config::kRamChargeMaxMs,
                  ac::config::kGrazeRepickMs,
                  ac::config::kEliteStrafeMs,
                  ac::config::kEliteStrafeSpeedRatio,
                  ac::config::kGrazeSpeedRatio,
                  ac::config::kGrazeObstacleRadiusRatio,
                  ac::config::kArriveSlowRadiusM,
                  ac::config::kFlockWeightSeparation,
                  ac::config::kFlockWeightAlignment,
                  ac::config::kFlockWeightCohesion,
                  ac::config::kFlockCohesionScale,
                  ac::config::kFlockBlendRatio,
                  ac::config::kBiteKnockbackM,
                  ac::config::kChargeKnockbackM,
                  ac::config::kQuestionBoltLifeMs,
                  ac::config::kQuestionBoltRadiusM,
                  ac::config::kQuestionBoltSpawnHeightM,
                  ac::config::kTargetEyeHeightM,
                  ac::config::kVictimChestHeightM,
                  ac::config::kKingSummonRadiusM,
                  ac::config::kKingSummonJitterM,
                  ac::config::kKingPhase1MinRatio,
                  ac::config::kKingPhase2MinRatio,
                  ac::config::kFieldEdgeLimitM});
  text.push_back(kLf);
  std::vector<double> sheepAttack;
  for (const ac::config::WeaponDef& profile : ac::config::kSheepAttackProfile) {
    sheepAttack.push_back(profile.damage);
    sheepAttack.push_back(static_cast<double>(profile.pellets));
    sheepAttack.push_back(static_cast<double>(profile.rpm));
    sheepAttack.push_back(profile.isAuto ? 1.0 : 0.0);
    sheepAttack.push_back(static_cast<double>(profile.mag));
    sheepAttack.push_back(static_cast<double>(profile.reloadMs));
    sheepAttack.push_back(profile.spreadDeg);
    sheepAttack.push_back(profile.falloffStartM);
    sheepAttack.push_back(profile.falloffPerM);
    sheepAttack.push_back(profile.headshotMultiplier);
  }
  text += "sheep.attack=" + join17(sheepAttack);
  text.push_back(kLf);
  std::vector<double> waveRows{static_cast<double>(ac::config::kWaveMax),
                               ac::config::kWaveIntermissionMs,
                               ac::config::kWaveIntermissionMinMs,
                               ac::config::kBudgetScalePerExtraPlayer,
                               ac::config::kSpeedScalePerExtraPlayer,
                               static_cast<double>(ac::config::kMaxSpawnsPerTick),
                               static_cast<double>(ac::config::kMaxActiveSpawnPoints),
                               ac::config::kMinSpawnDistanceM};
  for (int32_t wave = 1; wave <= ac::config::kWaveMax; ++wave) {
    waveRows.push_back(static_cast<double>(ac::config::waveBaseBudget(wave)));
  }
  for (int32_t players = 1; players <= 4; ++players) {
    waveRows.push_back(ac::config::sheepSpeedMultiplier(players));
  }
  text += "waves=" + join17(waveRows);
  text.push_back(kLf);
  text += "waves.scaling=" +
          join17({static_cast<double>(ac::config::waveBudget(1, 4)),
                  static_cast<double>(ac::config::waveBudget(5, 4)),
                  static_cast<double>(ac::config::firstWaveFor(ac::config::SheepKind::kRam)),
                  static_cast<double>(ac::config::firstWaveFor(ac::config::SheepKind::kElite)),
                  static_cast<double>(ac::config::firstWaveFor(ac::config::SheepKind::kKing)),
                  static_cast<double>(ac::config::firstWaveFor(ac::config::SheepKind::kGrunt)),
                  ac::config::isBossWave(5) ? 1.0 : 0.0,
                  ac::config::isBossWave(4) ? 1.0 : 0.0,
                  static_cast<double>(ac::waves::kDirectorKindCount)});
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

uint8_t entityFlagsOf(const ac::sim::Entity& entity, double nowMs) noexcept {
  // 与 v1 combat/resolve.ts + sim.ts 的 flagsOf 逐位同表（bit3 charging / bit4 fading 两侧都不产出）。
  uint8_t flags = 0u;
  if (entity.downed.downed) flags |= kFlagDowned;
  if (ac::combat::isRageActive(entity.rage, nowMs)) flags |= kFlagRageMode;
  if (ac::combat::isReloading(entity.weapon)) flags |= kFlagReloading;
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
