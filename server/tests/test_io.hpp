#pragma once
// S13 四个新测试文件共用的最小工具：读文件、数行/数文件、构造 §5 战绩记录。
// （临时目录在 tmp_workdir.hpp，日志 sink 复位在 log_guard.hpp。）
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#include "persist/match_store.hpp"

namespace ac::test {

// 读整个文件；打开失败返回空串（用例自己断言长度与内容，不静默兜底）。
inline std::string readTextFile(const std::string& path) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return std::string();
  std::string out;
  char buffer[512];
  std::size_t read = 0u;
  while ((read = std::fread(buffer, 1u, sizeof(buffer), file)) > 0u) out.append(buffer, read);
  std::fclose(file);
  return out;
}

inline std::size_t countNewlines(const std::string& text) {
  std::size_t count = 0u;
  for (const char ch : text) {
    if (ch == '\n') ++count;
  }
  return count;
}

inline std::size_t countFiles(const std::string& directory) {
  std::error_code code;
  std::size_t count = 0u;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, code)) {
    std::error_code entryCode;
    if (entry.is_regular_file(entryCode)) ++count;
  }
  return count;
}

// §5 战绩记录的最小合法样本：字段齐全、可按需改（默认 1 人、指定击杀数）。
inline ac::persist::MatchResultRecord makeMatchRecord(const char* id, std::uint64_t startedAtMs,
                                                      std::uint32_t kills,
                                                      std::size_t playerCount = 1u) {
  ac::persist::MatchResultRecord record;
  record.matchId = id;
  record.startedAtMs = startedAtMs;
  record.durationMs = 60000u;
  record.waveReached = 5u;
  record.winnerTeam = 0u;
  record.players.resize(playerCount);
  for (std::size_t i = 0u; i < playerCount; ++i) {
    record.players[i].name = std::string("p") + std::to_string(i);
    record.players[i].kills = kills;
    record.players[i].aliveMs = 1000u;
  }
  return record;
}

// 环境变量：Windows 用 _putenv_s，其余平台 setenv/unsetenv（空值 = 删除）。
inline bool setEnvVar(const char* name, const std::string& value) {
#ifdef _WIN32
  return _putenv_s(name, value.c_str()) == 0;
#else
  return setenv(name, value.c_str(), 1) == 0;
#endif
}

inline bool clearEnvVar(const char* name) {
#ifdef _WIN32
  return _putenv_s(name, "") == 0;
#else
  return unsetenv(name) == 0;
#endif
}

inline bool setDataDirEnv(const std::string& value) { return setEnvVar("AC_DATA_DIR", value); }

inline bool clearDataDirEnv() { return clearEnvVar("AC_DATA_DIR"); }

}  // namespace ac::test
