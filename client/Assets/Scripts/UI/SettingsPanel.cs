using System;
using Ac.Core;

namespace Ac.UI
{
    // C13 §4 任务 4：控件取值/回写全部走 SettingsStore；任一改动立即应用，音量经 Ac.Core.AudioSeam 推给音频层。
    public sealed class SettingsPanel
    {
        private readonly SettingsStore _store;

        public SettingsPanel(SettingsStore store) { _store = store; }

        public bool Visible { get; private set; }
        public int ApplyCount { get; private set; }
        public int ConflictHintAction { get; private set; }
        public string Hint { get; private set; }

        public event Action<SettingsKey> OnChanged;

        public SettingsStore Store { get { return _store; } }

        public void Toggle() { Visible = !Visible; }

        public SettingsSnapshot Values { get { return _store.Get(); } }

        public static string QualityLabel(int tier)
        {
            if (tier <= 0) return "低";
            if (tier == 1) return "中";
            return "高";
        }

        public static string VolumeLabel(float value) { return (value * 100f).ToString("0") + "%"; }

        public static string KeyLabel(int action)
        {
            if (action < 0 || action >= SettingsDefaults.ActionCount) return "未知";
            return ActionNames[action];
        }

        public static readonly string[] ActionNames =
        {
            "前进", "后退", "左移", "右移", "疾跑", "跳跃", "开火",
            "换弹", "互动", "狂暴", "换武器", "聊天", "设置", "调试面板",
        };

        // 一次改动 = 走 store 一次 + 立即应用 + 通知界面
        public bool SetQualityTier(int tier) { return Notify(SettingsKey.QualityTier, _store.SetQualityTier(tier)); }
        public bool SetVolume(SettingsKey key, float value) { return Notify(key, _store.SetVolume(key, value)); }
        public bool SetSensitivity(float value) { return Notify(SettingsKey.Sensitivity, _store.SetSensitivity(value)); }
        public bool SetFov(float value) { return Notify(SettingsKey.Fov, _store.SetFov(value)); }
        public bool CycleCrosshairColor()
        {
            var colors = SettingsDefaults.CrosshairColors;
            var current = _store.Get().CrosshairColor;
            var next = colors[0];
            for (var i = 0; i < colors.Length; i++) if (colors[i] == current) next = colors[(i + 1) % colors.Length];
            return Notify(SettingsKey.CrosshairColor, _store.SetCrosshairColor(next));
        }
        public bool SetColorblindSafe(bool value) { return Notify(SettingsKey.ColorblindSafe, _store.SetColorblindSafe(value)); }
        public bool SetReduceMotion(bool value) { return Notify(SettingsKey.ReduceMotion, _store.SetReduceMotion(value)); }

        public bool Rebind(int action, string keyName)
        {
            var changed = _store.SetKeyBinding(action, keyName);
            ConflictHintAction = _store.LastConflictAction;
            Hint = ConflictHintAction >= 0 ? KeyLabel(ConflictHintAction) + " 已换键" : string.Empty;
            return Notify(SettingsKey.KeyBindings, changed);
        }

        public void RestoreDefaults()
        {
            _store.Reset();
            ApplyCount += 1;
            Hint = "已恢复默认";
            var handler = OnChanged;
            if (handler != null) handler(SettingsKey.QualityTier);
        }

        // 面板打开时把当前值刷一遍到音频与控制层
        public void ApplyAll()
        {
            _store.ApplyVolumesToAudio();
            ApplyCount += 1;
        }

        private bool Notify(SettingsKey key, bool changed)
        {
            if (!changed) return false;
            ApplyCount += 1;
            var handler = OnChanged;
            if (handler != null) handler(key);
            return true;
        }
    }
}
