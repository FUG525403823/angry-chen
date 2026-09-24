#include "persist/match_store.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

namespace ac::persist {

// §5「sqlite 缝」：默认关闭；启用前必须改 ADR 并实现 SqliteMatchStore。
static_assert(!kSqliteEnabled, "S13 5: AC_WITH_SQLITE default 0; enabling needs an ADR first");

namespace {

// ---------- JSON 文本 ----------
// 只覆盖 §5 的记录形状（扁平对象 + players 数组）。转义口径与 core/log.cpp 相同
// （\" 、\\ 、控制字符 \u00XX 大写十六进制），但不复用日志的 formatLine：那条路会把字段
// 提到 ts/level/evt 之后的位置，是「日志行」而不是「嵌套数组的记录行」。
void appendEscaped(std::string& out, std::string_view raw) {
  out.push_back('"');
  for (const char ch : raw) {
    const auto value = static_cast<unsigned char>(ch);
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (value < 0x20u) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned>(value));
          out += buffer;
        } else {
          out.push_back(ch);
        }
        break;
    }
  }
  out.push_back('"');
}

void appendU64(std::string& out, std::uint64_t value) {
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
  out += buffer;
}

void appendUtf8(std::string& out, std::uint32_t code) {
  if (code <= 0x7Fu) {
    out.push_back(static_cast<char>(code));
  } else if (code <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (code >> 6)));
    out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
  } else if (code <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (code >> 12)));
    out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (code >> 18)));
    out.push_back(static_cast<char>(0x80u | ((code >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((code >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (code & 0x3Fu)));
  }
}

// ---------- 极小 JSON 读取器（严格；任何意外都返回 false = 坏行） ----------
struct Reader {
  std::string_view text;
  std::size_t pos = 0u;

  bool isAtEnd() const noexcept { return pos >= text.size(); }

  void skipSpace() noexcept {
    while (pos < text.size()) {
      const char ch = text[pos];
      if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
        ++pos;
        continue;
      }
      break;
    }
  }

  bool eat(char expected) noexcept {
    if (pos < text.size() && text[pos] == expected) {
      ++pos;
      return true;
    }
    return false;
  }

  bool has(std::string_view literal) const noexcept {
    return text.size() - pos >= literal.size() && text.compare(pos, literal.size(), literal) == 0;
  }

  bool parseHex4(std::uint32_t& out) noexcept {
    if (text.size() - pos < 4u) return false;
    std::uint32_t value = 0u;
    for (std::size_t i = 0u; i < 4u; ++i) {
      const char ch = text[pos + i];
      std::uint32_t digit = 0u;
      if (ch >= '0' && ch <= '9') {
        digit = static_cast<std::uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        digit = static_cast<std::uint32_t>(ch - 'a') + 10u;
      } else if (ch >= 'A' && ch <= 'F') {
        digit = static_cast<std::uint32_t>(ch - 'A') + 10u;
      } else {
        return false;
      }
      value = (value << 4u) | digit;
    }
    pos += 4u;
    out = value;
    return true;
  }

  bool parseString(std::string& out) noexcept {
    out.clear();
    if (!eat('"')) return false;
    while (pos < text.size()) {
      const char ch = text[pos++];
      if (ch == '"') return true;
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }
      if (pos >= text.size()) return false;
      const char esc = text[pos++];
      switch (esc) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          std::uint32_t code = 0u;
          if (!parseHex4(code)) return false;
          if (code >= 0xD800u && code <= 0xDBFFu) {
            if (!eat('\\') || !eat('u')) return false;
            std::uint32_t low = 0u;
            if (!parseHex4(low) || low < 0xDC00u || low > 0xDFFFu) return false;
            code = 0x10000u + ((code - 0xD800u) << 10u) + (low - 0xDC00u);
          } else if (code >= 0xDC00u && code <= 0xDFFFu) {
            return false;  // 孤立低位代理
          }
          appendUtf8(out, code);
          break;
        }
        default: return false;
      }
    }
    return false;
  }

  bool parseU64(std::uint64_t& out) noexcept {
    const std::size_t start = pos;
    std::uint64_t value = 0u;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
      value = value * 10u + static_cast<std::uint64_t>(text[pos] - '0');
      ++pos;
    }
    if (pos == start) return false;
    out = value;
    return true;
  }

  bool parseBool(bool& out) noexcept {
    if (has("true")) {
      pos += 4u;
      out = true;
      return true;
    }
    if (has("false")) {
      pos += 5u;
      out = false;
      return true;
    }
    return false;
  }

  // 未知键：跳过任意一个 JSON 值（前向兼容；已知键缺一即坏行）。
  bool skipValue() noexcept {
    skipSpace();
    if (isAtEnd()) return false;
    const char ch = text[pos];
    if (ch == '"') {
      std::string scratch;
      return parseString(scratch);
    }
    if (ch == '-' || (ch >= '0' && ch <= '9')) {
      if (ch == '-') ++pos;
      const std::size_t digits = pos;
      while (pos < text.size()) {
        const char digit = text[pos];
        const bool isNumber = (digit >= '0' && digit <= '9') || digit == '.' || digit == 'e' ||
                              digit == 'E' || digit == '+' || digit == '-';
        if (!isNumber) break;
        ++pos;
      }
      return pos > digits;
    }
    if (has("true")) { pos += 4u; return true; }
    if (has("false")) { pos += 5u; return true; }
    if (has("null")) { pos += 4u; return true; }
    if (ch == '[') {
      ++pos;
      skipSpace();
      if (eat(']')) return true;
      while (true) {
        if (!skipValue()) return false;
        skipSpace();
        if (eat(',')) {
          skipSpace();
          continue;
        }
        return eat(']');
      }
    }
    if (ch == '{') {
      ++pos;
      skipSpace();
      if (eat('}')) return true;
      while (true) {
        skipSpace();
        std::string key;
        if (!parseString(key)) return false;
        skipSpace();
        if (!eat(':')) return false;
        if (!skipValue()) return false;
        skipSpace();
        if (eat(',')) continue;
        return eat('}');
      }
    }
    return false;
  }
};

bool parsePlayer(Reader& reader, PlayerResultRecord& out) noexcept {
  out = PlayerResultRecord{};
  bool hasName = false;
  bool hasKills = false;
  bool hasHeadshots = false;
  bool hasShots = false;
  bool hasHits = false;
  bool hasRevives = false;
  bool hasDowns = false;
  bool hasAlive = false;
  bool hasLeft = false;
  reader.skipSpace();
  if (!reader.eat('{')) return false;
  while (true) {
    reader.skipSpace();
    if (reader.eat('}')) break;
    std::string key;
    if (!reader.parseString(key)) return false;
    reader.skipSpace();
    if (!reader.eat(':')) return false;
    reader.skipSpace();
    std::uint64_t number = 0u;
    if (key == "name") {
      if (!reader.parseString(out.name)) return false;
      hasName = true;
    } else if (key == "kills") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.kills = static_cast<std::uint32_t>(number);
      hasKills = true;
    } else if (key == "headshots") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.headshots = static_cast<std::uint32_t>(number);
      hasHeadshots = true;
    } else if (key == "shotsFired") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.shotsFired = static_cast<std::uint32_t>(number);
      hasShots = true;
    } else if (key == "hits") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.hits = static_cast<std::uint32_t>(number);
      hasHits = true;
    } else if (key == "revives") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.revives = static_cast<std::uint32_t>(number);
      hasRevives = true;
    } else if (key == "downs") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.downs = static_cast<std::uint32_t>(number);
      hasDowns = true;
    } else if (key == "aliveMs") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.aliveMs = static_cast<std::uint32_t>(number);
      hasAlive = true;
    } else if (key == "leftMidMatch") {
      if (!reader.parseBool(out.leftMidMatch)) return false;
      hasLeft = true;
    } else if (!reader.skipValue()) {
      return false;
    }
    reader.skipSpace();
    if (reader.eat(',')) continue;
    if (reader.eat('}')) break;
    return false;
  }
  return hasName && hasKills && hasHeadshots && hasShots && hasHits && hasRevives && hasDowns &&
         hasAlive && hasLeft;
}

bool parsePlayers(Reader& reader, std::vector<PlayerResultRecord>& out) noexcept {
  out.clear();
  if (!reader.eat('[')) return false;
  reader.skipSpace();
  if (reader.eat(']')) return false;  // §5 playerCount ∈ 1…4
  while (true) {
    PlayerResultRecord player;
    if (!parsePlayer(reader, player)) return false;
    if (out.size() >= kMaxPlayersPerMatch) return false;
    out.push_back(std::move(player));
    reader.skipSpace();
    if (reader.eat(',')) {
      reader.skipSpace();
      continue;
    }
    return reader.eat(']');
  }
}

bool parseRecord(Reader& reader, MatchResultRecord& out) noexcept {
  out = MatchResultRecord{};
  bool hasMatchId = false;
  bool hasStarted = false;
  bool hasDuration = false;
  bool hasWave = false;
  bool hasWinner = false;
  bool hasCount = false;
  bool hasPlayers = false;
  std::uint64_t playerCount = 0u;
  reader.skipSpace();
  if (!reader.eat('{')) return false;
  while (true) {
    reader.skipSpace();
    if (reader.eat('}')) break;
    std::string key;
    if (!reader.parseString(key)) return false;
    reader.skipSpace();
    if (!reader.eat(':')) return false;
    reader.skipSpace();
    std::uint64_t number = 0u;
    if (key == "matchId") {
      if (!reader.parseString(out.matchId)) return false;
      hasMatchId = true;
    } else if (key == "startedAtMs") {
      if (!reader.parseU64(out.startedAtMs)) return false;
      hasStarted = true;
    } else if (key == "durationMs") {
      if (!reader.parseU64(number) || number > 0xFFFFFFFFu) return false;
      out.durationMs = static_cast<std::uint32_t>(number);
      hasDuration = true;
    } else if (key == "waveReached") {
      if (!reader.parseU64(number) || number > 0xFFFFu) return false;
      out.waveReached = static_cast<std::uint16_t>(number);
      hasWave = true;
    } else if (key == "winnerTeam") {
      if (!reader.parseU64(number) || number > 0xFFu) return false;
      out.winnerTeam = static_cast<std::uint8_t>(number);
      hasWinner = true;
    } else if (key == "playerCount") {
      if (!reader.parseU64(number)) return false;
      playerCount = number;
      hasCount = true;
    } else if (key == "players") {
      if (!parsePlayers(reader, out.players)) return false;
      hasPlayers = true;
    } else if (!reader.skipValue()) {
      return false;
    }
    reader.skipSpace();
    if (reader.eat(',')) continue;
    if (reader.eat('}')) break;
    return false;
  }
  if (!(hasMatchId && hasStarted && hasDuration && hasWave && hasWinner && hasCount && hasPlayers)) {
    return false;
  }
  if (playerCount != static_cast<std::uint64_t>(out.players.size())) return false;
  return isValidMatchRecord(out);
}

bool isOldestFirst(const MatchResultRecord& a, const MatchResultRecord& b) noexcept {
  if (a.startedAtMs != b.startedAtMs) return a.startedAtMs < b.startedAtMs;
  return a.matchId < b.matchId;
}

}  // namespace

bool isSafeMatchId(std::string_view matchId) noexcept {
  if (matchId.empty() || matchId.size() > kMaxMatchIdLength) return false;
  for (const char ch : matchId) {
    const bool isDigit = ch >= '0' && ch <= '9';
    const bool isLower = ch >= 'a' && ch <= 'z';
    const bool isUpper = ch >= 'A' && ch <= 'Z';
    if (!(isDigit || isLower || isUpper || ch == '.' || ch == '_' || ch == '-')) return false;
  }
  return true;
}

bool isValidMatchRecord(const MatchResultRecord& record) noexcept {
  if (!isSafeMatchId(record.matchId)) return false;
  if (record.players.empty() || record.players.size() > kMaxPlayersPerMatch) return false;
  if (record.winnerTeam > 1u) return false;
  for (const PlayerResultRecord& player : record.players) {
    if (player.name.empty() || player.name.size() > kMaxPlayerNameBytes) return false;
  }
  return true;
}

std::uint64_t totalKills(const MatchResultRecord& record) noexcept {
  std::uint64_t total = 0u;
  for (const PlayerResultRecord& player : record.players) total += player.kills;
  return total;
}

bool isOrderedBefore(const MatchResultRecord& a, const MatchResultRecord& b, RecordOrder order) noexcept {
  if (order == RecordOrder::kTop) {
    const std::uint64_t killsA = totalKills(a);
    const std::uint64_t killsB = totalKills(b);
    if (killsA != killsB) return killsA > killsB;
  }
  if (a.startedAtMs != b.startedAtMs) return a.startedAtMs > b.startedAtMs;
  return a.matchId < b.matchId;
}

std::string encodeMatchRecordLine(const MatchResultRecord& record) {
  std::string out = "{\"matchId\":";
  appendEscaped(out, record.matchId);
  out += ",\"startedAtMs\":";
  appendU64(out, record.startedAtMs);
  out += ",\"durationMs\":";
  appendU64(out, record.durationMs);
  out += ",\"waveReached\":";
  appendU64(out, record.waveReached);
  out += ",\"winnerTeam\":";
  appendU64(out, record.winnerTeam);
  out += ",\"playerCount\":";
  appendU64(out, record.players.size());
  out += ",\"players\":[";
  for (std::size_t i = 0u; i < record.players.size(); ++i) {
    const PlayerResultRecord& player = record.players[i];
    if (i != 0u) out.push_back(',');
    out += "{\"name\":";
    appendEscaped(out, player.name);
    out += ",\"kills\":";
    appendU64(out, player.kills);
    out += ",\"headshots\":";
    appendU64(out, player.headshots);
    out += ",\"shotsFired\":";
    appendU64(out, player.shotsFired);
    out += ",\"hits\":";
    appendU64(out, player.hits);
    out += ",\"revives\":";
    appendU64(out, player.revives);
    out += ",\"downs\":";
    appendU64(out, player.downs);
    out += ",\"aliveMs\":";
    appendU64(out, player.aliveMs);
    out += ",\"leftMidMatch\":";
    out += player.leftMidMatch ? "true" : "false";
    out.push_back('}');
  }
  out += "]}\n";
  return out;
}

bool decodeMatchRecordLine(std::string_view line, MatchResultRecord& out) noexcept {
  Reader reader{line, 0u};
  reader.skipSpace();
  if (reader.isAtEnd()) return false;
  if (!parseRecord(reader, out)) return false;
  reader.skipSpace();
  return reader.isAtEnd();
}

NdjsonMatchStore::NdjsonMatchStore(std::string dataDir) : dataDir_(std::move(dataDir)) {
  path_ = dataDir_;
  if (!path_.empty() && path_.back() != '/' && path_.back() != '\\') path_.push_back('/');
  path_ += std::string(kStoreFileName);
}

NdjsonMatchStore::~NdjsonMatchStore() {
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
}

bool NdjsonMatchStore::open(std::string* error) noexcept {
  records_.clear();
  stats_ = MatchStoreStats{};
  version_ = 0u;
  isReady_ = false;
  if (file_ != nullptr) {
    std::fclose(file_);
    file_ = nullptr;
  }
  if (!dataDir_.empty()) {
    std::error_code ignored;
    std::filesystem::create_directories(dataDir_, ignored);
  }
  std::ifstream in(path_, std::ios::binary);
  if (in.is_open()) {
    std::string line;
    while (std::getline(in, line)) {
      ++stats_.linesRead;
      MatchResultRecord record;
      if (!decodeMatchRecordLine(line, record)) {
        ++stats_.corruptLines;
        continue;
      }
      records_.push_back(std::move(record));
      evictOverflow();
    }
    in.close();
  }
  file_ = std::fopen(path_.c_str(), "ab");
  if (file_ == nullptr) {
    if (error != nullptr) *error = "cannot-open:" + path_;
    stats_.retained = records_.size();
    return false;
  }
  stats_.retained = records_.size();
  isReady_ = true;
  return true;
}

void NdjsonMatchStore::evictOverflow() noexcept {
  if (records_.size() <= kMaxRecords) return;
  const std::size_t batch = kEvictBatch == 0u ? 1u : kEvictBatch;
  const std::size_t remove = records_.size() - kMaxRecords;
  const std::size_t count = remove > batch ? remove : batch;
  const std::size_t bounded = count < records_.size() ? count : records_.size();
  std::nth_element(records_.begin(), records_.begin() + static_cast<std::ptrdiff_t>(bounded),
                   records_.end(), isOldestFirst);
  records_.erase(records_.begin(), records_.begin() + static_cast<std::ptrdiff_t>(bounded));
  stats_.evicted += bounded;
  ++version_;
  stats_.retained = records_.size();
}

bool NdjsonMatchStore::append(const MatchResultRecord& record) noexcept {
  if (!isReady_ || file_ == nullptr) return false;
  if (!isValidMatchRecord(record)) return false;
  const std::string line = encodeMatchRecordLine(record);
  if (std::fwrite(line.data(), 1u, line.size(), file_) != line.size()) return false;
  if (std::fflush(file_) != 0) return false;
  records_.push_back(record);
  ++version_;
  evictOverflow();
  stats_.retained = records_.size();
  return true;
}

std::vector<MatchResultRecord> NdjsonMatchStore::list(std::size_t limit, RecordOrder order) const {
  std::vector<MatchResultRecord> out = records_;
  std::sort(out.begin(), out.end(),
            [order](const MatchResultRecord& a, const MatchResultRecord& b) {
              return isOrderedBefore(a, b, order);
            });
  if (out.size() > limit) out.resize(limit);
  return out;
}

std::vector<MatchResultRecord> NdjsonMatchStore::listTop(std::size_t limit) const {
  return list(limit, RecordOrder::kTop);
}

std::vector<MatchResultRecord> NdjsonMatchStore::listRecent(std::size_t limit) const {
  return list(limit, RecordOrder::kRecent);
}

bool NdjsonMatchStore::flush() noexcept {
  if (file_ == nullptr) return false;
  return std::fflush(file_) == 0;
}

std::unique_ptr<MatchStore> openMatchStore(std::string dataDir, std::string* error) {
  auto store = std::make_unique<NdjsonMatchStore>(std::move(dataDir));
  if (!store->open(error)) return nullptr;
  return store;
}

}  // namespace ac::persist
