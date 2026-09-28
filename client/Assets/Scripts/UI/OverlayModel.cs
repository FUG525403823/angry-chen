using Ac.Net;

namespace Ac.UI
{
    // 绘制项的种类：上屏的每一个像素都由这三种之一产生（批处理/无设备时一条都不产）。
    public enum OverlayItemKind : byte { Text = 0, Bar = 1, Crosshair = 2 }

    // 字号档：唯一真相是 Hud.FontPx(role)，本枚举只是它的四个名字。
    public enum OverlayTextRole : byte { Title = 0, Numeric = 1, Label = 2, Feed = 3 }

    public enum OverlayAlign : byte { Left = 0, Center = 1, Right = 2 }

    // 一条绘制项。全部是值 + 一个已经缓存好的字符串（帧内不拼串）。
    public struct OverlayItem
    {
        public OverlayItemKind Kind;
        public OverlayTextRole Role;
        public OverlayAlign Align;
        public int X;             // Align=Left/Right 时是左边缘；Center 时是中心线
        public int Y;             // 上边缘
        public int W;
        public int H;
        public int ColorRgb;
        public float Alpha;
        public float Fill01;      // Bar：0..1 的填充比例
        public float SpreadPx;    // Crosshair：四段十字的臂长（由散布线性映射，见 Crosshair.SizePx）
        public bool Target;       // Crosshair：压在可命中目标上
        public string Text;       // Text：已缓存的行；其余种类为 null
    }

    // 布局模型的输入：全是生产路径上**已经存在**的对象（唯一真相），本层不自己推相位、不自己算身份。
    public struct OverlaySources
    {
        public Hud Hud;
        public Lobby Lobby;
        public Intermission Intermission;
        public Results Results;
        public DebugPanel Debug;
        public MatchStatePlayer[] Players;
        public int SelfPid;
    }

    // C10/C12/C13 的**布局模型**：把 HUD 采样、屏幕流相位、准星状态、队伍表、波间倒计时、结算榜单与调试面板
    // 变成一组预分配的绘制项。本类不引用 UnityEngine 的 UI（Ac.UI 里只有 Hud 为了系统字体用了 UnityEngine），
    // 所以可以在无头自检里逐项断言；真正的 GUI 调用在 Ac.Boot.OverlayRenderer（薄适配层）里。
    //
    // 热路径纪律（FrameProfiler.ManagedAllocBudgetBytes = 0）：绘制项写进固定缓冲，字符串按显示值的脏检查
    // 缓存（上游 Hud 已经把数值类 10Hz 节流，这里再按"显示镜像"比较）——值不变时一次分配都没有。
    public sealed class OverlayModel
    {
        public const int ItemCapacity = 64;
        public const int RosterRowLimit = Roster.VisibleRowLimit;
        public const int ScoreRowLimit = 6;
        public const int FeedRowHeightPx = 20;
        public const int RosterRowHeightPx = 22;
        public const int DebugLineHeightPx = 18;
        public const int DebugPanelWidthPx = 560;
        public const int BarWidthPx = 220;
        public const int BarHeightPx = 14;
        public const int HpLowPercent = 30;
        public const float DownedShadeAlpha = 0.35f;
        public const float PanelShadeAlpha = 0.72f;
        public const int PanelShadeRgb = 0x101014;

        // 常量行：不随帧变化，构造期就冻住（帧内拼接 = 每帧一次分配）
        public const string LobbyTitle = "ANGRY CHEN";
        public const string LobbyHint = "键入字符修改昵称 · 服务器下发相位后自动进入对局";
        public const string RosterHeader = "队伍";
        public const string NameValidText = "昵称合法";
        public const string NameInvalidText = "昵称必须是 1..12 个字节（UTF-8）";
        public const string ReloadText = "换弹中";
        public const string DownedText = "倒地";
        public const string ChargeText = "冲锋警戒";
        public const string IntermissionTitle = "波间";
        public const string SkipEnabledText = "可跳过：全员准备立即开波";
        public const string SkipDisabledText = "不可跳过（剩余时间 > 15s）";
        public const string ResultsTitle = "结算";
        public const string StaleBannerText = "榜单暂不可用（本地摘要）";

        private readonly OverlayItem[] _items = new OverlayItem[ItemCapacity];
        private int _count;

        // ---- 缓存的字符串（只有真的变了才重拼） ----
        private string _nameCache;
        private string _nameLine = string.Empty;
        private bool _nameValidCache;
        private bool _nameValidSet;
        private string _nameStatusLine = string.Empty;
        private string _roomCodeCache;
        private string _roomLine = string.Empty;
        private int _readyCountCache = int.MinValue;
        private int _playerCountCache = int.MinValue;
        private bool _hostCache;
        private string _readyLine = string.Empty;
        private string _hpLine = string.Empty;
        private int _hpCache = int.MinValue;
        private string _ammoLine = string.Empty;
        private int _magCache = int.MinValue;
        private int _reserveCache = int.MinValue;
        private string _rageLine = string.Empty;
        private int _rageCache = int.MinValue;
        private bool _rageModeCache;
        private int _rageLeftCache = int.MinValue;
        private string _waveLine = string.Empty;
        private int _waveCache = int.MinValue;
        private string _bannerLine = string.Empty;
        private int _bannerWaveCache = int.MinValue;
        private string _reviveLine = string.Empty;
        private int _reviveCache = int.MinValue;
        private string _waveTitleLine = string.Empty;
        private int _waveTitleCache = int.MinValue;
        private string _countdownLine = string.Empty;
        private int _countdownCache = int.MinValue;
        private string _summaryLine = string.Empty;
        private int _reachedCache = int.MinValue;
        private int _winnerCache = int.MinValue;
        private string _retryLine = string.Empty;
        private int _retryCache = int.MinValue;
        private readonly string[] _rosterLines = new string[RosterRowLimit];
        private readonly MatchStatePlayer[] _rosterCache = new MatchStatePlayer[RosterRowLimit];
        private readonly string[] _feedLines = new string[KillFeed.Capacity];
        private readonly int[] _feedVictim = new int[KillFeed.Capacity];
        private readonly bool[] _feedHeadshot = new bool[KillFeed.Capacity];
        private readonly int[] _feedWave = new int[KillFeed.Capacity];
        private readonly string[] _scoreLines = new string[ScoreRowLimit];
        private readonly string[] _scoreCache = new string[ScoreRowLimit];
        private readonly int[] _scoreKills = new int[ScoreRowLimit];

        public OverlayItem[] Items { get { return _items; } }
        public int Count { get { return _count; } }
        public OverlayItem ItemAt(int index) { return _items[index]; }

        public static string RoleName(OverlayTextRole role)
        {
            if (role == OverlayTextRole.Title) return "title";
            if (role == OverlayTextRole.Label) return "label";
            if (role == OverlayTextRole.Feed) return "feed";
            return "numeric";                     // Hud.FontPx 的兜底档就是主数值
        }

        // 准星的显示色：压在目标上/受伤用调色板的固定档，其余用**用户设置的那一档**
        //（crosshairColor / colorblindSafe 的行为读者；色盲档已由 Crosshair.SetPalette 选过对比最大的一档）。
        public static int CrosshairColor(Crosshair crosshair)
        {
            if (crosshair.State == CrosshairState.Target) return Hud.ColorTarget;
            if (crosshair.State == CrosshairState.Hurt) return Hud.ColorHurt;
            return crosshair.ColorRgb;
        }

        // 产出一帧的绘制项，返回条数（≤ ItemCapacity）。没有视口（= 没有显示设备）就一条都不产。
        public int Build(in OverlaySources sources, int viewportWidthPx, int viewportHeightPx)
        {
            _count = 0;
            var hud = sources.Hud;
            var lobby = sources.Lobby;
            if (hud == null || lobby == null || viewportWidthPx <= 0 || viewportHeightPx <= 0) return 0;

            var inset = (int)Hud.SafeAreaInsetPx(viewportHeightPx);
            if (lobby.LobbyVisible) BuildLobby(sources, viewportWidthPx, viewportHeightPx, inset);
            else if (lobby.LoadingVisible) BuildLoading(viewportWidthPx, viewportHeightPx);
            else if (lobby.HudVisible) BuildCombat(sources, viewportWidthPx, viewportHeightPx, inset);
            else if (lobby.IntermissionVisible) BuildIntermission(sources, viewportWidthPx, viewportHeightPx, inset);
            else if (lobby.ResultsVisible) BuildResults(sources, viewportWidthPx, viewportHeightPx, inset);

            // 调试面板（C13 §5）与相位无关：F3 开着就叠在最上层，默认关着。
            if (sources.Debug != null && sources.Debug.Visible) BuildDebug(sources.Debug, inset);
            return _count;
        }

        // ---- 各屏幕 ----

        private void BuildLobby(in OverlaySources sources, int w, int h, int inset)
        {
            var lobby = sources.Lobby;
            var y = inset;
            AddText(OverlayTextRole.Title, OverlayAlign.Center, w / 2, y, w, Hud.ColorNormal, 1f, LobbyTitle);
            y += Hud.FontTitlePx + 12;

            // 昵称只**显示**：输入的唯一通路是 GameLoopDriver → ScreenFlow.CaptureName → Ac.UI.NameInput，
            // 这里再挂一个 IMGUI TextField 就是第二条平行输入源（会双重输入）。
            var name = lobby.Name;
            if (!ReferenceEquals(name, _nameCache))
            {
                _nameCache = name;
                _nameLine = "昵称: " + (string.IsNullOrEmpty(name) ? "(未设置)" : name);
            }
            AddText(OverlayTextRole.Numeric, OverlayAlign.Left, inset, y, w - inset, Hud.ColorNormal, 1f, _nameLine);
            y += Hud.FontNumericPx + 4;

            var valid = lobby.IsNameValid;
            if (!_nameValidSet || valid != _nameValidCache)
            {
                _nameValidSet = true;
                _nameValidCache = valid;
                _nameStatusLine = valid ? NameValidText : NameInvalidText;
            }
            AddText(OverlayTextRole.Label, OverlayAlign.Left, inset, y, w - inset,
                valid ? Hud.ColorNormal : Hud.ColorHurt, 1f, _nameStatusLine);
            y += Hud.FontLabelPx + 16;

            var code = lobby.RoomCode == null ? null : lobby.RoomCode.Code;
            if (!ReferenceEquals(code, _roomCodeCache))
            {
                _roomCodeCache = code;
                _roomLine = "房间码: " + (string.IsNullOrEmpty(code) ? "(未输入)" : code);
            }
            AddText(OverlayTextRole.Label, OverlayAlign.Left, inset, y, w - inset, Hud.ColorNormal, 1f, _roomLine);
            y += Hud.FontLabelPx + 4;

            AddText(OverlayTextRole.Label, OverlayAlign.Left, inset, y, w - inset, Hud.ColorNormal, 1f, ReadyLine(lobby));
            y += Hud.FontLabelPx + 6;

            // 备战条：已准备人数 / 总人数
            var total = lobby.PlayerCount;
            var fill = total <= 0 ? 0f : lobby.ReadyCount / (float)total;
            AddBar(inset, y, BarWidthPx, 10, Hud.ColorRage, 1f, fill);
            y += 10 + 14;

            AddText(OverlayTextRole.Label, OverlayAlign.Left, inset, y, w - inset, Hud.ColorNormal, 0.8f, LobbyHint);
            y += Hud.FontLabelPx + 16;

            if (Roster.VisibleRows(sources.Players) > 0)
            {
                AddText(OverlayTextRole.Label, OverlayAlign.Left, inset, y, w - inset, Hud.ColorTarget, 1f, RosterHeader);
                y += Hud.FontLabelPx + 2;
                BuildRosterRows(sources, inset, y, w - inset);
            }
        }

        private void BuildLoading(int w, int h)
        {
            AddText(OverlayTextRole.Title, OverlayAlign.Center, w / 2, h / 2 - Hud.FontTitlePx, w, Hud.ColorNormal, 1f, "载入中");
        }

        private void BuildCombat(in OverlaySources sources, int w, int h, int inset)
        {
            var hud = sources.Hud;
            var bottom = h - inset;

            var wave = hud.DisplayedWave;
            if (wave < 0) wave = 0;
            if (wave != _waveCache)
            {
                _waveCache = wave;
                _waveLine = "波次 " + wave;
            }
            if (wave >= 1) AddText(OverlayTextRole.Numeric, OverlayAlign.Center, w / 2, inset, w, Hud.ColorNormal, 1f, _waveLine);

            // 波次横幅（4500ms / 队列 3 由 WaveBanner 自己管，这里只读它当前该显示的那一波）
            var banner = hud.Banner;
            if (banner != null && banner.RemainingMs > 0f)
            {
                if (banner.CurrentWave != _bannerWaveCache)
                {
                    _bannerWaveCache = banner.CurrentWave;
                    _bannerLine = WaveBanner.IsBossWave(banner.CurrentWave)
                        ? "第 " + banner.CurrentWave + " 波 · BOSS"
                        : "第 " + banner.CurrentWave + " 波";
                }
                AddText(OverlayTextRole.Title, OverlayAlign.Center, w / 2, inset + Hud.FontNumericPx + 10, w, Hud.ColorTarget, 1f, _bannerLine);
            }

            // 血条 + 血量（显示值来自 Hud 的 10Hz 镜像）
            var hp = hud.DisplayedHp;
            if (hp < 0) hp = 0;
            if (hp > 100) hp = 100;
            if (hp != _hpCache)
            {
                _hpCache = hp;
                _hpLine = "HP " + hp;
            }
            var hpColor = hp <= HpLowPercent ? Hud.ColorHurt : Hud.ColorNormal;
            AddText(OverlayTextRole.Numeric, OverlayAlign.Left, inset, bottom - Hud.FontNumericPx - 40, 260, hpColor, 1f, _hpLine);
            AddBar(inset, bottom - 34, BarWidthPx, BarHeightPx, hpColor, 1f, hp / 100f);

            // 弹药 / 备弹（低弹色由 AmmoCounter 按该武器的弹匣容量判）
            var ammo = hud.Ammo;
            if (ammo.Mag != _magCache || ammo.Reserve != _reserveCache)
            {
                _magCache = ammo.Mag;
                _reserveCache = ammo.Reserve;
                _ammoLine = ammo.Mag + " / " + ammo.Reserve;
            }
            AddText(OverlayTextRole.Numeric, OverlayAlign.Right, w - inset, bottom - Hud.FontNumericPx - 40, 320, ammo.Color, 1f, _ammoLine);
            if (ammo.Reloading) AddText(OverlayTextRole.Label, OverlayAlign.Right, w - inset, bottom - 22, 320, Hud.ColorNormal, 1f, ReloadText);

            // 怒气条 / 狂暴倒计时
            var rage = hud.Rage;
            var rageTenths = (int)(rage.RageLeftMs / 100f);
            if (rage.Rage != _rageCache || rage.RageMode != _rageModeCache || rageTenths != _rageLeftCache)
            {
                _rageCache = rage.Rage;
                _rageModeCache = rage.RageMode;
                _rageLeftCache = rageTenths;
                _rageLine = rage.RageMode ? "狂暴 " + DebugPanel.Fmt1(rage.RageLeftMs) + " s" : "怒气 " + rage.Rage;
            }
            AddText(OverlayTextRole.Label, OverlayAlign.Left, inset, bottom - Hud.FontLabelPx - 26, 260, rage.Color, 1f, _rageLine);
            AddBar(inset, bottom - 12, 140, 10, rage.Color, 1f, rage.Fill01);

            // 准星（四段十字 + 扩散），中心固定在视口中心
            var crosshair = hud.Crosshair;
            if (crosshair != null && crosshair.Visible)
            {
                AddCrosshair(w / 2, h / 2, crosshair.SizePx, CrosshairColor(crosshair), crosshair.State == CrosshairState.Target);
            }

            // 击杀记录：右上角往下排，渐隐与爆头色都取自 KillFeed
            var feed = hud.Feed;
            for (var i = 0; i < KillFeed.Capacity; i++)
            {
                KillEntry entry;
                if (!hud.TryGetKill(i, out entry)) continue;
                if (entry.VictimId != _feedVictim[i] || entry.Headshot != _feedHeadshot[i] || entry.Wave != _feedWave[i])
                {
                    _feedVictim[i] = entry.VictimId;
                    _feedHeadshot[i] = entry.Headshot;
                    _feedWave[i] = entry.Wave;
                    _feedLines[i] = entry.Headshot ? "击杀 #" + entry.VictimId + " 爆头" : "击杀 #" + entry.VictimId;
                }
                AddText(OverlayTextRole.Feed, OverlayAlign.Right, w - inset, inset + i * FeedRowHeightPx, w / 2,
                    feed.ColorOf(i), feed.AlphaOf(i), _feedLines[i]);
            }

            // 倒地遮罩 + 救援提示 + 冲锋警戒
            if (hud.Downed != null && hud.Downed.Visible)
            {
                AddBar(0, 0, w, h, Hud.ColorHurt, DownedShadeAlpha, 1f);
                AddText(OverlayTextRole.Title, OverlayAlign.Center, w / 2, h / 2 - Hud.FontTitlePx / 2, w, Hud.ColorNormal, 1f, DownedText);
            }
            if (hud.Revive != null && hud.Revive.Visible)
            {
                var percent = (int)(hud.Revive.Progress * 100f + 0.5f);
                if (percent != _reviveCache)
                {
                    _reviveCache = percent;
                    _reviveLine = "救援中 " + percent + "%";
                }
                AddText(OverlayTextRole.Label, OverlayAlign.Center, w / 2, bottom - Hud.FontLabelPx - 40, w, Hud.ColorNormal, 1f, _reviveLine);
                AddBar(w / 2 - BarWidthPx / 2, bottom - 30, BarWidthPx, 10, Hud.ColorRage, 1f, hud.Revive.Progress);
            }
            if (hud.ChargeWarningRemainingMs > 0f)
            {
                AddText(OverlayTextRole.Numeric, OverlayAlign.Center, w / 2, (int)(h * 0.35f), w, Hud.ColorHurt, 1f, ChargeText);
            }
        }

        private void BuildIntermission(in OverlaySources sources, int w, int h, int inset)
        {
            var intermission = sources.Intermission;
            var lobby = sources.Lobby;
            var y = inset;
            AddText(OverlayTextRole.Title, OverlayAlign.Center, w / 2, y, w, Hud.ColorNormal, 1f, IntermissionTitle);
            y += Hud.FontTitlePx + 12;

            // 倒计时只显示服务器下发的权威值（§5：不本地外推），按 100ms 取整后缓存
            var ms = intermission == null ? 0 : intermission.RemainingMs;
            var tenths = ms < 0 ? 0 : ms / 100;
            if (tenths != _countdownCache)
            {
                _countdownCache = tenths;
                _countdownLine = (tenths / 10) + "." + (tenths % 10) + " s";
            }
            AddText(OverlayTextRole.Numeric, OverlayAlign.Center, w / 2, y, w, Hud.ColorTarget, 1f, _countdownLine);
            y += Hud.FontNumericPx + 8;

            var fill = ms <= 0 ? 0f : (ms >= Intermission.IntermissionInitialMs ? 1f : ms / (float)Intermission.IntermissionInitialMs);
            AddBar(w / 2 - BarWidthPx / 2, y, BarWidthPx, BarHeightPx, Hud.ColorRage, 1f, fill);
            y += BarHeightPx + 12;

            var wave = lobby.Wave;
            if (wave != _waveTitleCache)
            {
                _waveTitleCache = wave;
                _waveTitleLine = "第 " + wave + " 波准备中";
            }
            AddText(OverlayTextRole.Label, OverlayAlign.Center, w / 2, y, w, Hud.ColorNormal, 1f, _waveTitleLine);
            y += Hud.FontLabelPx + 8;

            AddText(OverlayTextRole.Label, OverlayAlign.Center, w / 2, y, w, Hud.ColorNormal, 1f, ReadyLine(lobby));
            y += Hud.FontLabelPx + 6;
            var total = lobby.PlayerCount;
            AddBar(w / 2 - BarWidthPx / 2, y, BarWidthPx, 10, Hud.ColorRage, 1f, total <= 0 ? 0f : lobby.ReadyCount / (float)total);
            y += 10 + 14;

            var skip = intermission != null && intermission.SkipEnabled;
            AddText(OverlayTextRole.Label, OverlayAlign.Center, w / 2, y, w, skip ? Hud.ColorNormal : Hud.ColorHurt, 1f,
                skip ? SkipEnabledText : SkipDisabledText);
            y += Hud.FontLabelPx + 16;

            BuildRosterRows(sources, inset, y, w - inset);
        }

        private void BuildResults(in OverlaySources sources, int w, int h, int inset)
        {
            var results = sources.Results;
            var y = inset;
            AddText(OverlayTextRole.Title, OverlayAlign.Center, w / 2, y, w, Hud.ColorNormal, 1f, ResultsTitle);
            y += Hud.FontTitlePx + 12;

            if (results != null)
            {
                if (results.WaveReached != _reachedCache || results.WinnerTeam != _winnerCache)
                {
                    _reachedCache = results.WaveReached;
                    _winnerCache = results.WinnerTeam;
                    _summaryLine = "到达波次 " + results.WaveReached + " · 胜方 队伍 " + results.WinnerTeam;
                }
                AddText(OverlayTextRole.Numeric, OverlayAlign.Center, w / 2, y, w, Hud.ColorTarget, 1f, _summaryLine);
                y += Hud.FontNumericPx + 8;

                if (results.StaleBanner)
                {
                    AddText(OverlayTextRole.Label, OverlayAlign.Center, w / 2, y, w, Hud.ColorHurt, 1f, StaleBannerText);
                    y += Hud.FontLabelPx + 4;
                }
                if (results.RetryPending)
                {
                    if (results.RetryCount != _retryCache)
                    {
                        _retryCache = results.RetryCount;
                        _retryLine = "重试中 " + results.RetryCount + "/" + Results.RetryMax;
                    }
                    AddText(OverlayTextRole.Label, OverlayAlign.Center, w / 2, y, w, Hud.ColorNormal, 1f, _retryLine);
                    y += Hud.FontLabelPx + 4;
                }
                y += 8;

                // 名次表：排序已经在 Results.Show/SortTop 里做完了（服务端口径），这里只画前几行
                var rows = results.Leaderboard;
                var total = rows == null ? 0 : rows.Count;
                if (total > ScoreRowLimit) total = ScoreRowLimit;
                for (var i = 0; i < total; i++)
                {
                    var record = rows[i];
                    var kills = (int)Results.TotalKills(record);
                    if (!string.Equals(record.MatchId, _scoreCache[i], System.StringComparison.Ordinal) || kills != _scoreKills[i])
                    {
                        _scoreCache[i] = record.MatchId;
                        _scoreKills[i] = kills;
                        _scoreLines[i] = (i + 1) + ". " + record.MatchId + " · 击杀 " + kills + " · 波 " + record.WaveReached;
                    }
                    AddText(OverlayTextRole.Feed, OverlayAlign.Left, inset, y + i * RosterRowHeightPx, w - inset, Hud.ColorNormal, 1f, _scoreLines[i]);
                }
                y += total * RosterRowHeightPx + 12;
            }

            BuildRosterRows(sources, inset, y, w - inset);
        }

        // 调试面板（C13 §5）：12 行信息行的字符串由 DebugPanel 自己按 250ms 节拍缓存，这里只引用。
        private void BuildDebug(DebugPanel panel, int inset)
        {
            AddBar(inset, inset, DebugPanelWidthPx, DebugPanel.FieldCount * DebugLineHeightPx + 8, PanelShadeRgb, PanelShadeAlpha, 1f);
            var lines = panel.Lines;
            for (var i = 0; i < DebugPanel.FieldCount; i++)
            {
                var text = lines == null ? null : lines[i];
                if (string.IsNullOrEmpty(text)) continue;      // 还没到 250ms 刷新点的行不画
                AddText(OverlayTextRole.Feed, OverlayAlign.Left, inset + 6, inset + 4 + i * DebugLineHeightPx,
                    DebugPanelWidthPx - 12, Hud.ColorNormal, 1f, text);
            }
        }

        private int BuildRosterRows(in OverlaySources sources, int x, int y, int width)
        {
            var rows = Roster.VisibleRows(sources.Players);
            var written = 0;
            for (var row = 0; row < rows && row < RosterRowLimit; row++)
            {
                MatchStatePlayer player;
                if (!VisiblePlayerAt(sources.Players, row, out player)) break;
                if (!SameRow(in _rosterCache[row], in player))
                {
                    _rosterCache[row] = player;
                    _rosterLines[row] = "#" + player.Pid + " "
                        + (string.IsNullOrEmpty(player.Name) ? "(无名)" : player.Name) + " "
                        + Roster.WeaponLabel(player.Weapon) + " " + Roster.StatusLabel(in player);
                }
                AddText(OverlayTextRole.Feed, OverlayAlign.Left, x, y + row * RosterRowHeightPx, width, Hud.ColorNormal, 1f, _rosterLines[row]);
                written += 1;
            }
            return written;
        }

        private string ReadyLine(Lobby lobby)
        {
            if (lobby.ReadyCount != _readyCountCache || lobby.PlayerCount != _playerCountCache || lobby.IsHost != _hostCache)
            {
                _readyCountCache = lobby.ReadyCount;
                _playerCountCache = lobby.PlayerCount;
                _hostCache = lobby.IsHost;
                _readyLine = "准备 " + lobby.ReadyCount + "/" + lobby.PlayerCount + (lobby.IsHost ? " · 主机" : string.Empty);
            }
            return _readyLine;
        }

        // pid <= 0 的行整行不渲染（Roster 的冻结规则）：取第 index 个可见行。
        private static bool VisiblePlayerAt(MatchStatePlayer[] players, int index, out MatchStatePlayer player)
        {
            player = default(MatchStatePlayer);
            if (players == null) return false;
            var seen = 0;
            for (var i = 0; i < players.Length; i++)
            {
                if (players[i].Pid <= 0) continue;
                if (seen == index) { player = players[i]; return true; }
                seen += 1;
            }
            return false;
        }

        private static bool SameRow(in MatchStatePlayer a, in MatchStatePlayer b)
        {
            return a.Pid == b.Pid && a.Ready == b.Ready && a.Weapon == b.Weapon && a.Downed == b.Downed
                && string.Equals(a.Name, b.Name, System.StringComparison.Ordinal);
        }

        // ---- 绘制项的写入（固定缓冲，满了就不写） ----

        private void AddText(OverlayTextRole role, OverlayAlign align, int x, int y, int width, int colorRgb, float alpha, string text)
        {
            if (text == null || _count >= ItemCapacity) return;
            var item = default(OverlayItem);
            item.Kind = OverlayItemKind.Text;
            item.Role = role;
            item.Align = align;
            item.X = x;
            item.Y = y;
            item.W = width;
            item.H = Hud.FontPx(RoleName(role)) + 6;
            item.ColorRgb = colorRgb;
            item.Alpha = alpha;
            item.Text = text;
            _items[_count] = item;
            _count += 1;
        }

        private void AddBar(int x, int y, int width, int height, int colorRgb, float alpha, float fill01)
        {
            if (_count >= ItemCapacity) return;
            var item = default(OverlayItem);
            item.Kind = OverlayItemKind.Bar;
            item.X = x;
            item.Y = y;
            item.W = width;
            item.H = height;
            item.ColorRgb = colorRgb;
            item.Alpha = alpha;
            item.Fill01 = fill01 < 0f ? 0f : (fill01 > 1f ? 1f : fill01);
            _items[_count] = item;
            _count += 1;
        }

        private void AddCrosshair(int x, int y, float spreadPx, int colorRgb, bool target)
        {
            if (_count >= ItemCapacity) return;
            var item = default(OverlayItem);
            item.Kind = OverlayItemKind.Crosshair;
            item.Align = OverlayAlign.Center;
            item.X = x;
            item.Y = y;
            item.ColorRgb = colorRgb;
            item.Alpha = 1f;
            item.SpreadPx = spreadPx;
            item.Target = target;
            _items[_count] = item;
            _count += 1;
        }
    }
}
