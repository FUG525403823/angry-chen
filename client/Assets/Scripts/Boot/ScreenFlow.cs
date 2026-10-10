using Ac.Net;
using Ac.UI;

namespace Ac.Boot
{
    // B1：大厅 → 对局 → 波间/结算的显隐。唯一真相是服务器下发的 MatchStatePayload.Phase
    // （Ac.Net，0 lobby / 1 loading / 2 playing / 3 intermission / 4 ended），本类不自己推相位。
    //
    // 四个界面对象此前在生产代码里**没有任何构造点**（Lobby/Results/Intermission 从未被实例化，
    // Roster 这个静态类从未被生产代码调用）。它们在这里被构造并接线。
    public sealed class ScreenFlow
    {
        public readonly Lobby Lobby = new Lobby();
        public readonly Results Results = new Results();
        public readonly Intermission Intermission = new Intermission();
        // S16：波间升级面板（points + 4 级，数据来自 MatchState 本地玩家行）。
        public readonly UpgradeModel Upgrades = new UpgradeModel();
        // 昵称键入捕获：GameLoopDriver 把本帧的 Input.inputString 交进来的落点（C12 §4）。
        public readonly NameInput NameInput = new NameInput();
        // 局内聊天（C12 §5）：规则与输入缓冲都在 Ac.UI.Chat 里；显隐由 GameLoop 按相位驱动，
        // 字符来源与昵称同一条通路（Input.inputString），但只在 playing 相位收。
        public readonly Chat Chat = new Chat();

        // 键入字符 → 昵称。只在大厅相位接收：对局里的按键属于 InputSampler（移动/开火），不是昵称。
        // 返回昵称是否真的变了；没有输入时不碰名字（也不分配）。
        public bool CaptureName(string typedThisFrame)
        {
            if (!LobbyVisible) return false;
            if (!NameInput.Feed(typedThisFrame)) return false;
            Lobby.SetName(NameInput.Text);
            return true;
        }

        public byte Phase { get; private set; }
        public int Wave { get; private set; }
        public int SelfPid { get; private set; }
        public int PlayerCount { get; private set; }
        public int ReadyCount { get; private set; }
        public int RosterRows { get; private set; }
        public int ApplyCount { get; private set; }

        public ScreenFlow() { Phase = Hud.PhaseLobby; }

        public void Apply(in MatchStatePayload state, int selfPid)
        {
            ApplyCount += 1;
            SelfPid = selfPid;
            Phase = state.Phase;
            Wave = state.Wave;
            PlayerCount = Roster.PlayerCount(state.Players);
            ReadyCount = Roster.ReadyCount(state.Players);
            RosterRows = Roster.VisibleRows(state.Players);
            // 三个界面的显隐都只由相位驱动（Lobby.Apply 还会带上 players 表算主机/准备数）
            Lobby.Apply(state, selfPid);
            Results.ApplyPhase(state.Phase);
            Intermission.Apply(state, selfPid);
            // S16：升级面板只认本地玩家行；波间之外或表里没有自己就隐藏。
            var self = Roster.Self(state.Players, selfPid);
            if (self.HasValue) Upgrades.Apply(self.Value);
            else Upgrades.Hide();
        }

        public bool LobbyVisible { get { return Lobby.LobbyVisible; } }
        public bool LoadingVisible { get { return Lobby.LoadingVisible; } }
        public bool PlayingVisible { get { return Lobby.HudVisible; } }
        public bool IntermissionVisible { get { return Intermission.Visible; } }
        public bool ResultsVisible { get { return Results.Visible; } }
        // 队伍表在"还没进对局 / 波间 / 结算"三种屏幕上都要看得见；对局中由 HUD 承担。
        public bool RosterVisible { get { return RosterRows > 0 && !Lobby.HudVisible; } }
    }
}
