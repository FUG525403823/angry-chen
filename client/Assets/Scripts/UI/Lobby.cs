using System;
using System.Text;
using Ac.Net;

namespace Ac.UI
{
    // C12 §4 任务 4 / §5 阶段表：界面显隐唯一真相是服务器下发的 phase。
    public sealed class Lobby
    {
        public const int NameMinBytes = 1;
        public const int NameMaxBytes = 12;

        public readonly RoomCodeInput RoomCode = new RoomCodeInput();
        public readonly WeaponPicker Weapon = new WeaponPicker();

        public string Name { get; private set; }
        public byte Phase { get; private set; }
        public int SelfPid { get; private set; }
        public int PlayerCount { get; private set; }
        public int ReadyCount { get; private set; }
        public int HostPid { get; private set; }
        public int PhaseChangeCount { get; private set; }

        public event Action<byte> OnPhaseChanged;
        // 清洗后的昵称变化：本地身份解析（过渡方案，见 Ac.Net.LocalIdentity）靠它去认领 MatchState 里的行。
        public event Action<string> OnNameChanged;

        public Lobby() { Name = string.Empty; Phase = Hud.PhaseLobby; }

        // 同名不重入：键入被清洗掉的字符（如 "<"）不该产生一次"变化"。
        public void SetName(string raw)
        {
            var sanitized = SanitizeName(raw);
            if (string.Equals(sanitized, Name, StringComparison.Ordinal)) return;
            Name = sanitized;
            var handler = OnNameChanged;
            if (handler != null) handler(Name);
        }

        public bool IsNameValid { get { return IsValidName(Name); } }

        // 昵称：剥离控制字符与 <>&"'，去掉首尾空白，再按 12 字节截断
        public static string SanitizeName(string raw)
        {
            if (string.IsNullOrEmpty(raw)) return string.Empty;
            var builder = new StringBuilder(raw.Length);
            for (var i = 0; i < raw.Length; i++)
            {
                var c = raw[i];
                if (char.IsControl(c)) continue;
                if (c == '<' || c == '>' || c == '&' || c == '"' || c == '\'') continue;
                builder.Append(c);
            }
            return Chat.TruncateUtf8(builder.ToString().Trim(), NameMaxBytes);
        }

        public static bool IsValidName(string sanitized)
        {
            var bytes = Chat.Utf8Bytes(sanitized);
            return bytes >= NameMinBytes && bytes <= NameMaxBytes;
        }

        public void Apply(in MatchStatePayload state, int selfPid)
        {
            SelfPid = selfPid;
            Wave = state.Wave;                       // 波间显隐依赖它，不能只在 SetWave 里更新
            PlayerCount = Roster.PlayerCount(state.Players);
            ReadyCount = Roster.ReadyCount(state.Players);
            HostPid = Roster.HostPid(state.Players);
            // 只在相位真的变了才切换界面：重复同相位不重入（§8 第一条风险）
            if (state.Phase == Phase) return;
            Phase = state.Phase;
            PhaseChangeCount += 1;
            var handler = OnPhaseChanged;
            if (handler != null) handler(Phase);
        }

        public static bool LobbyVisibleOf(byte phase) { return phase == Hud.PhaseLobby; }
        public static bool LoadingVisibleOf(byte phase) { return phase == Hud.PhaseLoading; }
        public static bool HudVisibleOf(byte phase) { return phase == Hud.PhasePlaying; }
        public static bool IntermissionVisibleOf(byte phase, int wave) { return phase == Hud.PhaseIntermission && wave >= 1; }
        public static bool ResultsVisibleOf(byte phase) { return phase == Hud.PhaseEnded; }

        public bool LobbyVisible { get { return LobbyVisibleOf(Phase); } }
        public bool LoadingVisible { get { return LoadingVisibleOf(Phase); } }
        public bool HudVisible { get { return HudVisibleOf(Phase); } }
        public bool IntermissionVisible { get { return IntermissionVisibleOf(Phase, Wave); } }
        public bool ResultsVisible { get { return ResultsVisibleOf(Phase); } }

        public int Wave { get; private set; }
        public bool IsHost { get { return HostPid > 0 && HostPid == SelfPid; } }
        public bool AllReady { get { return PlayerCount > 0 && ReadyCount == PlayerCount; } }
        public bool CanReady { get { return Phase == Hud.PhaseLobby && PlayerCount > 0; } }
        public bool CanStart { get { return Phase == Hud.PhaseLobby && IsHost && PlayerCount > 0; } }

        public void SetWave(int wave) { Wave = wave; }
        public bool SelectWeapon(int slot) { return Weapon.Select(slot); }
        public bool CanJoin { get { return RoomCode.CanJoin; } }
        public void SetRoomCode(string raw) { RoomCode.SetRaw(raw); }
    }
}
