using System;
using Ac.Net;

namespace Ac.UI
{
    // Read-only authority projection. Pending is transport intent, never a local grant.
    public sealed class ExpeditionHudModel
    {
        public ExpeditionStatePayload State { get; private set; }
        private double _elapsedMs;
        private uint _pendingId;
        public bool Visible { get { return State != null && State.Mode == 1; } }
        public bool RewardVisible { get { return Visible && State.Stage == 1; } }
        public bool CanChoose { get { return RewardVisible && State.OfferCount == 3 && State.Resolved == 0 && !Pending && RemainingSeconds > 0; } }
        public bool Pending { get { return _pendingId != 0; } }
        public byte LastResult { get { return State == null ? (byte)0 : State.LastResult; } }
        public uint ClockMs { get { return State == null ? 0 : State.ClockMs + (uint)_elapsedMs; } }
        public int RemainingSeconds { get { return State == null ? 0 : SecondsLeft(State.RewardDeadlineMs); } }
        public byte LocalCardMask { get; private set; }
        public uint ToolReadyAtMs { get; private set; }
        public int ToolSeconds
        {
            get
            {
                var clock = State != null && State.Stage == 2 ? ClockMs : State == null ? 0u : State.ClockMs;
                return ToolReadyAtMs > clock ? (int)((ToolReadyAtMs - clock + 999) / 1000) : 0;
            }
        }
        public void Reset() { State = null; _elapsedMs = 0; _pendingId = 0; LocalCardMask = 0; ToolReadyAtMs = 0; }
        public void BeginRequest(uint id) { if (id != 0) _pendingId = id; }
        public void Tick(double dtMs) { if (dtMs > 0) _elapsedMs += dtMs; }
        private int SecondsLeft(uint deadline) { return deadline > ClockMs ? (int)((deadline - ClockMs + 999) / 1000) : 0; }
        public bool Apply(ExpeditionStatePayload state)
        {
            if (state == null) return false;
            // Room run IDs are monotonic; a new identity/room explicitly calls Reset().
            if (State != null && state.RunId < State.RunId) return false;
            if (State != null && state.RunId == State.RunId
                && (state.Revision < State.Revision || (state.Revision == State.Revision && state.ClockMs < State.ClockMs))) return false;
            if (State != null && state.RunId != State.RunId) _pendingId = 0;
            State = state;
            _elapsedMs = 0;
            if (_pendingId != 0 && state.LastRequestId == _pendingId) _pendingId = 0;
            LocalCardMask = 0;
            ToolReadyAtMs = 0;
            if (state.Players != null)
                for (var i = 0; i < state.Players.Length; i++)
                    if (state.Players[i].Pid == state.LocalPid)
                    { LocalCardMask = state.Players[i].CardMask; ToolReadyAtMs = state.Players[i].ToolReadyAtMs; break; }
            return true;
        }
        public static string CardName(int id)
        {
            switch (id) { case 1: return "精准猎手 · 标记"; case 2: return "精准猎手 · 返弹"; case 3: return "精准猎手 · 凝神";
                case 4: return "破阵冲锋 · 打断"; case 5: return "破阵冲锋 · 续战"; case 6: return "破阵冲锋 · 节奏"; default: return "补给"; }
        }
        public static string CardTerms(int id)
        {
            switch (id)
            {
                case 1: return "头部命中标记3秒，后续命中标记目标×1.5\n代价：未标记身体×0.85；本击不享标记";
                case 2: return "头部返1发，冷却0.5秒、不超弹匣\n代价：身体×0.85；前置：标记";
                case 3: return "静止满1秒，头部×1.75\n代价：移动时×0.8；前置：标记";
                case 4: return "6米内命中打断0.5秒，同目标冷却2秒\n代价：6米外×0.8";
                case 5: return "6米内击杀回血8，冷却1秒\n代价：6米外×0.8；前置：打断";
                case 6: return "6米内击杀填满当前弹匣并取消换弹，冷却2秒\n代价：6米外×0.8；前置：打断";
                default: return "存活治疗25HP + 30备弹\n不复活，不自动选择机制卡";
            }
        }
        public static string ResultText(int result)
        {
            switch (result) { case 1: return "远征成功"; case 2: return "设施被摧毁"; case 3: return "净化超时"; case 4: return "全员倒地"; default: return "进行中"; }
        }
        public static string RequestResultText(int result)
        {
            switch (result) { case 1: return "已接受"; case 2: return "规则档不符"; case 3: return "局号不符"; case 4: return "阶段不符";
                case 5: return "候选已过期"; case 6: return "候选非法"; case 7: return "请求冲突"; case 8: return "当前不可用"; case 9: return "仅主机可用"; default: return ""; }
        }
    }
}
