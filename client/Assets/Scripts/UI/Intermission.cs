using System;
using Ac.Net;

namespace Ac.UI
{
    // C12 §5 波间规则：倒计时初值 20000ms、按服务器 intermissionMs 覆盖显示（不本地外推）、
    // intermissionMs > 15000 时禁用跳过、全员准备立即请求开波。
    public sealed class Intermission
    {
        public const int IntermissionInitialMs = 20000;
        public const int IntermissionMinMs = 5000;
        public const int SkipEnabledMaxMs = 15000;

        public int RemainingMs { get; private set; }
        public int Wave { get; private set; }
        public int SelfPid { get; private set; }
        public bool Visible { get; private set; }
        public bool SkipEnabled { get; private set; }
        public bool SelfReady { get; private set; }
        public bool StartRequested { get; private set; }
        public int ApplyCount { get; private set; }

        public readonly WeaponPicker Weapon = new WeaponPicker();

        public event Action<bool, int> OnReadyChanged;

        public Intermission() { RemainingMs = IntermissionInitialMs; }

        public void Apply(MatchStatePayload state, int selfPid)
        {
            ApplyCount += 1;
            SelfPid = selfPid;
            Wave = state.Wave;
            RemainingMs = state.IntermissionMs;
            Visible = state.Phase == Hud.PhaseIntermission && state.Wave >= 1;
            SkipEnabled = Visible && RemainingMs > 0 && RemainingMs <= SkipEnabledMaxMs;
            var self = Roster.Self(state.Players, selfPid);
            SelfReady = self.HasValue && self.Value.Ready;
            StartRequested = Visible && Roster.AllReady(state.Players);
        }

        public bool SetReady(bool ready)
        {
            if (ReadySet && SelfReady == ready) return false;   // 重复设置不重复回调
            ReadySet = true;
            SelfReady = ready;
            var handler = OnReadyChanged;
            if (handler != null) handler(ready, Weapon.Selected);
            return true;
        }

        // 请求开波：全员准备且人数 > 0（§5）
        public bool RequestStart(MatchStatePayload state)
        {
            if (!Visible) return false;              // 不在波间就不谈"立即开波"
            return Roster.AllReady(state.Players);
        }

        public bool ReadySet { get; private set; }

        public float RemainingSeconds { get { return RemainingMs / 1000f; } }
    }
}
