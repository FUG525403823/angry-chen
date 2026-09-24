using System.Collections.Generic;

namespace Ac.UI
{
    // C12 §5：字段照抄服务端冻结 schema（server/src/persist/match_store.hpp 的 PlayerResultRecord / MatchResultRecord），不新增字段。
    public struct PlayerRecord
    {
        public string Name;
        public int Kills;
        public int Headshots;
        public int ShotsFired;
        public int Hits;
        public int Revives;
        public int Downs;
        public int AliveMs;
        public bool LeftMidMatch;
    }

    public struct MatchRecord
    {
        public string MatchId;
        public long StartedAtMs;
        public int DurationMs;
        public int WaveReached;
        public int WinnerTeam;
        public int PlayerCount;      // 服务端 NDJSON 必写字段，且必须等于 players 长度
        public PlayerRecord[] Players;
    }

    public struct ResultsSummary
    {
        public string MatchId;
        public int WaveReached;
        public int WinnerTeam;
        public int DurationMs;
        public MatchRecord[] Records;
    }

    // C12 §4 任务 7 / §5：榜单排序与服务端 isOrderedBefore(top) 一致；拉取失败 400ms 重试、最多 3 次后本地摘要兜底。
    public sealed class Results
    {
        public const int LeaderboardLimit = 10;
        public const int RetryMs = 400;
        public const int RetryMax = 3;
        public const int MaxPlayersPerMatch = 4;      // 服务端 kMaxPlayersPerMatch
        public const int MatchIdMaxLength = 64;       // 服务端 kMaxMatchIdLength
        public const int PlayerNameMaxBytes = 64;     // 服务端 kMaxPlayerNameBytes

        private readonly List<MatchRecord> _leaderboard = new List<MatchRecord>(LeaderboardLimit);

        public bool Visible { get; private set; }
        public bool RetryPending { get; private set; }
        public bool StaleBanner { get; private set; }
        public int RetryCount { get; private set; }
        public int FetchRequestCount { get; private set; }
        public int WaveReached { get; private set; }
        public int WinnerTeam { get; private set; }
        public float RetryRemainingMs { get; private set; }
        public string MatchId { get; private set; }
        public IReadOnlyList<MatchRecord> Leaderboard { get { return _leaderboard; } }

        public Results() { MatchId = string.Empty; }

        public static long TotalKills(in MatchRecord record)
        {
            if (record.Players == null) return 0L;
            long total = 0;
            for (var i = 0; i < record.Players.Length; i++) total += record.Players[i].Kills;
            return total;
        }

        // top = 击杀降序 → startedAtMs 降序 → matchId 升序（服务端 §5「排序一致性」逐字一致）
        public static bool IsOrderedBefore(in MatchRecord a, in MatchRecord b)
        {
            var killsA = TotalKills(a);
            var killsB = TotalKills(b);
            if (killsA != killsB) return killsA > killsB;
            if (a.StartedAtMs != b.StartedAtMs) return a.StartedAtMs > b.StartedAtMs;
            return string.CompareOrdinal(a.MatchId, b.MatchId) < 0;
        }

        public static MatchRecord[] SortTop(MatchRecord[] records, int limit)
        {
            if (records == null) return new MatchRecord[0];
            var copy = new MatchRecord[records.Length];
            System.Array.Copy(records, copy, records.Length);
            for (var i = 1; i < copy.Length; i++)            // 插入排序：≤10 条，不分配、不用 LINQ
            {
                var key = copy[i];
                var j = i - 1;
                while (j >= 0 && IsOrderedBefore(key, copy[j])) { copy[j + 1] = copy[j]; j -= 1; }
                copy[j + 1] = key;
            }
            if (limit >= 0 && copy.Length > limit)
            {
                var trimmed = new MatchRecord[limit];
                System.Array.Copy(copy, trimmed, limit);
                return trimmed;
            }
            return copy;
        }

        public static bool IsValidMatchId(string matchId)
        {
            if (string.IsNullOrEmpty(matchId) || matchId.Length > MatchIdMaxLength) return false;
            for (var i = 0; i < matchId.Length; i++)
            {
                var c = matchId[i];
                var ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
                if (!ok) return false;
            }
            return true;
        }

        // 对齐服务端 isValidMatchRecord：matchId 字符集/长度、players 非空且 ≤4、playerCount 自洽、
        // winnerTeam ∈ {0,1}、每个玩家名 1..64 字节。
        public static bool IsValid(in MatchRecord record)
        {
            if (!IsValidMatchId(record.MatchId)) return false;
            if (record.Players == null || record.Players.Length == 0) return false;
            if (record.Players.Length > MaxPlayersPerMatch) return false;
            if (record.PlayerCount != record.Players.Length) return false;
            if (record.WinnerTeam < 0 || record.WinnerTeam > 1) return false;
            for (var i = 0; i < record.Players.Length; i++)
            {
                var name = record.Players[i].Name;
                if (string.IsNullOrEmpty(name)) return false;
                if (System.Text.Encoding.UTF8.GetByteCount(name) > PlayerNameMaxBytes) return false;
            }
            return true;
        }

        public void Show(in ResultsSummary summary)
        {
            Visible = true;
            RetryPending = false;
            RetryCount = 0;
            RetryRemainingMs = 0f;
            MatchId = summary.MatchId;
            WaveReached = summary.WaveReached;
            WinnerTeam = summary.WinnerTeam;
            _leaderboard.Clear();
            var top = SortTop(summary.Records, LeaderboardLimit);
            for (var i = 0; i < top.Length; i++) _leaderboard.Add(top[i]);
            // 榜单为空（接口不可达 / 还没落盘）→ 用本局摘要兜底，不阻塞返回大厅
            StaleBanner = _leaderboard.Count == 0;
            if (StaleBanner)
            {
                var local = default(MatchRecord);
                local.MatchId = summary.MatchId;
                local.WaveReached = summary.WaveReached;
                local.WinnerTeam = summary.WinnerTeam;
                local.DurationMs = summary.DurationMs;
                local.PlayerCount = 0;
                if (IsValidMatchId(local.MatchId)) _leaderboard.Add(local);
            }
        }

        public void RequestFetch() { FetchRequestCount += 1; }

        // 拉取失败：400ms 后重试，最多 3 次；之后停在"榜单暂不可用"的本地摘要上
        public bool OnFetchFailed()
        {
            if (RetryCount >= RetryMax) { RetryPending = false; StaleBanner = true; return false; }
            RetryCount += 1;
            RetryPending = true;
            RetryRemainingMs = RetryMs;
            return true;
        }

        public void Tick(float dtMs)
        {
            if (!RetryPending) return;
            RetryRemainingMs -= dtMs;
            if (RetryRemainingMs > 0f) return;
            RetryPending = false;
            RequestFetch();
        }

        public void Hide() { Visible = false; }

        // §5 阶段表：ended 才显示结算。显隐只由服务器下发的 phase 驱动。
        public static bool VisibleOf(byte phase) { return phase == Hud.PhaseEnded; }

        public void ApplyPhase(byte phase)
        {
            Visible = VisibleOf(phase);
            if (Visible) RequestFetch();
        }
    }
}
