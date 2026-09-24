using Ac.Net;

namespace Ac.UI
{
    // C12 §4 任务 2 / §5 队伍字段表：pid <= 0 的行整行不渲染、不计数。
    public static class Roster
    {
        public const int VisibleRowLimit = 4;      // SelfRowLimit
        public const int WeaponPistol = 0;
        public const int WeaponRifle = 1;
        public const int WeaponShotgun = 2;

        public static int PlayerCount(MatchStatePlayer[] players)
        {
            if (players == null) return 0;
            var count = 0;
            for (var i = 0; i < players.Length; i++) if (players[i].Pid > 0) count += 1;
            return count;
        }

        public static int ReadyCount(MatchStatePlayer[] players)
        {
            if (players == null) return 0;
            var count = 0;
            for (var i = 0; i < players.Length; i++) if (players[i].Pid > 0 && players[i].Ready) count += 1;
            return count;
        }

        public static bool AllReady(MatchStatePlayer[] players)
        {
            var total = PlayerCount(players);
            return total > 0 && ReadyCount(players) == total;
        }

        public static int HostPid(MatchStatePlayer[] players)
        {
            if (players == null) return 0;
            var host = 0;
            for (var i = 0; i < players.Length; i++)
            {
                var pid = players[i].Pid;
                if (pid <= 0) continue;
                if (host == 0 || pid < host) host = pid;
            }
            return host;
        }

        public static bool IsHost(MatchStatePlayer[] players, int selfPid)
        {
            var host = HostPid(players);
            return host > 0 && host == selfPid;
        }

        public static MatchStatePlayer? Self(MatchStatePlayer[] players, int pid)
        {
            if (players == null || pid <= 0) return null;
            for (var i = 0; i < players.Length; i++) if (players[i].Pid == pid) return players[i];
            return null;
        }

        public static string WeaponLabel(int weapon)
        {
            if (weapon == WeaponPistol) return "手枪";
            if (weapon == WeaponRifle) return "步枪";
            if (weapon == WeaponShotgun) return "霰弹枪";
            return "未知";
        }

        // 波间：倒地优先于准备状态
        public static string StatusLabel(in MatchStatePlayer player)
        {
            if (player.Downed) return "倒地";
            return player.Ready ? "已准备" : "未准备";
        }

        public static int VisibleRows(MatchStatePlayer[] players)
        {
            var total = PlayerCount(players);
            return total < VisibleRowLimit ? total : VisibleRowLimit;
        }
    }
}
