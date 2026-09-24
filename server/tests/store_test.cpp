// S13 §5 的战绩存储契约：NDJSON 追加与重载、坏行只计数、常驻上限与批量淘汰、排序一致性、
// 行编解码五类字段、schema 校验、sqlite 缝、AC_DATA_DIR、以及 10 万行夹具下的常驻条数。
#include "tiny_test.hpp"
#include "test_io.hpp"
#include "tmp_workdir.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "persist/match_store.hpp"
#include "room/stats.hpp"

namespace {

namespace fs = std::filesystem;

void writeFixture(const std::string& path, std::size_t lines) {
  std::error_code code;
  fs::create_directories(fs::path(path).parent_path(), code);
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) return;
  for (std::size_t i = 0u; i < lines; ++i) {
    char id[64];
    std::snprintf(id, sizeof(id), "m-%06zu", i);
    const ac::persist::MatchResultRecord record =
        ac::test::makeMatchRecord(id, 1700000000000ull + i, static_cast<std::uint32_t>(i % 7u));
    const std::string line = ac::persist::encodeMatchRecordLine(record);
    std::fwrite(line.data(), 1u, line.size(), file);
  }
  std::fclose(file);
}

std::vector<std::string> idsOf(const std::vector<ac::persist::MatchResultRecord>& records) {
  std::vector<std::string> ids;
  ids.reserve(records.size());
  for (const ac::persist::MatchResultRecord& record : records) ids.push_back(record.matchId);
  return ids;
}

}  // namespace

AC_TEST(store_appends_and_reloads_records) {
  ac::test::TempDir dir("store");
  AC_CHECK(dir.isReady());
  const std::string dataDir = dir.file("data");
  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dataDir, &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  AC_CHECK(store->isReady());
  AC_CHECK_EQ(store->recordCount(), static_cast<std::size_t>(0));
  AC_CHECK(ac::persist::kStoreFileName == std::string_view("matches.ndjson"));

  AC_CHECK(store->append(ac::test::makeMatchRecord("m-003", 3000u, 1u)));
  AC_CHECK(store->append(ac::test::makeMatchRecord("m-001", 1000u, 9u)));
  AC_CHECK(store->append(ac::test::makeMatchRecord("m-002", 2000u, 9u)));
  AC_CHECK_EQ(store->recordCount(), static_cast<std::size_t>(3));
  AC_CHECK(store->flush());

  const std::string text = ac::test::readTextFile(dir.file("data/matches.ndjson"));
  AC_CHECK_EQ(text.size() > 0u, true);
  const std::vector<std::string> recent = idsOf(store->listRecent(2u));
  AC_CHECK_EQ(recent.size(), static_cast<std::size_t>(2));
  AC_CHECK_EQ(recent[0], std::string("m-003"));
  AC_CHECK_EQ(recent[1], std::string("m-002"));

  std::string reloadError;
  std::unique_ptr<ac::persist::MatchStore> reloaded =
      ac::persist::openMatchStore(dataDir, &reloadError);
  AC_CHECK(reloaded != nullptr);
  if (reloaded == nullptr) return;
  AC_CHECK_EQ(reloaded->recordCount(), static_cast<std::size_t>(3));
  AC_CHECK_EQ(reloaded->stats().corruptLines, static_cast<std::size_t>(0));
  AC_CHECK_EQ(reloaded->stats().linesRead, static_cast<std::size_t>(3));
}

AC_TEST(store_rejects_unsafe_record_id) {
  AC_CHECK(ac::persist::isSafeMatchId("ABC1-1700000000000"));
  AC_CHECK(ac::persist::isSafeMatchId("a.b_c-D9"));
  AC_CHECK(!ac::persist::isSafeMatchId(""));
  AC_CHECK(!ac::persist::isSafeMatchId("../escape"));
  AC_CHECK(!ac::persist::isSafeMatchId("a b"));
  AC_CHECK(!ac::persist::isSafeMatchId("a/b"));
  AC_CHECK(!ac::persist::isSafeMatchId(std::string(65u, 'x')));

  ac::test::TempDir dir("store");
  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dir.file("data"), &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  AC_CHECK(!store->append(ac::test::makeMatchRecord("../escape", 1u, 1u)));
  AC_CHECK_EQ(store->recordCount(), static_cast<std::size_t>(0));
  AC_CHECK(!store->append(ac::test::makeMatchRecord("m-1", 1u, 1u, 5u)));  // 人数越界
  AC_CHECK_EQ(store->recordCount(), static_cast<std::size_t>(0));
}

AC_TEST(store_counts_corrupt_lines_and_keeps_going) {
  ac::test::TempDir dir("store");
  const std::string dataDir = dir.file("data");
  std::error_code code;
  fs::create_directories(dataDir, code);
  const std::string path = dataDir + "/matches.ndjson";
  std::FILE* file = std::fopen(path.c_str(), "wb");
  AC_CHECK(file != nullptr);
  if (file == nullptr) return;
  const std::string good = ac::persist::encodeMatchRecordLine(ac::test::makeMatchRecord("m-001", 1000u, 3u));
  std::fwrite(good.data(), 1u, good.size(), file);
  const std::string broken = "{\"matchId\":\"m-002\",\"startedAtMs\"";
  std::fwrite(broken.data(), 1u, broken.size(), file);
  std::fputc('\n', file);
  const std::string missing = "{\"matchId\":\"m-003\"}\n";
  std::fwrite(missing.data(), 1u, missing.size(), file);
  const std::string good2 = ac::persist::encodeMatchRecordLine(ac::test::makeMatchRecord("m-004", 2000u, 1u));
  std::fwrite(good2.data(), 1u, good2.size(), file);
  std::fclose(file);

  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dataDir, &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  AC_CHECK_EQ(store->recordCount(), static_cast<std::size_t>(2));
  AC_CHECK_EQ(store->stats().corruptLines, static_cast<std::size_t>(2));
  AC_CHECK_EQ(store->stats().linesRead, static_cast<std::size_t>(4));
  const std::vector<std::string> ids = idsOf(store->listRecent(10u));
  AC_CHECK_EQ(ids.size(), static_cast<std::size_t>(2));
  AC_CHECK_EQ(ids[0], std::string("m-004"));
  AC_CHECK_EQ(ids[1], std::string("m-001"));
}

AC_TEST(store_top_and_recent_orders_are_stable) {
  ac::test::TempDir dir("store");
  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dir.file("data"), &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  // top：击杀降序 -> startedAtMs 降序 -> matchId 升序；recent：startedAtMs 降序 -> matchId 升序。
  AC_CHECK(store->append(ac::test::makeMatchRecord("m-b", 2000u, 5u)));
  AC_CHECK(store->append(ac::test::makeMatchRecord("m-a", 2000u, 5u)));
  AC_CHECK(store->append(ac::test::makeMatchRecord("m-c", 3000u, 5u)));
  AC_CHECK(store->append(ac::test::makeMatchRecord("m-d", 4000u, 1u)));

  const std::vector<std::string> top = idsOf(store->listTop(10u));
  AC_CHECK_EQ(top.size(), static_cast<std::size_t>(4));
  AC_CHECK_EQ(top[0], std::string("m-c"));
  AC_CHECK_EQ(top[1], std::string("m-a"));
  AC_CHECK_EQ(top[2], std::string("m-b"));
  AC_CHECK_EQ(top[3], std::string("m-d"));

  const std::vector<std::string> recent = idsOf(store->listRecent(10u));
  AC_CHECK_EQ(recent[0], std::string("m-d"));
  AC_CHECK_EQ(recent[1], std::string("m-c"));
  AC_CHECK_EQ(recent[2], std::string("m-a"));
  AC_CHECK_EQ(recent[3], std::string("m-b"));

  AC_CHECK(ac::persist::isOrderedBefore(ac::test::makeMatchRecord("m-a", 2000u, 5u), ac::test::makeMatchRecord("m-b", 2000u, 5u),
                                        ac::persist::RecordOrder::kTop));
  AC_CHECK(!ac::persist::isOrderedBefore(ac::test::makeMatchRecord("m-b", 2000u, 5u), ac::test::makeMatchRecord("m-a", 2000u, 5u),
                                         ac::persist::RecordOrder::kTop));
  AC_CHECK_EQ(ac::persist::totalKills(ac::test::makeMatchRecord("m-x", 0u, 4u, 2u)), static_cast<std::uint64_t>(8));
}

AC_TEST(store_query_limit_is_clamped) {
  ac::test::TempDir dir("store");
  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dir.file("data"), &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  for (int i = 0; i < 5; ++i) {
    char id[32];
    std::snprintf(id, sizeof(id), "m-%d", i);
    AC_CHECK(store->append(ac::test::makeMatchRecord(id, 1000u + static_cast<std::uint64_t>(i), 1u)));
  }
  AC_CHECK_EQ(store->listTop(0u).size(), static_cast<std::size_t>(0));
  AC_CHECK_EQ(store->listTop(2u).size(), static_cast<std::size_t>(2));
  AC_CHECK_EQ(store->listTop(999u).size(), static_cast<std::size_t>(5));
  AC_CHECK_EQ(store->listRecent(1u).size(), static_cast<std::size_t>(1));
}

AC_TEST(store_version_changes_on_append_and_evict) {
  ac::test::TempDir dir("store");
  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dir.file("data"), &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  const std::uint64_t before = store->version();
  AC_CHECK(store->append(ac::test::makeMatchRecord("m-1", 1000u, 1u)));
  const std::uint64_t after = store->version();
  AC_CHECK(after > before);
  AC_CHECK(!store->append(ac::test::makeMatchRecord("../bad", 1000u, 1u)));  // 失败不改版本
  AC_CHECK_EQ(store->version(), after);
  AC_CHECK(store->flush());
  AC_CHECK_EQ(store->recordCount(), static_cast<std::size_t>(1));
}

AC_TEST(store_line_round_trips_every_field_kind) {
  ac::persist::MatchResultRecord record = ac::test::makeMatchRecord("m-round", 1700000000000ull, 2u, 2u);
  record.players[0].name = std::string("q\"uo\\te") + '\x01' + "中";
  record.players[1].name = "tab\there";
  record.players[1].leftMidMatch = true;
  record.players[1].headshots = 7u;
  record.durationMs = 4294967295u;
  record.waveReached = 65535u;
  record.winnerTeam = 1u;
  const std::string line = ac::persist::encodeMatchRecordLine(record);
  AC_CHECK_EQ(line.back(), '\n');
  AC_CHECK(line.find("\"leftMidMatch\":true") != std::string::npos);
  ac::persist::MatchResultRecord decoded;
  AC_CHECK(ac::persist::decodeMatchRecordLine(line, decoded));
  AC_CHECK_EQ(decoded.matchId, record.matchId);
  AC_CHECK_EQ(decoded.durationMs, record.durationMs);
  AC_CHECK_EQ(decoded.waveReached, record.waveReached);
  AC_CHECK_EQ(decoded.winnerTeam, record.winnerTeam);
  AC_CHECK_EQ(decoded.players.size(), record.players.size());
  AC_CHECK_EQ(decoded.players[0].name, record.players[0].name);
  AC_CHECK_EQ(decoded.players[1].name, record.players[1].name);
  AC_CHECK_EQ(decoded.players[1].leftMidMatch, true);
  AC_CHECK_EQ(decoded.players[1].headshots, static_cast<std::uint32_t>(7));
}

AC_TEST(store_rejects_invalid_player_count) {
  AC_CHECK(ac::persist::isValidMatchRecord(ac::test::makeMatchRecord("m-1", 1u, 1u, 1u)));
  AC_CHECK(ac::persist::isValidMatchRecord(ac::test::makeMatchRecord("m-1", 1u, 1u, 4u)));
  AC_CHECK(!ac::persist::isValidMatchRecord(ac::test::makeMatchRecord("m-1", 1u, 1u, 0u)));
  AC_CHECK(!ac::persist::isValidMatchRecord(ac::test::makeMatchRecord("m-1", 1u, 1u, 5u)));
  ac::persist::MatchResultRecord badTeam = ac::test::makeMatchRecord("m-1", 1u, 1u);
  badTeam.winnerTeam = 2u;
  AC_CHECK(!ac::persist::isValidMatchRecord(badTeam));
  ac::persist::MatchResultRecord badId = ac::test::makeMatchRecord("../bad", 1u, 1u);
  AC_CHECK(!ac::persist::isValidMatchRecord(badId));
}

AC_TEST(store_sqlite_seam_is_declared_and_off) {
  AC_CHECK_EQ(ac::persist::kSqliteEnabled, false);
  AC_CHECK_EQ(ac::persist::kMaxRecords, static_cast<std::size_t>(10000));
  AC_CHECK_EQ(ac::persist::kEvictBatch, static_cast<std::size_t>(100));
  AC_CHECK_EQ(ac::persist::kMaxPlayersPerMatch, static_cast<std::size_t>(4));
  AC_CHECK_EQ(ac::persist::kReportDirName, std::string_view("reports"));
}

AC_TEST(store_adapter_copies_simulation_result_into_record) {
  ac::room::MatchResultRecord source{};
  const char* id = "ABCD-1700000000000";
  for (std::size_t i = 0u; i < sizeof(source.matchId) && id[i] != '\0'; ++i) source.matchId[i] = id[i];
  source.startedAtMs = 1700000000000ull;
  source.durationMs = 5000000000ull;  // 超过 u32：夹到上限
  source.waveReached = 70000;         // 超过 u16：夹到上限
  source.winnerTeam = 1u;
  source.playerCount = 2u;
  source.players[0].name[0] = 'A';
  source.players[0].nameBytes = 1u;
  source.players[0].kills = 3u;
  source.players[0].headshots = 1u;
  source.players[0].leftMidMatch = true;
  source.players[1].name[0] = 'B';
  source.players[1].name[1] = 'C';
  source.players[1].nameBytes = 2u;
  source.players[1].aliveMs = 12345u;

  const ac::persist::MatchResultRecord stored = ac::persist::toStoredRecord(source);
  AC_CHECK_EQ(stored.matchId, std::string("ABCD-1700000000000"));
  AC_CHECK_EQ(stored.startedAtMs, static_cast<std::uint64_t>(1700000000000ull));
  AC_CHECK_EQ(stored.durationMs, static_cast<std::uint32_t>(4294967295u));
  AC_CHECK_EQ(stored.waveReached, static_cast<std::uint16_t>(65535u));
  AC_CHECK_EQ(stored.winnerTeam, static_cast<std::uint8_t>(1));
  AC_CHECK_EQ(stored.players.size(), static_cast<std::size_t>(2));
  AC_CHECK_EQ(stored.players[0].name, std::string("A"));
  AC_CHECK_EQ(stored.players[0].kills, static_cast<std::uint32_t>(3));
  AC_CHECK_EQ(stored.players[0].headshots, static_cast<std::uint32_t>(1));
  AC_CHECK_EQ(stored.players[0].leftMidMatch, true);
  AC_CHECK_EQ(stored.players[1].name, std::string("BC"));
  AC_CHECK_EQ(stored.players[1].aliveMs, static_cast<std::uint32_t>(12345));
  AC_CHECK(ac::persist::isValidMatchRecord(stored));
}

AC_TEST(store_open_error_is_reported) {
  ac::test::TempDir dir("store");
  const std::string blocker = dir.file("blocker");
  std::FILE* file = std::fopen(blocker.c_str(), "wb");
  AC_CHECK(file != nullptr);
  if (file == nullptr) return;
  std::fputs("x", file);
  std::fclose(file);

  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store =
      ac::persist::openMatchStore(blocker + "/sub", &error);
  AC_CHECK(store == nullptr);
  AC_CHECK_EQ(error.empty(), false);
}

AC_TEST(store_data_dir_env_fallback) {
  AC_CHECK_EQ(ac::persist::dataDirFromEnv().empty(), false);
  // 保存原值：结束时恢复成原值（未设则清空），不留空串给后面的用例。
  const char* previousDataDir = std::getenv("AC_DATA_DIR");
  const std::string savedDataDir = previousDataDir != nullptr ? std::string(previousDataDir) : std::string();
  AC_CHECK_EQ(ac::test::setDataDirEnv("D:/ac-s13-data"), true);
  AC_CHECK_EQ(ac::persist::dataDirFromEnv(), std::string("D:/ac-s13-data"));
  AC_CHECK_EQ(ac::test::clearDataDirEnv(), true);
  // S15：未设 AC_DATA_DIR 时回落**平台默认值**（Windows data / POSIX /var/lib/angry-chen），见 README §18.8-1。
  AC_CHECK_EQ(ac::persist::dataDirFromEnv(), [] {
#ifdef _WIN32
    return std::string("data");
#else
    return std::string("/var/lib/angry-chen");
#endif
  }());
  if (previousDataDir != nullptr) {
    AC_CHECK_EQ(ac::test::setDataDirEnv(savedDataDir), true);
  } else {
    AC_CHECK_EQ(ac::test::clearDataDirEnv(), true);
  }
  AC_CHECK_EQ(ac::persist::dataDirFromEnv(),
              savedDataDir.empty() ? [] {
#ifdef _WIN32
                return std::string("data");
#else
                return std::string("/var/lib/angry-chen");
#endif
              }()
                                    : savedDataDir);
}

AC_TEST(store_hundred_thousand_lines_stay_bounded) {
  ac::test::TempDir dir("store");
  AC_CHECK(dir.isReady());
  const std::string dataDir = dir.file("ndjson");
  std::error_code code;
  fs::create_directories(dataDir, code);
  const std::string storePath = dataDir + "/matches.ndjson";
  const char* fixture = ac::test::fixturePath();
  if (fixture != nullptr) {
    fs::create_directories(fs::path(fixture).parent_path(), code);
    writeFixture(fixture, 100000u);  // 不把外部文件当缓存：门禁路径上的内容由用例决定
    fs::copy_file(fixture, storePath, fs::copy_options::overwrite_existing, code);
    AC_CHECK(!code);
  } else {
    writeFixture(storePath, 100000u);
  }

  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dataDir, &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  AC_CHECK_EQ(store->stats().linesRead, static_cast<std::size_t>(100000));
  AC_CHECK_EQ(store->stats().corruptLines, static_cast<std::size_t>(0));
  AC_CHECK_EQ(store->stats().retained, ac::persist::kMaxRecords);
  AC_CHECK_EQ(store->recordCount(), ac::persist::kMaxRecords);
  AC_CHECK_EQ(store->stats().evicted, static_cast<std::size_t>(90000));
  AC_CHECK(store->version() > 0u);
  std::printf("retained=%zu corrupt=%zu\n", store->recordCount(), store->stats().corruptLines);
}

AC_TEST(store_partial_query_equals_full_sort_prefix) {
  ac::test::TempDir dir("store");
  std::string error;
  std::unique_ptr<ac::persist::MatchStore> store = ac::persist::openMatchStore(dir.file("data"), &error);
  AC_CHECK(store != nullptr);
  if (store == nullptr) return;
  // 60 条记录、故意制造平票（只有 7 个开始时刻、5 档击杀），排序决胜必须落到 matchId。
  std::vector<ac::persist::MatchResultRecord> all;
  for (std::size_t i = 0u; i < 60u; ++i) {
    char id[32];
    std::snprintf(id, sizeof(id), "m-%02zu", i);
    all.push_back(ac::test::makeMatchRecord(id, 1000u + (i % 7u), static_cast<std::uint32_t>(i % 5u)));
    AC_CHECK(store->append(all.back()));
  }
  const auto totalKillsOf = [](const ac::persist::MatchResultRecord& record) {
    std::uint64_t total = 0u;
    for (const ac::persist::PlayerResultRecord& player : record.players) total += player.kills;
    return total;
  };
  std::vector<std::size_t> order(all.size());
  for (std::size_t i = 0u; i < order.size(); ++i) order[i] = i;

  // 独立于库内实现的全排序（键在测试里重新提取），再与「查询取前 N」逐条比对。
  std::stable_sort(order.begin(), order.end(), [&all, &totalKillsOf](std::size_t a, std::size_t b) {
    const std::uint64_t ka = totalKillsOf(all[a]);
    const std::uint64_t kb = totalKillsOf(all[b]);
    if (ka != kb) return ka > kb;
    if (all[a].startedAtMs != all[b].startedAtMs) return all[a].startedAtMs > all[b].startedAtMs;
    return all[a].matchId < all[b].matchId;
  });
  for (const std::size_t limit : {1u, 3u, 10u, 60u, 999u}) {
    const std::vector<ac::persist::MatchResultRecord> top = store->listTop(limit);
    const std::size_t expected = limit < all.size() ? limit : all.size();
    AC_CHECK_EQ(top.size(), expected);
    for (std::size_t i = 0u; i < expected; ++i) {
      AC_CHECK_EQ(top[i].matchId, all[order[i]].matchId);
    }
  }
  std::stable_sort(order.begin(), order.end(), [&all](std::size_t a, std::size_t b) {
    if (all[a].startedAtMs != all[b].startedAtMs) return all[a].startedAtMs > all[b].startedAtMs;
    return all[a].matchId < all[b].matchId;
  });
  for (const std::size_t limit : {1u, 7u, 60u}) {
    const std::vector<ac::persist::MatchResultRecord> recent = store->listRecent(limit);
    const std::size_t expected = limit < all.size() ? limit : all.size();
    AC_CHECK_EQ(recent.size(), expected);
    for (std::size_t i = 0u; i < expected; ++i) {
      AC_CHECK_EQ(recent[i].matchId, all[order[i]].matchId);
    }
  }
}
