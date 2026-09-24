#pragma once
// S13 §3/§5：战绩存储（NDJSON 追加式 + 可替换的 sqlite 缝）。
//
// 一行一条记录，字段名即 JSON 键（§5 的 schema 表）；本文件是纯 I/O 层：不引 sim/net，
// 只有标准库。常驻上限与批量淘汰、排序一致性、坏行只计数（不抛异常）都是 §5 的冻结项。

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// §5「sqlite 缝」：默认关闭，启用需先改 ADR（S13 只留声明，不实现）。
#ifndef AC_WITH_SQLITE
#define AC_WITH_SQLITE 0
#endif

namespace ac::persist {

inline constexpr std::size_t kMaxRecords = 10000u;              // §5 常驻上限
inline constexpr std::size_t kEvictBatch = kMaxRecords / 100u;  // §5 批量淘汰 max(1, 10000/100) = 100
inline constexpr std::size_t kMaxPlayersPerMatch = 4u;          // §5 playerCount ∈ 1…4
inline constexpr std::size_t kMaxMatchIdLength = 64u;
inline constexpr std::size_t kMaxPlayerNameBytes = 64u;
inline constexpr std::string_view kStoreFileName = "matches.ndjson";
inline constexpr std::string_view kReportDirName = "reports";
inline constexpr bool kSqliteEnabled = (AC_WITH_SQLITE != 0);

struct PlayerResultRecord {
  std::string name;
  std::uint32_t kills = 0u;
  std::uint32_t headshots = 0u;
  std::uint32_t shotsFired = 0u;
  std::uint32_t hits = 0u;
  std::uint32_t revives = 0u;
  std::uint32_t downs = 0u;
  std::uint32_t aliveMs = 0u;
  bool leftMidMatch = false;
};

struct MatchResultRecord {
  std::string matchId;
  std::uint64_t startedAtMs = 0u;
  std::uint32_t durationMs = 0u;
  std::uint16_t waveReached = 0u;
  std::uint8_t winnerTeam = 0u;
  std::vector<PlayerResultRecord> players;
};

// matchId 的文件名安全字符集：§5 的 [A-Za-z0-9._-]，长度 1…64。
bool isSafeMatchId(std::string_view matchId) noexcept;
bool isValidMatchRecord(const MatchResultRecord& record) noexcept;

// 行编解码（与文件格式同源；报告与测试共用，避免第二套形状）。
std::string encodeMatchRecordLine(const MatchResultRecord& record);
bool decodeMatchRecordLine(std::string_view line, MatchResultRecord& out) noexcept;

enum class RecordOrder : std::uint8_t { kTop = 0u, kRecent = 1u };

// §5 排序一致性：top = 击杀降序 → startedAtMs 降序 → matchId 升序；recent = startedAtMs 降序 → matchId 升序。
std::uint64_t totalKills(const MatchResultRecord& record) noexcept;
bool isOrderedBefore(const MatchResultRecord& a, const MatchResultRecord& b, RecordOrder order) noexcept;

struct MatchStoreStats {
  std::size_t linesRead = 0u;     // 加载时读到的行数（含坏行）
  std::size_t retained = 0u;      // 常驻记录数（≤ kMaxRecords）
  std::size_t corruptLines = 0u;  // 解析失败或字段缺失 → ac_corrupt_lines_total
  std::size_t evicted = 0u;       // 批量淘汰累计条数
};

// 接口（§9 稳定接口）：append / listTop / listRecent / flush / version / recordCount。
class MatchStore {
 public:
  MatchStore() = default;
  MatchStore(const MatchStore&) = delete;
  MatchStore& operator=(const MatchStore&) = delete;
  virtual ~MatchStore() = default;

  virtual bool append(const MatchResultRecord& record) noexcept = 0;
  virtual std::vector<MatchResultRecord> listTop(std::size_t limit) const = 0;
  virtual std::vector<MatchResultRecord> listRecent(std::size_t limit) const = 0;
  virtual bool flush() noexcept = 0;
  virtual std::uint64_t version() const noexcept = 0;  // 追加或淘汰即自增（读缓存失效键）
  virtual std::size_t recordCount() const noexcept = 0;
  virtual const MatchStoreStats& stats() const noexcept = 0;
  virtual bool isReady() const noexcept = 0;
};

// <dataDir>/matches.ndjson：只追加、永不截断。open() 流式加载（逐行解析，坏行只计数），
// 常驻超过 kMaxRecords 即批量淘汰 kEvictBatch 条最旧记录（startedAtMs 升序，matchId 升序决胜）。
class NdjsonMatchStore final : public MatchStore {
 public:
  explicit NdjsonMatchStore(std::string dataDir);
  ~NdjsonMatchStore() override;

  NdjsonMatchStore(const NdjsonMatchStore&) = delete;
  NdjsonMatchStore& operator=(const NdjsonMatchStore&) = delete;

  // 失败原因写进 *error（可为 nullptr）；失败后 isReady() 为 false。
  bool open(std::string* error = nullptr) noexcept;

  bool append(const MatchResultRecord& record) noexcept override;
  std::vector<MatchResultRecord> listTop(std::size_t limit) const override;
  std::vector<MatchResultRecord> listRecent(std::size_t limit) const override;
  bool flush() noexcept override;
  std::uint64_t version() const noexcept override { return version_; }
  std::size_t recordCount() const noexcept override { return records_.size(); }
  const MatchStoreStats& stats() const noexcept override { return stats_; }
  bool isReady() const noexcept override { return isReady_; }

  const std::string& path() const noexcept { return path_; }
  const std::string& dataDir() const noexcept { return dataDir_; }

 private:
  std::vector<MatchResultRecord> list(std::size_t limit, RecordOrder order) const;
  void evictOverflow() noexcept;

  std::string dataDir_;
  std::string path_;
  std::vector<MatchResultRecord> records_;
  MatchStoreStats stats_{};
  std::uint64_t version_ = 0u;
  std::FILE* file_ = nullptr;
  bool isReady_ = false;
};

// §5「sqlite 缝」：同一 MatchStore 接口的第二实现，只留声明（AC_WITH_SQLITE 默认 0）。
// 声明存在即契约：启用时要新增真实实现并把 AC_WITH_SQLITE 置 1（需先改 ADR）。
class SqliteMatchStore final : public MatchStore {
 public:
  static std::unique_ptr<MatchStore> open(const std::string& path);
};

// 工厂（§9）：构造 + open；失败返回 nullptr 并把原因写进 *error（可为 nullptr）。
std::unique_ptr<MatchStore> openMatchStore(std::string dataDir, std::string* error = nullptr);

}  // namespace ac::persist
