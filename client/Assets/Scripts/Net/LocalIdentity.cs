using System;

namespace Ac.Net
{
    // 本地玩家身份 —— **过渡方案**，不是权威身份。
    //
    // 服务端事实（联调时逐条核对，见 docs/evidence/client-v2-acceptance.md）：
    //  · pid 就是玩家实体的 EntityId（server/src/room/match_controller.hpp）。
    //  · HelloAck 只有 serverTick + salt（server/src/net/codec.hpp）；kJoin（type 11，ADR-009）
    //    是**客户端单向上报**昵称，服务端不回 pid ⇒ 客户端仍然拿不到权威身份。
    //  · 昵称确实被服务端采用：`handleJoin` → `setSessionName`（净化失败则丢弃这一帧、保留旧值）；
    //    准入在 Hello 那一步完成，房间满/开局中会被 `Disconnect(8)` 拒掉（ADR-012）。
    //    `roomJoin` 里的 "player" 只是**从未发过 kJoin**时的兜底。
    //  · 所以 MatchState（type=10，§5.7）仍是唯一带 pid + name 的下行通道，
    //    本地身份只能按"昵称严格相等"从玩家表里认领一行。
    //
    // 这个方案的代价必须写明：服务端不保证昵称唯一（净化后仍可能同名）⇒ **同名会串**（本类取最小 pid
    // 并把次数记进 AmbiguousCount，不假装它是权威身份）。一旦服务端在握手/加入回复里带回 pid，
    // 本类必须整体替换，不能继续沿用。
    public sealed class LocalIdentity
    {
        public const ushort NoPid = 0;

        private string _name = string.Empty;

        // 本地输入的昵称（已经过 Lobby.SanitizeName，与 wire 上限同源）。
        public string Name { get { return _name; } }
        // 最近一次重解析的结果：0 = 玩家表里没有本地昵称 ⇒ 没有本地实体。
        public ushort Pid { get; private set; }
        public int ResolveCount { get; private set; }
        public int MatchedCount { get; private set; }
        // 同名多行时的猜测次数（歧义的可见账）。
        public int AmbiguousCount { get; private set; }

        public void SetName(string sanitizedName) { _name = sanitizedName ?? string.Empty; }

        // 纯函数：昵称序数相等的那一行；多行同名取最小 pid（确定性，不随表序漂移）。
        public static ushort MatchPid(MatchStatePlayer[] players, string name)
        {
            if (players == null || string.IsNullOrEmpty(name)) return NoPid;
            var pid = NoPid;
            for (var i = 0; i < players.Length; i++)
            {
                var row = players[i];
                if (row.Pid == NoPid) continue;
                if (!string.Equals(row.Name, name, StringComparison.Ordinal)) continue;
                if (pid == NoPid || row.Pid < pid) pid = row.Pid;
            }
            return pid;
        }

        public static int CountNameMatches(MatchStatePlayer[] players, string name)
        {
            if (players == null || string.IsNullOrEmpty(name)) return 0;
            var count = 0;
            for (var i = 0; i < players.Length; i++)
            {
                if (players[i].Pid == NoPid) continue;
                if (string.Equals(players[i].Name, name, StringComparison.Ordinal)) count += 1;
            }
            return count;
        }

        // 重解析：返回 Pid 是否变化。昵称不在表里（还没进房/自己中途离开）⇒ Pid = 0，
        // 而不是"保留上一次的 pid"：pid 就是实体 id，实体 id 会被回收，留着旧值会指到别人身上。
        public bool Resolve(MatchStatePlayer[] players)
        {
            ResolveCount += 1;
            var pid = MatchPid(players, _name);
            if (pid != NoPid && CountNameMatches(players, _name) > 1) AmbiguousCount += 1;
            if (pid == Pid) return false;
            Pid = pid;
            if (pid != NoPid) MatchedCount += 1;
            return true;
        }
    }
}
