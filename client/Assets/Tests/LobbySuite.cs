using System;
using Ac.Core;
using Ac.Net;
using Ac.UI;
using UnityEngine;

namespace Ac.Tests
{
    // C12 §6/§7：房间码归一与判定、阶段显隐、聊天截断与限流、波间规则、结算排序与重试、schema 镜像。
    public static class LobbySuite
    {
        public static void Add(string id, System.Action body) { SelfTest.Add(id, body); }

        public static void Register()
        {
            Add("lobby.roomcode_normalize", ChecksNormalize);
            Add("lobby.roomcode_validate", ChecksValidate);
            Add("lobby.roster", ChecksRoster);
            Add("lobby.picker", ChecksPicker);
            Add("lobby.chat_truncate", ChecksChatTruncate);
            Add("lobby.chat_limits", ChecksChatLimits);
            Add("lobby.intermission", ChecksIntermission);
            Add("lobby.phase_visibility", ChecksPhaseVisibility);
            Add("lobby.name_sanitize", ChecksName);
            Add("lobby.results_sort", ChecksResultsSort);
            Add("lobby.results_retry", ChecksResultsRetry);
            Add("lobby.results_schema", ChecksResultsSchema);
        }

        private static MatchStatePlayer Player(int pid, bool ready)
        {
            var player = default(MatchStatePlayer);
            player.Pid = (ushort)pid;
            player.Ready = ready;
            player.Name = "p" + pid;
            player.Weapon = 1;
            return player;
        }

        private static MatchStatePayload State(byte phase, int wave, int intermissionMs, params MatchStatePlayer[] players)
        {
            var state = default(MatchStatePayload);
            state.Phase = phase;
            state.Wave = (byte)wave;
            state.IntermissionMs = (ushort)intermissionMs;
            state.Players = players;
            return state;
        }

        private static void ChecksNormalize()
        {
            SelfTest.True(RoomCodeInput.RoomCodeLength == 4, "房间码 4 位", RoomCodeInput.RoomCodeLength.ToString());
            SelfTest.Equal(31, (long)RoomCodeInput.RoomCodeAlphabet.Length);
            SelfTest.True(RoomCodeInput.RoomCodeAlphabet == "ABCDEFGHJKMNPQRSTUVWXYZ23456789", "字母表逐字一致", RoomCodeInput.RoomCodeAlphabet);
            SelfTest.True(RoomCodeInput.RoomCodeAlphabet.IndexOf('I') < 0 && RoomCodeInput.RoomCodeAlphabet.IndexOf('L') < 0 && RoomCodeInput.RoomCodeAlphabet.IndexOf('O') < 0, "排除 I/L/O", "还在");
            SelfTest.True(RoomCodeInput.RoomCodeAlphabet.IndexOf('0') < 0 && RoomCodeInput.RoomCodeAlphabet.IndexOf('1') < 0, "排除 0/1", "还在");
            SelfTest.True(RoomCodeInput.Normalize("ab3k") == "AB3K", "①转大写", RoomCodeInput.Normalize("ab3k"));
            SelfTest.True(RoomCodeInput.Normalize("a b0o") == "AB", "②丢弃表外字符", RoomCodeInput.Normalize("a b0o"));
            SelfTest.True(RoomCodeInput.Normalize("ABCDEF") == "ABCD", "③截断 4 位", RoomCodeInput.Normalize("ABCDEF"));
            SelfTest.True(RoomCodeInput.Normalize("ＡＢ３Ｋ") == string.Empty, "全角字符被丢弃", RoomCodeInput.Normalize("ＡＢ３Ｋ"));
            SelfTest.True(RoomCodeInput.Normalize("") == string.Empty, "空串仍是空串", RoomCodeInput.Normalize(""));
            SelfTest.True(RoomCodeInput.Normalize("  hg7m  ") == "HG7M", "两侧空格被丢弃", RoomCodeInput.Normalize("  hg7m  "));
            SelfTest.True(RoomCodeInput.Normalize("i1l0o") == string.Empty, "全是表外字符", RoomCodeInput.Normalize("i1l0o"));
            SelfTest.True(RoomCodeInput.Normalize("ab-cd") == "ABCD", "连字符丢弃且继续吃后续字符", RoomCodeInput.Normalize("ab-cd"));
        }

        private static void ChecksValidate()
        {
            SelfTest.True(RoomCodeInput.IsJoinable("ABCD"), "ABCD 可加入", "不可加入");
            SelfTest.True(!RoomCodeInput.IsJoinable("0000"), "0000 是新建哨兵", "被当成加入码");
            SelfTest.True(RoomCodeInput.NewRoomCode == "0000", "哨兵值", RoomCodeInput.NewRoomCode);
            SelfTest.True(!RoomCodeInput.IsJoinable("ABC"), "长度不足非法", "判成合法");
            SelfTest.True(!RoomCodeInput.IsJoinable("ABCDE"), "长度超限非法", "判成合法");
            SelfTest.True(!RoomCodeInput.IsJoinable(""), "空码非法", "判成合法");
            SelfTest.True(!RoomCodeInput.IsJoinable("ABID"), "含 I 非法", "判成合法");
            SelfTest.True(RoomCodeInput.Validate("0000") == RoomCodeError.Sentinel, "错误码 = 哨兵", RoomCodeInput.Validate("0000").ToString());
            SelfTest.True(RoomCodeInput.Validate("") == RoomCodeError.Empty, "错误码 = 空", RoomCodeInput.Validate("").ToString());
            SelfTest.True(RoomCodeInput.Validate("ABC") == RoomCodeError.WrongLength, "错误码 = 长度", RoomCodeInput.Validate("ABC").ToString());
            var input = new RoomCodeInput();
            input.SetRaw("ab3k");
            SelfTest.True(input.Code == "AB3K" && input.CanJoin && input.ErrorText.Length == 0, "输入框归一后可直接加入", input.Code + "/" + input.ErrorText);
            input.SetRaw("0000");
            SelfTest.True(!input.CanJoin && input.ErrorText.Length > 0, "哨兵有错误文案", input.ErrorText);
            input.SetRaw("A");
            SelfTest.True(input.ErrorText.IndexOf("4", StringComparison.Ordinal) >= 0, "长度错误文案说明 4 位", input.ErrorText);
        }

        private static void ChecksRoster()
        {
            var players = new[] { Player(7, true), Player(3, false), Player(0, true), Player(9, true) };
            SelfTest.Equal(3, (long)Roster.PlayerCount(players));            // pid <= 0 不计数
            SelfTest.Equal(2, (long)Roster.ReadyCount(players));
            SelfTest.True(!Roster.AllReady(players), "未全员准备", "判成全员");
            SelfTest.Equal(3, (long)Roster.HostPid(players));                // min(pid)
            SelfTest.True(Roster.IsHost(players, 3) && !Roster.IsHost(players, 7), "只有主机看到开始按钮", "判错");
            var self = Roster.Self(players, 9);
            SelfTest.True(self.HasValue && self.Value.Pid == 9, "自身行", self.HasValue ? self.Value.Pid.ToString() : "null");
            SelfTest.True(!Roster.Self(players, 0).HasValue, "pid 0 没有行", "有行");
            SelfTest.True(Roster.AllReady(new[] { Player(1, true) }), "单人全准备", "判成未全准备");
            SelfTest.True(!Roster.AllReady(new MatchStatePlayer[0]), "无人不算全准备", "判成全员");
            SelfTest.Equal(0, (long)Roster.HostPid(new[] { Player(0, false) }));
            SelfTest.True(Roster.WeaponLabel(0) == "手枪" && Roster.WeaponLabel(1) == "步枪" && Roster.WeaponLabel(2) == "霰弹枪", "武器标签三档", Roster.WeaponLabel(2));
            SelfTest.True(Roster.WeaponLabel(9) == "未知", "越界武器未知", Roster.WeaponLabel(9));
            var downed = Player(1, true);
            downed.Downed = true;
            SelfTest.True(Roster.StatusLabel(downed) == "倒地", "倒地优先于准备", Roster.StatusLabel(downed));
            SelfTest.True(Roster.StatusLabel(Player(1, true)) == "已准备" && Roster.StatusLabel(Player(1, false)) == "未准备", "准备状态文案", Roster.StatusLabel(Player(1, false)));
            SelfTest.Equal(4, (long)Roster.VisibleRowLimit);
            var five = new[] { Player(1, true), Player(2, true), Player(3, true), Player(4, true), Player(5, true) };
            SelfTest.Equal(4, (long)Roster.VisibleRows(five));
        }

        private static void ChecksPicker()
        {
            var picker = new WeaponPicker();
            SelfTest.Equal(3, (long)WeaponPicker.SlotCount);
            SelfTest.Equal(0, (long)picker.Selected);   // 服务端默认槽位 0（手枪）
            var seen = -1;
            var calls = 0;
            picker.Changed += slot => { seen = slot; calls += 1; };
            SelfTest.True(picker.Select(2), "切到霰弹枪", "没切");
            SelfTest.Equal(2, (long)seen);
            SelfTest.Equal(1, calls);
            SelfTest.True(!picker.Select(2), "同槽位不重复回调", "又回调了");
            SelfTest.Equal(1, calls);
            SelfTest.True(!picker.Select(3) && !picker.Select(-1), "越界槽位被拒", "接受了");
            SelfTest.Equal(2, (long)picker.RejectedCount);
            SelfTest.Equal(2, (long)picker.Selected);
            SelfTest.True(picker.Label == "霰弹枪", "标签跟随选中", picker.Label);
            var fresh = new WeaponPicker();
            SelfTest.True(fresh.Label == "手枪", "默认槽位跟服务端一致（手枪）", fresh.Label);
        }

        private static void ChecksChatTruncate()
        {
            SelfTest.Equal(64, (long)Chat.ChatMaxBytes);
            SelfTest.Equal(64, (long)Chat.TruncateUtf8(new string('a', 100), 64).Length);
            SelfTest.Equal(64, (long)Chat.Utf8Bytes(Chat.TruncateUtf8(new string('a', 100), 64)));
            var chinese = new string('羊', 30);                       // 每字 3 字节 = 90
            var cut = Chat.TruncateUtf8(chinese, Chat.ChatMaxBytes);
            SelfTest.Equal(63, (long)Chat.Utf8Bytes(cut));            // 21 字 × 3，不切断多字节字符
            SelfTest.Equal(21, (long)cut.Length);
            SelfTest.True(cut[cut.Length - 1] == '羊', "最后一个字符完整", cut[cut.Length - 1].ToString());
            var mixed = "ab" + new string('羊', 25);
            SelfTest.True(Chat.Utf8Bytes(Chat.TruncateUtf8(mixed, Chat.ChatMaxBytes)) <= Chat.ChatMaxBytes, "混合文本不超 64 字节", Chat.Utf8Bytes(Chat.TruncateUtf8(mixed, Chat.ChatMaxBytes)).ToString());
            SelfTest.Equal(4, (long)Chat.Utf8Bytes("🐑"));
            SelfTest.True(Chat.Utf8Bytes(Chat.TruncateUtf8("🐑🐑🐑🐑", 6)) <= 6, "代理对整只保留", Chat.Utf8Bytes(Chat.TruncateUtf8("🐑🐑🐑🐑", 6)).ToString());
            SelfTest.Equal(0, (long)Chat.Utf8Bytes(""));
            SelfTest.True(Chat.TruncateUtf8(null, 8) == string.Empty, "null 变空串", "非空");
        }

        private static void ChecksChatLimits()
        {
            SelfTest.True(Chat.ChatMinIntervalMs == 500f, "间隔 500ms", Chat.ChatMinIntervalMs.ToString("R"));
            SelfTest.True(Chat.ChatWindowMs == 5000f, "窗口 5000ms", Chat.ChatWindowMs.ToString("R"));
            SelfTest.Equal(5, (long)Chat.ChatWindowMax);
            SelfTest.Equal(5, (long)Chat.ChatMaxLines);

            var chat = new Chat();
            string hint;
            SelfTest.True(!chat.TrySend("   ", out hint) && hint == Chat.HintEmpty, "空文本提示", hint);
            SelfTest.True(chat.TrySend("第一条", out hint) && hint.Length == 0, "首条可发", hint);
            SelfTest.True(!chat.TrySend("太快", out hint) && hint == Chat.HintTooFast, "500ms 内第二次丢弃", hint);
            chat.Tick(600f);
            SelfTest.True(chat.TrySend("第二条", out hint), "过 600ms 可发", hint);
            chat.Tick(600f);
            chat.TrySend("第三条", out hint);
            chat.Tick(600f);
            chat.TrySend("第四条", out hint);
            chat.Tick(600f);
            chat.TrySend("第五条", out hint);
            chat.Tick(600f);
            SelfTest.True(!chat.TrySend("第六条", out hint) && hint == Chat.HintTooOften, "5 秒窗口第 6 条丢弃", hint);
            SelfTest.Equal(5, (long)chat.SentCount);
            SelfTest.True(chat.LineCount == 5, "缓冲满 5 行", chat.LineCount.ToString());
            chat.Push(3, "别人发的");                                   // 超出丢最旧
            SelfTest.Equal(5, (long)chat.LineCount);
            SelfTest.True(chat.Lines[4] == "别人发的", "新行在末尾", chat.Lines[4]);
            SelfTest.True(chat.DroppedCount >= 3, "被丢的条数有记录", chat.DroppedCount.ToString());
            chat.Tick(6000f);
            SelfTest.True(chat.TrySend("窗口过去后可发", out hint), "窗口滚出后可发", hint);
            chat.SetVisible(true);
            SelfTest.True(chat.Visible, "可见性可设", "不可见");
            SelfTest.True(Chat.TruncateUtf8(new string('x', 80), Chat.ChatMaxBytes).Length == 64, "超长被截断而不是拒发", "未截断");
            var longChat = new Chat();
            longChat.Tick(1000f);
            SelfTest.True(longChat.TrySend(new string('x', 80), out hint) && hint == Chat.HintTruncated, "超长要有提示", hint);
            SelfTest.True(longChat.Lines[0].Length == 64, "送到聊天框的是截断后的", longChat.Lines[0].Length.ToString());
        }

        private static void ChecksIntermission()
        {
            SelfTest.Equal(20000, (long)Intermission.IntermissionInitialMs);
            SelfTest.Equal(5000, (long)Intermission.IntermissionMinMs);
            var window = new Intermission();
            SelfTest.Equal(20000, (long)window.RemainingMs);
            var two = new[] { Player(1, false), Player(2, false) };
            window.Apply(State(Hud.PhaseIntermission, 3, 18000, two), 1);
            SelfTest.True(window.Visible, "intermission + wave>=1 可见", "不可见");
            SelfTest.Equal(18000, (long)window.RemainingMs);          // 按服务器值覆盖，不外推
            SelfTest.True(!window.SkipEnabled, "18000 > 15000 禁用跳过", "可跳过");
            window.Apply(State(Hud.PhaseIntermission, 3, 15000, two), 1);
            SelfTest.True(window.SkipEnabled, "15000 放开跳过", "仍禁用");
            window.Apply(State(Hud.PhaseIntermission, 3, 4000, two), 1);
            SelfTest.True(window.SkipEnabled, "4000 可跳过", "禁用");
            SelfTest.True(Math.Abs(window.RemainingSeconds - 4f) < 1e-4f, "0.1s 精度显示", window.RemainingSeconds.ToString("R"));
            window.Apply(State(Hud.PhasePlaying, 3, 0, two), 1);
            SelfTest.True(!window.Visible && !window.SkipEnabled, "playing 时波间不可见", "仍可见");
            window.Apply(State(Hud.PhaseIntermission, 0, 9000, two), 1);
            SelfTest.True(!window.Visible, "wave 0 不算波间", "判成波间");
            // 全员准备 → 立即请求开波
            var ready = new[] { Player(1, true), Player(2, true) };
            window.Apply(State(Hud.PhaseIntermission, 4, 12000, ready), 1);
            SelfTest.True(window.StartRequested, "全员准备立即请求开波", "没请求");
            SelfTest.True(window.SelfReady, "自身准备态来自服务器", "不是");
            SelfTest.True(!window.RequestStart(State(Hud.PhaseIntermission, 4, 12000, two)), "有人没准备就不请求", "请求了");
            var callbacks = 0;
            window.OnReadyChanged += (isReady, slot) => { callbacks += 1; };
            SelfTest.True(window.SetReady(true), "第一次设置准备有回调", "没回调");
            SelfTest.True(!window.SetReady(true), "重复设置不重复回调", "又回调了");
            SelfTest.Equal(1, callbacks);
            SelfTest.True(window.SetReady(false), "改回未准备有回调", "没回调");
            SelfTest.Equal(2, callbacks);
            SelfTest.True(window.Weapon.Select(2) && window.Weapon.Selected == 2, "波间可换武器", "换不了");
        }

        private static void ChecksPhaseVisibility()
        {
            var lobby = new Lobby();
            SelfTest.Equal(5, (long)LobbySuitePhaseCount);
            SelfTest.True(Lobby.LobbyVisibleOf(Hud.PhaseLobby) && !Lobby.LobbyVisibleOf(Hud.PhasePlaying), "lobby 只在大厅", "错位");
            SelfTest.True(Lobby.LoadingVisibleOf(Hud.PhaseLoading), "loading 显示加载条", "没显示");
            SelfTest.True(Lobby.HudVisibleOf(Hud.PhasePlaying), "playing 显示 HUD", "没显示");
            SelfTest.True(Lobby.IntermissionVisibleOf(Hud.PhaseIntermission, 2), "intermission + wave>=1", "没显示");
            SelfTest.True(!Lobby.IntermissionVisibleOf(Hud.PhaseIntermission, 0), "intermission + wave 0 不显示", "显示了");
            SelfTest.True(Lobby.ResultsVisibleOf(Hud.PhaseEnded), "ended 显示结算", "没显示");
            var phases = new[] { Hud.PhaseLobby, Hud.PhaseLoading, Hud.PhasePlaying, Hud.PhaseIntermission, Hud.PhaseEnded };
            var noPhase = 0;
            var multiPhase = 0;
            for (var i = 0; i < phases.Length; i++)
            {
                var phase = phases[i];
                var wave = 2;
                var shown = 0;
                if (Lobby.LobbyVisibleOf(phase)) shown += 1;
                if (Lobby.LoadingVisibleOf(phase)) shown += 1;
                if (Lobby.HudVisibleOf(phase)) shown += 1;
                if (Lobby.IntermissionVisibleOf(phase, wave)) shown += 1;
                if (Lobby.ResultsVisibleOf(phase)) shown += 1;
                if (shown == 0) noPhase += 1;
                if (shown > 1) multiPhase += 1;
            }
            SelfTest.Equal(0, multiPhase);       // 每个相位最多一个界面
            SelfTest.Equal(0, noPhase);          // 每个相位至少一个界面

            var changes = 0;
            lobby.OnPhaseChanged += phase => { changes += 1; };
            lobby.Apply(State(Hud.PhaseLobby, 0, 0, Player(1, true)), 1);
            SelfTest.Equal(0, lobby.PhaseChangeCount);      // 初始就是 lobby：同相位不重入
            lobby.Apply(State(Hud.PhaseLoading, 0, 0, Player(1, true)), 1);
            SelfTest.Equal(1, lobby.PhaseChangeCount);
            lobby.Apply(State(Hud.PhaseLoading, 0, 0, Player(1, true)), 1);
            SelfTest.Equal(1, lobby.PhaseChangeCount);
            SelfTest.Equal(1, changes);
            lobby.Apply(State(Hud.PhasePlaying, 1, 0, Player(1, true), Player(2, false)), 1);
            SelfTest.True(lobby.HudVisible && !lobby.LobbyVisible, "切到 playing", "没切");
            SelfTest.Equal(2, (long)lobby.PlayerCount);
            SelfTest.Equal(1, (long)lobby.ReadyCount);
            SelfTest.True(!lobby.AllReady && lobby.IsHost, "主机判定", "错");
            SelfTest.True(!lobby.CanStart, "playing 时不能开始", "可以开始");
            lobby.Apply(State(Hud.PhaseLobby, 0, 0, Player(1, false), Player(2, false)), 1);
            SelfTest.True(lobby.CanStart && lobby.CanReady, "回大厅可准备可开始", "不可用");
            SelfTest.True(lobby.ResultsVisible == false && lobby.LoadingVisible == false, "其余界面都隐藏", "还有可见的");
        }

        private static void ChecksName()
        {
            SelfTest.Equal(1, (long)Lobby.NameMinBytes);
            SelfTest.Equal(12, (long)Lobby.NameMaxBytes);
            SelfTest.True(Lobby.SanitizeName("  牧羊人  ") == "牧羊人", "去首尾空白", Lobby.SanitizeName("  牧羊人  "));
            SelfTest.True(Lobby.SanitizeName("a<b>c&d\"e'f") == "abcdef", "剥离 <>&\"'", Lobby.SanitizeName("a<b>c&d\"e'f"));
            SelfTest.True(Lobby.SanitizeName("a\u0007b") == "ab", "剥离控制字符", Lobby.SanitizeName("a\u0007b"));
            SelfTest.True(Chat.Utf8Bytes(Lobby.SanitizeName(new string('羊', 10))) == 12, "12 字节截断（4 个汉字）", Lobby.SanitizeName(new string('羊', 10)));
            SelfTest.True(Lobby.SanitizeName("") == string.Empty, "空昵称", "非空");
            SelfTest.True(!Lobby.IsValidName(""), "空昵称非法", "判成合法");
            SelfTest.True(Lobby.IsValidName("羊"), "一个字合法（3 字节）", "判成非法");
            var lobby = new Lobby();
            lobby.SetName("  牧  <羊>  ");
            SelfTest.True(lobby.IsNameValid, "设置后有效", lobby.Name);
            SelfTest.True(lobby.Name.IndexOf('<') < 0, "落库前已经剥离", lobby.Name);
        }

        private static MatchRecord Record(string matchId, long startedAtMs, int kills, int players)
        {
            var record = default(MatchRecord);
            record.MatchId = matchId;
            record.StartedAtMs = startedAtMs;
            record.DurationMs = 60000;
            record.WaveReached = 5;
            record.WinnerTeam = 0;
            record.PlayerCount = players;
            record.Players = new PlayerRecord[players];
            for (var i = 0; i < players; i++)
            {
                record.Players[i].Name = "p" + i;
                record.Players[i].Kills = i == 0 ? kills : 0;
            }
            return record;
        }

        private static void ChecksResultsSort()
        {
            SelfTest.Equal(10, (long)Results.LeaderboardLimit);
            SelfTest.Equal(4, (long)Results.MaxPlayersPerMatch);
            SelfTest.Equal(64, (long)Results.MatchIdMaxLength);
            SelfTest.Equal(64, (long)Results.PlayerNameMaxBytes);
            var a = Record("m-a", 1000, 30, 2);
            var b = Record("m-b", 1000, 30, 1);
            var c = Record("m-c", 2000, 30, 1);
            var d = Record("m-d", 2000, 5, 1);
            SelfTest.Equal(30, Results.TotalKills(a));
            SelfTest.True(Results.IsOrderedBefore(c, b), "同击杀比 startedAtMs 降序", "判错");
            SelfTest.True(Results.IsOrderedBefore(a, b), "都相同则 matchId 升序（m-a 在前）", "判错");
            SelfTest.True(Results.IsOrderedBefore(a, d), "击杀多者在前", "判错");
            var sorted = Results.SortTop(new[] { b, d, a, c }, Results.LeaderboardLimit);
            SelfTest.True(sorted[0].MatchId == "m-c" && sorted[1].MatchId == "m-a" && sorted[2].MatchId == "m-b" && sorted[3].MatchId == "m-d", "四条完整顺序", sorted[0].MatchId + sorted[1].MatchId + sorted[2].MatchId + sorted[3].MatchId);
            var many = new MatchRecord[14];
            for (var i = 0; i < many.Length; i++) many[i] = Record("m-" + i.ToString("D2"), 1000 + i, i, 1);
            var top = Results.SortTop(many, Results.LeaderboardLimit);
            SelfTest.Equal(10, (long)top.Length);
            SelfTest.True(top[0].MatchId == "m-13", "榜长上限 10 且取最高击杀", top[0].MatchId);
            SelfTest.Equal(0, (long)Results.SortTop(null, 10).Length);
            var results = new Results();
            var summary = default(ResultsSummary);
            summary.MatchId = "m-1";
            summary.Records = many;
            results.Show(summary);
            SelfTest.True(results.Visible && results.Leaderboard.Count == 10, "渲染榜长 10", results.Leaderboard.Count.ToString());
            SelfTest.True(!results.StaleBanner, "有数据不显示兜底横幅", "显示了");
        }

        private static void ChecksResultsRetry()
        {
            SelfTest.Equal(400, (long)Results.RetryMs);
            SelfTest.Equal(3, (long)Results.RetryMax);
            var results = new Results();
            var summary = default(ResultsSummary);
            summary.MatchId = "m-local";
            summary.WaveReached = 7;
            summary.WinnerTeam = 1;
            summary.DurationMs = 90000;
            results.Show(summary);
            SelfTest.True(results.StaleBanner, "空榜单走本地摘要兜底", "没有兜底");
            SelfTest.True(results.Leaderboard.Count == 1 && results.Leaderboard[0].MatchId == "m-local", "摘要作为唯一一行", results.Leaderboard.Count.ToString());
            SelfTest.True(results.WaveReached == 7 && results.WinnerTeam == 1, "摘要字段可用", results.WaveReached.ToString());
            SelfTest.True(results.OnFetchFailed(), "第一次失败会重试", "不重试");
            SelfTest.True(results.RetryPending && results.RetryRemainingMs == 400f, "400ms 后重试", results.RetryRemainingMs.ToString("R"));
            results.Tick(399f);
            SelfTest.True(results.RetryPending && results.FetchRequestCount == 0, "没到 400ms 不重发", results.FetchRequestCount.ToString());
            results.Tick(1f);
            SelfTest.True(!results.RetryPending && results.FetchRequestCount == 1, "到点重发一次", results.FetchRequestCount.ToString());
            results.OnFetchFailed();
            results.Tick(400f);
            results.OnFetchFailed();
            results.Tick(400f);
            SelfTest.Equal(3, (long)results.FetchRequestCount);
            SelfTest.True(!results.OnFetchFailed(), "第三次之后不再重试", "还在重试");
            SelfTest.True(!results.RetryPending && results.StaleBanner, "停在本地摘要 + 榜单暂不可用", "状态错");
            SelfTest.True(results.Leaderboard.Count == 1, "兜底摘要还在", results.Leaderboard.Count.ToString());
            results.Hide();
            SelfTest.True(!results.Visible, "可隐藏（返回大厅）", "仍可见");
        }

        private static void ChecksResultsSchema()
        {
            SelfTest.True(Results.IsValidMatchId("m-1.a_b"), "合法 matchId", "判成非法");
            SelfTest.True(!Results.IsValidMatchId("m/1"), "斜杠非法", "判成合法");
            SelfTest.True(!Results.IsValidMatchId(""), "空 matchId 非法", "判成合法");
            SelfTest.True(!Results.IsValidMatchId(new string('x', 65)), "超 64 字符非法", "判成合法");
            SelfTest.True(Results.IsValid(Record("m-1", 1, 1, 4)), "4 人记录合法", "判成非法");
            var mismatch = Record("m-1", 1, 1, 3);
            mismatch.PlayerCount = 2;
            SelfTest.True(!Results.IsValid(mismatch), "playerCount 与 players 长度不符判非法", "判成合法");
            var badTeam = Record("m-1", 1, 1, 2);
            badTeam.WinnerTeam = 3;
            SelfTest.True(!Results.IsValid(badTeam), "winnerTeam 越界判非法", "判成合法");
            var noPlayers = Record("m-1", 1, 1, 1);
            noPlayers.Players = new PlayerRecord[0];
            noPlayers.PlayerCount = 0;
            SelfTest.True(!Results.IsValid(noPlayers), "空 players 判非法", "判成合法");
            SelfTest.True(!Results.IsValid(Record("m-1", 1, 1, 5)), "5 人记录非法", "判成合法");
            // 明细字段与 server/src/persist/match_store.hpp 的 PlayerResultRecord 逐字对应
            var detail = default(PlayerRecord);
            detail.Name = "p";
            detail.Kills = 1;
            detail.Headshots = 2;
            detail.ShotsFired = 3;
            detail.Hits = 4;
            detail.Revives = 5;
            detail.Downs = 6;
            detail.AliveMs = 7;
            detail.LeftMidMatch = true;
            SelfTest.Equal(7, (long)detail.AliveMs);
            SelfTest.True(detail.LeftMidMatch, "中途离开标记", "丢失");
            var record = Record("m-2", 12345, 3, 2);
            SelfTest.Equal(12345L, record.StartedAtMs);
            SelfTest.True(record.WaveReached == 5 && record.DurationMs == 60000 && record.WinnerTeam == 0, "榜单行字段可用", record.WaveReached.ToString());
            SelfTest.Equal(3, Results.TotalKills(record));
            SelfTest.Equal(2, (long)record.Players.Length);   // 2 名玩家 → 索引 0..1
        }

        private const int LobbySuitePhaseCount = 5;
    }
}
