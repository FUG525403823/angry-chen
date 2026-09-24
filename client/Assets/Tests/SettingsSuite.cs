using System;
using System.IO;
using Ac.Audio;
using Ac.Core;
using Ac.UI;

namespace Ac.Tests
{
    // C13 §6/§7：默认值全表、clamp 边界、迁移、坏 JSON、未知键、原子写、键位冲突、音量推音频、面板字段顺序。
    public static class SettingsSuite
    {
        private static string TempDir()
        {
            var dir = Path.Combine(Path.GetTempPath(), "ac-settings-test");
            Directory.CreateDirectory(dir);
            return dir;
        }

        private static void Clean(string dir)
        {
            foreach (var name in new[] { SettingsStore.FileName, SettingsStore.FileName + ".tmp", SettingsStore.BadName })
            {
                var path = Path.Combine(dir, name);
                if (File.Exists(path)) File.Delete(path);
            }
        }

        public static void Register()
        {
            SelfTest.Add("settings.defaults", ChecksDefaults);
            SelfTest.Add("settings.clamp", ChecksClamp);
            SelfTest.Add("settings.serialize", ChecksSerialize);
            SelfTest.Add("settings.roundtrip", ChecksRoundtrip);
            SelfTest.Add("settings.migration_v1", ChecksMigration);
            SelfTest.Add("settings.future_version", ChecksFutureVersion);
            SelfTest.Add("settings.bad_json", ChecksBadJson);
            SelfTest.Add("settings.unknown_keys", ChecksUnknownKeys);
            SelfTest.Add("settings.atomic_write", ChecksAtomicWrite);
            SelfTest.Add("settings.keybind_conflict", ChecksKeyConflict);
            SelfTest.Add("settings.audio_push", ChecksAudioPush);
            SelfTest.Add("settings.debug_panel", ChecksDebugPanel);
        }

        private static void ChecksDefaults()
        {
            var snapshot = SettingsDefaults.Default();
            SelfTest.Equal(2, (long)snapshot.SchemaVersion);
            SelfTest.Equal(2, (long)snapshot.QualityTier);
            SelfTest.True(snapshot.MasterVolume == 0.80f, "masterVolume 默认 0.80", snapshot.MasterVolume.ToString("R"));
            SelfTest.True(snapshot.SfxVolume == 0.80f, "sfxVolume 默认 0.80", snapshot.SfxVolume.ToString("R"));
            SelfTest.True(snapshot.MusicVolume == 0.50f, "musicVolume 默认 0.50", snapshot.MusicVolume.ToString("R"));
            SelfTest.True(snapshot.Sensitivity == 1.00f, "sensitivity 默认 1.00", snapshot.Sensitivity.ToString("R"));
            SelfTest.True(snapshot.Fov == 75f, "fov 默认 75", snapshot.Fov.ToString("R"));
            SelfTest.Equal(0x00FF66, (long)snapshot.CrosshairColor);
            SelfTest.True(!snapshot.ColorblindSafe && !snapshot.ReduceMotion, "两个开关默认 false", "默认 true");
            SelfTest.Equal(14, (long)snapshot.KeyBindings.Length);
            SelfTest.True(snapshot.KeyBindings[0] == "W" && snapshot.KeyBindings[1] == "S" && snapshot.KeyBindings[2] == "A" && snapshot.KeyBindings[3] == "D", "前后左右 = W/S/A/D", snapshot.KeyBindings[0]);
            SelfTest.True(snapshot.KeyBindings[4] == "LeftShift" && snapshot.KeyBindings[5] == "Space" && snapshot.KeyBindings[6] == "Mouse0", "疾跑/跳跃/开火", snapshot.KeyBindings[6]);
            SelfTest.True(snapshot.KeyBindings[7] == "R" && snapshot.KeyBindings[8] == "E" && snapshot.KeyBindings[9] == "F", "换弹/互动/狂暴", snapshot.KeyBindings[9]);
            SelfTest.True(snapshot.KeyBindings[10] == "Q" && snapshot.KeyBindings[11] == "Return" && snapshot.KeyBindings[12] == "O" && snapshot.KeyBindings[13] == "F3", "换武器/聊天/设置/调试", snapshot.KeyBindings[13]);
            SelfTest.Equal(4, (long)SettingsDefaults.CrosshairColors.Length);
            SelfTest.True(SettingsDefaults.IsAllowedCrosshairColor(0xFF66CC), "调色板含 0xFF66CC", "不含");
            // 快照不可被外部改动影响默认值
            var clone = SettingsDefaults.Default();
            clone.KeyBindings[0] = "X";
            SelfTest.True(SettingsDefaults.Default().KeyBindings[0] == "W", "每次 Default() 都是独立数组", "被共享");
        }

        private static void ChecksClamp()
        {
            var store = new SettingsStore();
            store.SetSensitivity(0.19f);
            SelfTest.True(store.Get().Sensitivity == 0.20f, "0.19 → 0.20", store.Get().Sensitivity.ToString("R"));
            store.SetSensitivity(3.01f);
            SelfTest.True(store.Get().Sensitivity == 3.00f, "3.01 → 3.00", store.Get().Sensitivity.ToString("R"));
            store.SetSensitivity(1.5f);
            SelfTest.True(store.Get().Sensitivity == 1.5f, "范围内原样", store.Get().Sensitivity.ToString("R"));
            store.SetFov(59f);
            SelfTest.True(store.Get().Fov == 60f, "59 → 60", store.Get().Fov.ToString("R"));
            store.SetFov(101f);
            SelfTest.True(store.Get().Fov == 100f, "101 → 100", store.Get().Fov.ToString("R"));
            store.SetFov(90f);
            SelfTest.True(store.Get().Fov == 90f, "范围内原样（fov）", store.Get().Fov.ToString("R"));
            store.SetVolume(SettingsKey.MasterVolume, -0.5f);
            SelfTest.True(store.Get().MasterVolume == 0f, "音量下界 0", store.Get().MasterVolume.ToString("R"));
            store.SetVolume(SettingsKey.SfxVolume, 1.5f);
            SelfTest.True(store.Get().SfxVolume == 1f, "音量上界 1", store.Get().SfxVolume.ToString("R"));
            store.SetQualityTier(-1);
            SelfTest.Equal(0, (long)store.Get().QualityTier);
            store.SetQualityTier(5);
            SelfTest.Equal(2, (long)store.Get().QualityTier);
            store.SetCrosshairColor(0x123456);
            SelfTest.True(SettingsDefaults.IsAllowedCrosshairColor(store.Get().CrosshairColor), "非调色板颜色吸附到最近的允许值", store.Get().CrosshairColor.ToString("X6"));
            SelfTest.True(!store.SetVolume(SettingsKey.Sensitivity, 1f), "音量接口不接受非音量键", "接受了");
            var changes = 0;
            store.Changed += key => { changes += 1; };
            store.SetQualityTier(1);
            SelfTest.Equal(1, changes);
            store.SetQualityTier(1);
            SelfTest.Equal(1, changes);      // 同值不重复触发
        }

        private static void ChecksSerialize()
        {
            var text = SettingsStore.Serialize(SettingsDefaults.Default());
            var lines = text.Split('\n');
            SelfTest.True(lines[0].Trim() == "{", "首行是左花括号", lines[0]);
            SelfTest.True(lines[1] == "  \"schemaVersion\": 2,", "缩进 2 空格且 schemaVersion 在最前", lines[1]);
            SelfTest.True(text.IndexOf("\"qualityTier\": 2") > 0, "camelCase 键名", "缺 qualityTier");
            SelfTest.True(text.IndexOf("\"masterVolume\": 0.80") > 0, "浮点两位小数", "格式不符");
            SelfTest.True(text.IndexOf("\"colorblindSafe\": false") > 0, "布尔字面量", "格式不符");
            SelfTest.True(text.IndexOf("\"keyBindings\": [\"W\", \"S\"") > 0, "keyBindings 是字符串数组", "格式不符");
            var keys = new[] { "schemaVersion", "qualityTier", "masterVolume", "sfxVolume", "musicVolume", "sensitivity", "fov", "crosshairColor", "colorblindSafe", "reduceMotion", "keyBindings" };
            var missing = 0;
            for (var i = 0; i < keys.Length; i++) if (text.IndexOf("\"" + keys[i] + "\"", StringComparison.Ordinal) < 0) missing += 1;
            SelfTest.Equal(0, (long)missing);
            SelfTest.True(text.TrimEnd().EndsWith("}"), "结尾是右花括号", "缺");
        }

        private static void ChecksRoundtrip()
        {
            var original = SettingsDefaults.Default();
            original.QualityTier = 1;
            original.Sensitivity = 2.35f;
            original.Fov = 88f;
            original.ColorblindSafe = true;
            original.KeyBindings[13] = "F4";
            SettingsSnapshot parsed;
            SelfTest.True(SettingsStore.TryParse(SettingsStore.Serialize(original), out parsed), "自己写的能自己读", "读失败");
            SelfTest.Equal(1, (long)parsed.QualityTier);
            SelfTest.True(parsed.Sensitivity == 2.35f, "灵敏度往返", parsed.Sensitivity.ToString("R"));
            SelfTest.True(parsed.Fov == 88f, "视野往返", parsed.Fov.ToString("R"));
            SelfTest.True(parsed.ColorblindSafe, "开关往返", "丢了");
            SelfTest.True(parsed.KeyBindings[13] == "F4", "键位往返", parsed.KeyBindings[13]);
            SelfTest.True(parsed.KeyBindings[0] == "W", "未改的键位保持", parsed.KeyBindings[0]);
        }

        private static void ChecksMigration()
        {
            var v1 = "{\n  \"schemaVersion\": 1,\n  \"sensitivity\": 2.5,\n  \"fov\": 95,\n  \"masterVolume\": 0.4,\n  \"sfxVolume\": 0.6,\n  \"colorblindSafe\": true,\n  \"reduceMotion\": true\n}\n";
            SettingsSnapshot parsed;
            SelfTest.True(SettingsStore.TryParse(v1, out parsed), "v1 能读", "读失败");
            SelfTest.Equal(2, (long)parsed.SchemaVersion);                 // 迁移到 v2
            SelfTest.True(parsed.Sensitivity == 2.5f && parsed.Fov == 95f, "v1 的灵敏度/视野原样搬运", parsed.Sensitivity.ToString("R"));
            SelfTest.True(parsed.MasterVolume == 0.4f && parsed.SfxVolume == 0.6f, "v1 的两个音量原样搬运", parsed.MasterVolume.ToString("R"));
            SelfTest.True(parsed.ColorblindSafe && parsed.ReduceMotion, "v1 的两个开关原样搬运", "丢了");
            SelfTest.True(parsed.MusicVolume == 0.50f, "v1 没有的音乐音量取默认", parsed.MusicVolume.ToString("R"));
            SelfTest.Equal(2, (long)parsed.QualityTier);
            SelfTest.Equal(0x00FF66, (long)parsed.CrosshairColor);
            SelfTest.True(parsed.KeyBindings[6] == "Mouse0", "v1 没有的键位取默认表", parsed.KeyBindings[6]);
            // 完全没有 schemaVersion 也按 v1 迁移
            SettingsSnapshot bare;
            SelfTest.True(SettingsStore.TryParse("{\"sensitivity\": 0.5}", out bare), "缺版本号也能读", "读失败");
            SelfTest.Equal(2, (long)bare.SchemaVersion);
            SelfTest.True(bare.Sensitivity == 0.5f, "缺版本号时字段照读", bare.Sensitivity.ToString("R"));
        }

        private static void ChecksFutureVersion()
        {
            var dir = TempDir();
            Clean(dir);
            var path = Path.Combine(dir, SettingsStore.FileName);
            File.WriteAllText(path, "{\"schemaVersion\": 3, \"qualityTier\": 0, \"sfxVolume\": 0.1}");
            var store = new SettingsStore();
            store.Load(dir);
            SelfTest.True(store.ReadOnlyFile, "schemaVersion > 2 标记只读", "没标记");
            SelfTest.Equal(2, (long)store.Get().QualityTier);        // 内存用默认值
            SelfTest.True(store.Get().SfxVolume == 0.80f, "高版本文件不改内存取值方式（用默认）", store.Get().SfxVolume.ToString("R"));
            store.SetQualityTier(0);
            SelfTest.True(!store.FlushIfDirty(dir, true), "只读模式不写盘", "写了");
            SelfTest.True(File.ReadAllText(path).IndexOf("\"schemaVersion\": 3") >= 0, "高版本文件原样保留", "被覆盖");
            Clean(dir);
        }

        private static void ChecksBadJson()
        {
            var dir = TempDir();
            Clean(dir);
            var path = Path.Combine(dir, SettingsStore.FileName);
            File.WriteAllText(path, "{这显然不是 JSON");
            var store = new SettingsStore();
            store.Load(dir);
            SelfTest.Equal(1, (long)store.BadFileCount);
            SelfTest.True(File.Exists(Path.Combine(dir, SettingsStore.BadName)), "坏文件被备份为 settings.bad.json", "没备份");
            SelfTest.True(!File.Exists(path), "原文件已让位", "还在");
            SelfTest.Equal(2, (long)store.Get().QualityTier);
            SelfTest.True(store.Get().Fov == 75f, "坏 JSON 回落默认值", store.Get().Fov.ToString("R"));
            SelfTest.True(store.Dirty, "回落默认值后标记为脏（下次 flush 会重写）", "没标脏");
            SettingsSnapshot parsed;
            SelfTest.True(!SettingsStore.TryParse("{\"fov\": }", out parsed), "残缺值判为坏 JSON", "判成合法");
            SelfTest.True(!SettingsStore.TryParse("nonsense", out parsed), "非对象判为坏 JSON", "判成合法");
            SelfTest.True(!SettingsStore.TryParse("", out parsed), "空串判为坏 JSON", "判成合法");
            Clean(dir);
        }

        private static void ChecksUnknownKeys()
        {
            var json = "{\"schemaVersion\": 2, \"fov\": 80, \"somethingElse\": 42, \"nested\": {\"a\": 1}, \"qualityTier\": \"high\"}";
            SettingsSnapshot parsed;
            SelfTest.True(SettingsStore.TryParse(json, out parsed), "含未知键也能读", "读失败");
            SelfTest.True(parsed.Fov == 80f, "已知键照读", parsed.Fov.ToString("R"));
            SelfTest.Equal(2, (long)parsed.QualityTier);       // 类型不符 → 默认值，不整份丢弃
            SelfTest.True(parsed.KeyBindings.Length == 14, "未知键被丢弃但其它键完整", parsed.KeyBindings.Length.ToString());
            SettingsSnapshot clamped;
            SelfTest.True(SettingsStore.TryParse("{\"schemaVersion\": 2, \"fov\": 999, \"sensitivity\": 0.01}", out clamped), "越界值也能读", "读失败");
            SelfTest.True(clamped.Fov == 100f, "读入时逐键 clamp（上界）", clamped.Fov.ToString("R"));
            SelfTest.True(clamped.Sensitivity == 0.20f, "读入时逐键 clamp（下界）", clamped.Sensitivity.ToString("R"));
        }

        private static void ChecksAtomicWrite()
        {
            var dir = TempDir();
            Clean(dir);
            var store = new SettingsStore();
            store.SetFov(85f);
            store.BeginFrame();
            SelfTest.True(store.FlushIfDirty(dir), "脏了才落盘", "没落盘");
            var path = Path.Combine(dir, SettingsStore.FileName);
            SelfTest.True(File.Exists(path), "settings.json 已生成", "没生成");
            SelfTest.True(!File.Exists(path + ".tmp"), "没有 .tmp 残留", "有残留");
            SelfTest.True(File.ReadAllText(path).IndexOf("\"schemaVersion\": 2") > 0, "落盘内容是 v2", "版本不对");
            SelfTest.Equal(1, (long)store.FlushCount);
            var snapshot = default(SettingsSnapshot);
            SelfTest.True(SettingsStore.TryParse(File.ReadAllText(path), out snapshot) && snapshot.Fov == 85f, "落盘可回读", "回读失败");
            // 每帧最多一次
            store.SetFov(86f);
            store.BeginFrame();
            store.FlushIfDirty(dir);
            SelfTest.True(!store.FlushIfDirty(dir), "同一帧第二次不重复落盘", "又写了");
            store.SetFov(87f);
            store.BeginFrame();
            SelfTest.True(store.FlushIfDirty(dir), "下一帧继续", "没写");
            // 重新装载读回磁盘值
            var reloaded = new SettingsStore();
            reloaded.Load(dir);
            SelfTest.True(reloaded.Get().Fov == 87f, "重启后设置还在", reloaded.Get().Fov.ToString("R"));
            SelfTest.True(!reloaded.Dirty, "读 v2 文件不算脏", "标脏了");
            SelfTest.True(SettingsStore.PathOf(dir).EndsWith(SettingsStore.FileName), "路径拼接", SettingsStore.PathOf(dir));
            Clean(dir);
        }

        private static void ChecksKeyConflict()
        {
            var store = new SettingsStore();
            var changes = 0;
            store.Changed += key => { if (key == SettingsKey.KeyBindings) changes += 1; };
            SelfTest.True(store.SetKeyBinding(6, "E"), "开火改到 E（互动占用中）", "没改");
            var keys = store.Get().KeyBindings;
            SelfTest.True(keys[6] == "E", "开火 = E", keys[6]);
            SelfTest.True(keys[8] == "Mouse0", "旧占用者换成新占用者原来的键", keys[8]);
            SelfTest.Equal(8, (long)store.LastConflictAction);
            SelfTest.Equal(1, changes);
            var nonEmpty = true;
            for (var i = 0; i < keys.Length; i++) if (string.IsNullOrEmpty(keys[i])) nonEmpty = false;
            SelfTest.True(nonEmpty, "每个动作都还有键名", "有空键");
            SelfTest.True(!store.SetKeyBinding(0, ""), "空键名被拒", "接受了");
            SelfTest.True(!store.SetKeyBinding(99, "Z"), "越界动作被拒", "接受了");
            SelfTest.True(!store.SetKeyBinding(0, "W"), "同值不重复触发", "触发了");
            SelfTest.Equal(1, changes);
            var panel = new SettingsPanel(store);
            panel.Rebind(0, "E");
            SelfTest.True(panel.Hint.Length > 0, "面板给出冲突提示", "没提示");
            SelfTest.Equal(6, (long)panel.ConflictHintAction);
            SelfTest.True(panel.Rebind(0, store.Get().KeyBindings[0]) == false, "同值重绑不算变化（面板不刷新）", "刷新了");
        }

        private static void ChecksAudioPush()
        {
            var mixer = new Mixer();
            AudioSeam.Bind(mixer);
            var store = new SettingsStore();
            store.SetVolume(SettingsKey.MasterVolume, 0.30f);
            SelfTest.True(mixer.VolumeOf(MixBus.Master) == 0.30f, "改主音量立即推给音频层", mixer.VolumeOf(MixBus.Master).ToString("R"));
            store.SetVolume(SettingsKey.SfxVolume, 0.20f);
            SelfTest.True(mixer.VolumeOf(MixBus.Sfx) == 0.20f, "改音效音量立即生效", mixer.VolumeOf(MixBus.Sfx).ToString("R"));
            store.SetVolume(SettingsKey.MusicVolume, 0.10f);
            SelfTest.True(mixer.VolumeOf(MixBus.Music) == 0.10f, "改音乐音量立即生效", mixer.VolumeOf(MixBus.Music).ToString("R"));
            store.Reset();
            SelfTest.True(mixer.VolumeOf(MixBus.Master) == 0.80f && mixer.VolumeOf(MixBus.Music) == 0.50f, "恢复默认同时推音频", mixer.VolumeOf(MixBus.Master).ToString("R"));
            SelfTest.True(mixer.VolumeOf(MixBus.Subtitle) == 0f, "字幕总线恒 0（不接音源）", mixer.VolumeOf(MixBus.Subtitle).ToString("R"));
            AudioSeam.Bind(null);
            SelfTest.True(AudioSeam.IsSilent, "复位为静音", "没复位");
            // 面板改动也走同一条路径
            AudioSeam.Bind(mixer);
            var panel = new SettingsPanel(store);
            panel.SetVolume(SettingsKey.SfxVolume, 0.45f);
            SelfTest.True(mixer.VolumeOf(MixBus.Sfx) == 0.45f, "面板改音量也推音频", mixer.VolumeOf(MixBus.Sfx).ToString("R"));
            SelfTest.True(panel.ApplyCount > 0, "面板记录应用次数", panel.ApplyCount.ToString());
            AudioSeam.Bind(null);
        }

        private static void ChecksDebugPanel()
        {
            SelfTest.True(DebugPanel.RefreshMs == 250f, "刷新间隔 250ms", DebugPanel.RefreshMs.ToString("R"));
            SelfTest.Equal(12, (long)DebugPanel.FieldCount);
            var expected = new[]
            {
                "playerTick", "serverTick", "ping", "lossPercent", "inboundBytesPerSec", "outboundBytesPerSec",
                "frameTimeP95Ms", "frameTimeMaxMs", "entityCount", "pooledCount", "snapshotRateHz", "versionLine",
            };
            for (var i = 0; i < expected.Length; i++) SelfTest.True(DebugPanel.FieldName(i) == expected[i], "字段顺序 " + i, DebugPanel.FieldName(i));

            var panel = new DebugPanel();
            var sample = default(DebugSample);
            sample.PlayerTick = 120;
            sample.ServerTick = 118;
            sample.HasNetwork = true;
            sample.PingMs = 42.35f;
            sample.LossPercent = 6.5f;
            sample.InboundBytesPerSec = 50000f;
            sample.OutboundBytesPerSec = 1000f;
            sample.HasFrameTimes = true;
            sample.FrameTimeP95Ms = 21.25f;
            sample.FrameTimeMaxMs = 30f;
            sample.HasViewCounts = true;
            sample.EntityCount = 64;
            sample.PooledCount = 12;
            sample.HasSnapshotRate = true;
            sample.SnapshotRateHz = 20f;
            sample.VersionLine = "v2.0.0 MC13";
            panel.Build(sample);
            SelfTest.True(panel.LineAt(0) == "playerTick: 120", "playerTick 格式", panel.LineAt(0));
            SelfTest.True(panel.LineAt(2) == "ping: 42.4 ms", "ping 一位小数", panel.LineAt(2));
            SelfTest.True(panel.LineAt(3) == "lossPercent: 6.5 % !", "超 5% 标红", panel.LineAt(3));
            SelfTest.True(panel.LineAt(4) == "inboundBytesPerSec: 50000 B/s !", "入向超 40960 标红", panel.LineAt(4));
            SelfTest.True(panel.LineAt(5) == "outboundBytesPerSec: 1000 B/s", "出向未超不标", panel.LineAt(5));
            SelfTest.True(panel.LineAt(6) == "frameTimeP95Ms: 21.3 ms !", "P95 超 20 标红", panel.LineAt(6));
            SelfTest.True(panel.LineAt(7) == "frameTimeMaxMs: 30.0 ms", "最大值 30 未超 33", panel.LineAt(7));
            SelfTest.True(panel.LineAt(8) == "entityCount: 64" && panel.LineAt(9) == "pooledCount: 12", "视图计数格式", panel.LineAt(8));
            SelfTest.True(panel.LineAt(10) == "snapshotRateHz: 20.0 /s", "快照率格式", panel.LineAt(10));
            SelfTest.True(panel.LineAt(11) == "versionLine: v2.0.0 MC13", "版本行原样", panel.LineAt(11));
            // 缺数据源 → n/a，且不标红
            panel.Build(default(DebugSample));
            SelfTest.True(panel.LineAt(2) == "ping: n/a", "断网显示 n/a 不崩", panel.LineAt(2));
            SelfTest.True(panel.LineAt(6) == "frameTimeP95Ms: n/a", "帧时间未接线显示 n/a", panel.LineAt(6));
            SelfTest.True(panel.LineAt(8) == "entityCount: n/a", "视图计数未接线显示 n/a", panel.LineAt(8));
            SelfTest.True(panel.LineAt(11) == "versionLine: n/a", "空版本行显示 n/a", panel.LineAt(11));
            // 刷新节流与"不可见不采样"
            var throttled = new DebugPanel();
            throttled.Tick(100f, sample);
            SelfTest.Equal(0, throttled.RefreshCount);
            throttled.Toggle();
            throttled.Tick(100f, sample);
            SelfTest.Equal(1, throttled.RefreshCount);        // 打开即刷一次
            throttled.Tick(200f, sample);
            SelfTest.Equal(1, throttled.RefreshCount);        // 250ms 内不重复
            throttled.Tick(50f, sample);
            SelfTest.Equal(2, throttled.RefreshCount);
            throttled.Toggle();
            throttled.Tick(5000f, sample);
            SelfTest.Equal(2, throttled.RefreshCount);        // 不可见不刷新
            throttled.Toggle();
            throttled.Tick(1f, sample);
            SelfTest.True(throttled.RefreshCount >= 3, "再次打开又能刷新", throttled.RefreshCount.ToString());
            SelfTest.True(panel.LineAt(6).IndexOf("!", StringComparison.Ordinal) >= 0 || panel.LineAt(7).IndexOf("n/a", StringComparison.Ordinal) >= 0, "阈值标记只在有数据时出现", "标记错");
        }
    }
}
