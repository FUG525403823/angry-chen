using System;
using System.IO;
using System.Text;

namespace Ac.Core
{
    public enum SettingsKey : byte
    {
        QualityTier = 0, MasterVolume = 1, SfxVolume = 2, MusicVolume = 3, Sensitivity = 4,
        Fov = 5, CrosshairColor = 6, ColorblindSafe = 7, ReduceMotion = 8, KeyBindings = 9,
        CrosshairScale = 10, CrosshairThickness = 11, CrosshairGap = 12, CrosshairDynamic = 13,
    }

    // §5：只读快照。写一律走 SettingsStore.Set/Reset 并触发一次 Changed。
    public struct SettingsSnapshot
    {
        public int SchemaVersion;
        public int QualityTier;
        public float MasterVolume;
        public float SfxVolume;
        public float MusicVolume;
        public float Sensitivity;
        public float Fov;
        public int CrosshairColor;
        public float CrosshairScale;
        public float CrosshairThickness;
        public float CrosshairGap;
        public bool CrosshairDynamic;
        public bool ColorblindSafe;
        public bool ReduceMotion;
        public string[] KeyBindings;
    }

    public static class SettingsDefaults
    {
        public const int SchemaVersion = 3;
        public const int QualityTier = 2;
        public const float MasterVolume = 0.80f;
        public const float SfxVolume = 0.80f;
        public const float MusicVolume = 0.50f;
        public const float Sensitivity = 1.00f;
        public const float Fov = 75f;
        public const int CrosshairColor = 0x00FF66;
        public const float CrosshairScale = 1f;
        public const float CrosshairScaleMin = .5f;
        public const float CrosshairScaleMax = 3f;
        public const float CrosshairThickness = 2f;
        public const float CrosshairThicknessMin = 1f;
        public const float CrosshairThicknessMax = 6f;
        public const float CrosshairGap = 2f;
        public const float CrosshairGapMin = 0f;
        public const float CrosshairGapMax = 20f;
        public const bool CrosshairDynamic = true;
        public const bool ColorblindSafe = false;
        public const bool ReduceMotion = false;

        public const int QualityTierMin = 0;
        public const int QualityTierMax = 2;
        public const float VolumeMin = 0.00f;
        public const float VolumeMax = 1.00f;
        public const float SensitivityMin = 0.20f;
        public const float SensitivityMax = 5.00f;
        public const float FovMin = 60f;
        public const float FovMax = 100f;

        public const int ActionCount = 15;
        // 第 12 条（下标 11）= 局内聊天键。C13 §5 的动作表一直有这一格、默认值也是 Return，但**从没有
        // 消费方**（全仓无 ActionChat 常量）⇒ 这条绑定此前是死的。v2 收尾把聊天 UI 接上后才可读。
        // 它与下标 14 的 `ready` 默认同键，是**刻意**的：两者相位互斥（`ready` 只在大厅读，`chat` 只在
        // playing 读），重绑面板按用户选择解绑。采样器与聊天缓冲里都不许写死 Return。
        public const int ActionChat = 11;
        // 第 14 条（下标 13）是调试面板热键：Boot 侧不许再硬编码 F3，一律按这个下标读键位表。
        public const int ActionDebugPanel = 13;
        // 第 15 条（下标 14）= 大厅"准备"确认键（ADR-013 的 Ready 位），v2 收官新增：
        // 默认与下标 11 的 `chat` 同为 Return，相位互斥的说明见 ActionChat。
        public const int ActionReady = 14;

        // 顺序即 keyBindings 下标，逐条照抄 C13 §5 的动作表（v2 收官时新增第 15 条 `ready`）
        public static readonly string[] KeyBindings =
        {
            "W", "S", "A", "D", "LeftShift", "Space", "Mouse0",
            "R", "E", "F", "Q", "Return", "O", "F3", "Return",
        };

        public static readonly int[] CrosshairColors = { 0x00FF66, 0x66E0FF, 0xFFFF66, 0xFF66CC };

        // H5：色盲安全开关不能是个死设置。按 sRGB 亮度权重在既有调色板里挑最亮（深色 HUD 上最清楚）的一档，
        // 用整数权重避免浮点不确定性。
        public static int MostContrastingCrosshairColor()
        {
            var best = CrosshairColors[0];
            var bestLuma = -1;
            for (var i = 0; i < CrosshairColors.Length; i++)
            {
                var c = CrosshairColors[i];
                var luma = 2126 * ((c >> 16) & 0xFF) + 7152 * ((c >> 8) & 0xFF) + 722 * (c & 0xFF);
                if (luma > bestLuma) { bestLuma = luma; best = c; }
            }
            return best;
        }

        public static SettingsSnapshot Default()
        {
            var snapshot = default(SettingsSnapshot);
            snapshot.SchemaVersion = SchemaVersion;
            snapshot.QualityTier = QualityTier;
            snapshot.MasterVolume = MasterVolume;
            snapshot.SfxVolume = SfxVolume;
            snapshot.MusicVolume = MusicVolume;
            snapshot.Sensitivity = Sensitivity;
            snapshot.Fov = Fov;
            snapshot.CrosshairColor = CrosshairColor;
            snapshot.CrosshairScale = CrosshairScale;
            snapshot.CrosshairThickness = CrosshairThickness;
            snapshot.CrosshairGap = CrosshairGap;
            snapshot.CrosshairDynamic = CrosshairDynamic;
            snapshot.ColorblindSafe = ColorblindSafe;
            snapshot.ReduceMotion = ReduceMotion;
            snapshot.KeyBindings = (string[])KeyBindings.Clone();
            return snapshot;
        }

        public static int ClampTier(int value) { return value < QualityTierMin ? QualityTierMin : (value > QualityTierMax ? QualityTierMax : value); }
        // NaN 与任何比较都是 false，会直接穿透 clamp 并被写进文件 → 一律回落默认值（"类型不符用默认值"）
        public static float ClampVolume(float value) { return float.IsNaN(value) ? MasterVolume : (value < VolumeMin ? VolumeMin : (value > VolumeMax ? VolumeMax : value)); }
        public static float ClampSensitivity(float value) { return float.IsNaN(value) ? Sensitivity : (value < SensitivityMin ? SensitivityMin : (value > SensitivityMax ? SensitivityMax : value)); }
        public static float ClampFov(float value) { return float.IsNaN(value) ? Fov : (value < FovMin ? FovMin : (value > FovMax ? FovMax : value)); }

        public static float ClampCrosshairScale(float value) { return float.IsNaN(value) ? CrosshairScale : (value < CrosshairScaleMin ? CrosshairScaleMin : (value > CrosshairScaleMax ? CrosshairScaleMax : value)); }

        public static float ClampCrosshairThickness(float value) { return float.IsNaN(value) ? CrosshairThickness : (value < CrosshairThicknessMin ? CrosshairThicknessMin : (value > CrosshairThicknessMax ? CrosshairThicknessMax : value)); }

        public static float ClampCrosshairGap(float value) { return float.IsNaN(value) ? CrosshairGap : (value < CrosshairGapMin ? CrosshairGapMin : (value > CrosshairGapMax ? CrosshairGapMax : value)); }

        public static bool IsAllowedCrosshairColor(int color)
        {
            for (var i = 0; i < CrosshairColors.Length; i++) if (CrosshairColors[i] == color) return true;
            return false;
        }

        public static int NearestCrosshairColor(int color)
        {
            if (IsAllowedCrosshairColor(color)) return color;
            var best = CrosshairColors[0];
            var bestDistance = int.MaxValue;
            for (var i = 0; i < CrosshairColors.Length; i++)
            {
                var r = ((CrosshairColors[i] >> 16) & 0xFF) - ((color >> 16) & 0xFF);
                var g = ((CrosshairColors[i] >> 8) & 0xFF) - ((color >> 8) & 0xFF);
                var b = (CrosshairColors[i] & 0xFF) - (color & 0xFF);
                var distance = r * r + g * g + b * b;
                if (distance < bestDistance) { bestDistance = distance; best = CrosshairColors[i]; }
            }
            return best;
        }
    }

    // C13 §5：设置模型 + 默认值表 + clamp + JSON 读写 + 原子落盘 + 版本迁移。
    public sealed class SettingsStore
    {
        public const string FileName = "settings.json";
        public const string TempName = "settings.json.tmp";
        public const string BadName = "settings.bad.json";

        private SettingsSnapshot _snapshot;
        private bool _dirty;
        private bool _flushedThisFrame;
        private int _lastConflictAction = -1;

        public SettingsStore() { _snapshot = SettingsDefaults.Default(); }

        public bool Dirty { get { return _dirty; } }
        public int FlushCount { get; private set; }
        public int BadFileCount { get; private set; }
        public int LoadCount { get; private set; }
        public bool ReadOnlyFile { get; private set; }      // Future schemas are never overwritten.
        public string LastSaveError { get; private set; } = string.Empty;
        public int LastConflictAction { get { return _lastConflictAction; } }

        public event Action<SettingsKey> Changed;

        // §5：Get() 只给不可变快照。键位数组是引用类型，这里克隆一份，外部改不到内部状态。
        public SettingsSnapshot Get()
        {
            var copy = _snapshot;
            var keys = _snapshot.KeyBindings ?? SettingsDefaults.KeyBindings;
            copy.KeyBindings = (string[])keys.Clone();
            return copy;
        }

        public static string PathOf(string directory)
        {
            return Path.Combine(string.IsNullOrEmpty(directory) ? "." : directory, FileName);
        }

        public static string TempPathOf(string directory) { return Path.Combine(string.IsNullOrEmpty(directory) ? "." : directory, TempName); }
        public static string BadPathOf(string directory) { return Path.Combine(string.IsNullOrEmpty(directory) ? "." : directory, BadName); }

        // ---- 写入 ----

        public bool SetQualityTier(int value) { return Apply(SettingsKey.QualityTier, SettingsDefaults.ClampTier(value), 0f, false); }

        public bool SetVolume(SettingsKey key, float value)
        {
            if (key != SettingsKey.MasterVolume && key != SettingsKey.SfxVolume && key != SettingsKey.MusicVolume) return false;
            return Apply(key, 0, SettingsDefaults.ClampVolume(value), false);
        }

        public bool SetSensitivity(float value) { return Apply(SettingsKey.Sensitivity, 0, SettingsDefaults.ClampSensitivity(value), false); }
        public bool SetFov(float value) { return Apply(SettingsKey.Fov, 0, SettingsDefaults.ClampFov(value), false); }
        public bool SetCrosshairColor(int value) { return Apply(SettingsKey.CrosshairColor, SettingsDefaults.NearestCrosshairColor(value), 0f, false); }
        public bool SetCrosshairScale(float value) { return Apply(SettingsKey.CrosshairScale, 0, SettingsDefaults.ClampCrosshairScale(value), false); }
        public bool SetCrosshairThickness(float value) { return Apply(SettingsKey.CrosshairThickness, 0, SettingsDefaults.ClampCrosshairThickness(value), false); }
        public bool SetCrosshairGap(float value) { return Apply(SettingsKey.CrosshairGap, 0, SettingsDefaults.ClampCrosshairGap(value), false); }
        public bool SetCrosshairDynamic(bool value) { return Apply(SettingsKey.CrosshairDynamic, 0, 0f, value); }
        public bool SetColorblindSafe(bool value) { return Apply(SettingsKey.ColorblindSafe, 0, 0f, value); }
        public bool SetReduceMotion(bool value) { return Apply(SettingsKey.ReduceMotion, 0, 0f, value); }

        // 键位重绑：同一键被两个动作占用时，把旧占用者换成新占用者原来的键（保持每个动作非空）并在面板提示
        public bool SetKeyBinding(int action, string keyName)
        {
            if (action < 0 || action >= SettingsDefaults.ActionCount) return false;
            if (string.IsNullOrEmpty(keyName)) return false;
            var current = _snapshot.KeyBindings;
            var keys = (string[])current.Clone();      // 不在旧数组上原地改：否则脏检查与 Changed 都会失效
            _lastConflictAction = -1;
            for (var i = 0; i < keys.Length; i++)
            {
                if (i == action || keys[i] != keyName) continue;
                keys[i] = keys[action];
                _lastConflictAction = i;
            }
            keys[action] = keyName;
            var changed = false;
            for (var i = 0; i < keys.Length; i++) if (keys[i] != current[i]) { changed = true; break; }
            if (!changed) return false;                 // 同值不标脏、不触发（Apply 的 before 快照在这里用不上：数组是引用类型）
            _snapshot.KeyBindings = keys;
            _dirty = true;
            var handler = Changed;
            if (handler != null) handler(SettingsKey.KeyBindings);
            return true;
        }

        private bool Apply(SettingsKey key, int intValue, float floatValue, bool boolValue)
        {
            var before = _snapshot;
            switch (key)
            {
                case SettingsKey.QualityTier: _snapshot.QualityTier = intValue; break;
                case SettingsKey.MasterVolume: _snapshot.MasterVolume = floatValue; break;
                case SettingsKey.SfxVolume: _snapshot.SfxVolume = floatValue; break;
                case SettingsKey.MusicVolume: _snapshot.MusicVolume = floatValue; break;
                case SettingsKey.Sensitivity: _snapshot.Sensitivity = floatValue; break;
                case SettingsKey.Fov: _snapshot.Fov = floatValue; break;
                case SettingsKey.CrosshairColor: _snapshot.CrosshairColor = intValue; break;
                case SettingsKey.CrosshairScale: _snapshot.CrosshairScale = floatValue; break;
                case SettingsKey.CrosshairThickness: _snapshot.CrosshairThickness = floatValue; break;
                case SettingsKey.CrosshairGap: _snapshot.CrosshairGap = floatValue; break;
                case SettingsKey.CrosshairDynamic: _snapshot.CrosshairDynamic = boolValue; break;
                case SettingsKey.ColorblindSafe: _snapshot.ColorblindSafe = boolValue; break;
                case SettingsKey.ReduceMotion: _snapshot.ReduceMotion = boolValue; break;
                case SettingsKey.KeyBindings: break;      // 数组已就地改过
            }
            if (SameValue(key, in before, in _snapshot)) return false;
            _dirty = true;
            PushToAudio(key);
            var handler = Changed;
            if (handler != null) handler(key);
            return true;
        }

        private static bool SameValue(SettingsKey key, in SettingsSnapshot a, in SettingsSnapshot b)
        {
            switch (key)
            {
                case SettingsKey.QualityTier: return a.QualityTier == b.QualityTier;
                case SettingsKey.MasterVolume: return a.MasterVolume == b.MasterVolume;
                case SettingsKey.SfxVolume: return a.SfxVolume == b.SfxVolume;
                case SettingsKey.MusicVolume: return a.MusicVolume == b.MusicVolume;
                case SettingsKey.Sensitivity: return a.Sensitivity == b.Sensitivity;
                case SettingsKey.Fov: return a.Fov == b.Fov;
                case SettingsKey.CrosshairColor: return a.CrosshairColor == b.CrosshairColor;
                case SettingsKey.CrosshairScale: return a.CrosshairScale == b.CrosshairScale;
                case SettingsKey.CrosshairThickness: return a.CrosshairThickness == b.CrosshairThickness;
                case SettingsKey.CrosshairGap: return a.CrosshairGap == b.CrosshairGap;
                case SettingsKey.CrosshairDynamic: return a.CrosshairDynamic == b.CrosshairDynamic;
                case SettingsKey.ColorblindSafe: return a.ColorblindSafe == b.ColorblindSafe;
                case SettingsKey.ReduceMotion: return a.ReduceMotion == b.ReduceMotion;
                default:
                    var x = a.KeyBindings;
                    var y = b.KeyBindings;
                    if (x == null || y == null || x.Length != y.Length) return false;
                    for (var i = 0; i < x.Length; i++) if (x[i] != y[i]) return false;
                    return true;
            }
        }

        public void Reset()
        {
            _snapshot = SettingsDefaults.Default();
            _dirty = true;
            ApplyVolumesToAudio();      // ReadOnlyFile 不动：高版本文件任何时候都不许被写回
            var handler = Changed;
            if (handler != null) handler(SettingsKey.QualityTier);
        }

        // §5：音量经 Ac.Core 的音频缝推给音频层（Ac.UI 不引用 Ac.Audio）
        public void ApplyVolumesToAudio()
        {
            AudioSeam.SetVolume(MixBus.Master, _snapshot.MasterVolume);
            AudioSeam.SetVolume(MixBus.Sfx, _snapshot.SfxVolume);
            AudioSeam.SetVolume(MixBus.Music, _snapshot.MusicVolume);
        }

        private void PushToAudio(SettingsKey key)
        {
            if (key == SettingsKey.MasterVolume) AudioSeam.SetVolume(MixBus.Master, _snapshot.MasterVolume);
            else if (key == SettingsKey.SfxVolume) AudioSeam.SetVolume(MixBus.Sfx, _snapshot.SfxVolume);
            else if (key == SettingsKey.MusicVolume) AudioSeam.SetVolume(MixBus.Music, _snapshot.MusicVolume);
        }

        // ---- 落盘 ----

        public void BeginFrame() { _flushedThisFrame = false; }

        // 帧末 flush：每帧最多落盘一次；schemaVersion > 3 的文件只读，绝不覆盖
        public bool FlushIfDirty(string directory, bool force = false)
        {
            if (!_dirty) return false;
            if (!force && _flushedThisFrame) return false;
            return TrySave(directory);
        }

        public void Save(string directory) { TrySave(directory); }

        private bool TrySave(string directory)
        {
            if (ReadOnlyFile)
            {
                LastSaveError = "设置文件来自更高版本，当前仅在内存中应用，未保存";
                return false;
            }
            try
            {
                var path = PathOf(directory);
                var temp = TempPathOf(directory);
                var json = Serialize(_snapshot);
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                File.WriteAllText(temp, json, new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, null);
                else File.Move(temp, path);
            }
            catch (IOException ex) { LastSaveError = "保存设置失败：" + ex.Message; return false; }
            catch (UnauthorizedAccessException ex) { LastSaveError = "保存设置失败：" + ex.Message; return false; }
            catch (ArgumentException ex) { LastSaveError = "保存设置失败：" + ex.Message; return false; }
            catch (NotSupportedException ex) { LastSaveError = "保存设置失败：" + ex.Message; return false; }
            LastSaveError = string.Empty;
            _dirty = false;
            _flushedThisFrame = true;
            FlushCount += 1;
            return true;
        }

        public void Load(string directory)
        {
            LoadCount += 1;
            LastSaveError = string.Empty;
            _dirty = false;
            _flushedThisFrame = false;
            var path = PathOf(directory);
            if (!File.Exists(path))
            {
                _snapshot = SettingsDefaults.Default();
                ReadOnlyFile = false;            // 文件不在了就没有"高版本只读"这回事，否则落盘会被静默丢弃
                ApplyVolumesToAudio();
                return;
            }
            string text;
            try { text = File.ReadAllText(path, Encoding.UTF8); }
            catch (IOException) { _snapshot = SettingsDefaults.Default(); ReadOnlyFile = false; return; }

            SettingsSnapshot parsed;
            int sourceVersion;
            if (!TryParse(text, out parsed, out sourceVersion))
            {
                // §5：解析失败 → 备份 + 回落默认值，游戏不崩
                try
                {
                    var bad = BadPathOf(directory);
                    if (File.Exists(bad)) File.Delete(bad);
                    File.Move(path, bad);
                    BadFileCount += 1;
                }
                catch (IOException) { }
                _snapshot = SettingsDefaults.Default();
                _dirty = true;
                ReadOnlyFile = false;            // 坏文件已经改名备份，之后必须能正常落盘
                ApplyVolumesToAudio();
                return;
            }

            _snapshot = parsed;
            ReadOnlyFile = sourceVersion > SettingsDefaults.SchemaVersion;
            if (!ReadOnlyFile && sourceVersion < SettingsDefaults.SchemaVersion)
            {
                _snapshot.SchemaVersion = SettingsDefaults.SchemaVersion;
                _dirty = true;                                   // 迁移后立即写回（v1 或没有版本号）
            }
            ApplyVolumesToAudio();
        }

        // ---- JSON（只认这一个扁平形状：schemaVersion + 十个键） ----

        public static string Serialize(in SettingsSnapshot snapshot)
        {
            var builder = new StringBuilder(512);
            builder.Append("{\n");
            builder.Append("  \"schemaVersion\": ").Append(snapshot.SchemaVersion).Append(",\n");
            builder.Append("  \"qualityTier\": ").Append(snapshot.QualityTier).Append(",\n");
            builder.Append("  \"masterVolume\": ").Append(Float(snapshot.MasterVolume)).Append(",\n");
            builder.Append("  \"sfxVolume\": ").Append(Float(snapshot.SfxVolume)).Append(",\n");
            builder.Append("  \"musicVolume\": ").Append(Float(snapshot.MusicVolume)).Append(",\n");
            builder.Append("  \"sensitivity\": ").Append(Float(snapshot.Sensitivity)).Append(",\n");
            builder.Append("  \"fov\": ").Append(Float(snapshot.Fov)).Append(",\n");
            builder.Append("  \"crosshairColor\": ").Append(snapshot.CrosshairColor).Append(",\n");
            builder.Append("  \"crosshairScale\": ").Append(Float(snapshot.CrosshairScale)).Append(",\n");
            builder.Append("  \"crosshairThickness\": ").Append(Float(snapshot.CrosshairThickness)).Append(",\n");
            builder.Append("  \"crosshairGap\": ").Append(Float(snapshot.CrosshairGap)).Append(",\n");
            builder.Append("  \"crosshairDynamic\": ").Append(snapshot.CrosshairDynamic ? "true" : "false").Append(",\n");
            builder.Append("  \"colorblindSafe\": ").Append(snapshot.ColorblindSafe ? "true" : "false").Append(",\n");
            builder.Append("  \"reduceMotion\": ").Append(snapshot.ReduceMotion ? "true" : "false").Append(",\n");
            builder.Append("  \"keyBindings\": [");
            var keys = snapshot.KeyBindings ?? SettingsDefaults.KeyBindings;
            for (var i = 0; i < keys.Length; i++)
            {
                if (i > 0) builder.Append(", ");
                builder.Append('"').Append(Escape(keys[i])).Append('"');
            }
            builder.Append("]\n}\n");
            return builder.ToString();
        }

        // 键位名是玩家可改的字符串：不转义就会写出非法 JSON，下次启动整份被当成坏文件
        private static string Escape(string text)
        {
            if (string.IsNullOrEmpty(text)) return string.Empty;
            var builder = new StringBuilder(text.Length + 4);
            for (var i = 0; i < text.Length; i++)
            {
                var c = text[i];
                if (c == '"' || c == '\\') builder.Append('\\');
                builder.Append(c);
            }
            return builder.ToString();
        }

        private static string Float(float value)
        {
            return value.ToString("0.00", System.Globalization.CultureInfo.InvariantCulture);
        }

        public static bool TryParse(string json, out SettingsSnapshot snapshot)
        {
            int ignored;
            return TryParse(json, out snapshot, out ignored);
        }

        // sourceVersion 是文件里原本的版本号（迁移判定必须用它，不能用出口快照里的当前版本）
        public static bool TryParse(string json, out SettingsSnapshot snapshot, out int sourceVersion)
        {
            snapshot = SettingsDefaults.Default();
            sourceVersion = 1;
            JsonValue root;
            // B5：JSON 的词法/语法只有 MiniJson 一份实现；这里只做"取键 + 类型不符回落"的薄适配
            try { root = MiniJson.Parse(json); }
            catch (JsonException) { return false; }
            if (root.Kind != JsonKind.Object) return false;

            var version = ReadVersion(root);
            sourceVersion = version;
            if (version > SettingsDefaults.SchemaVersion)
            {
                snapshot.SchemaVersion = version;
                return true;                                     // 只读：内存用默认值，文件保留
            }
            var v1 = !HasMember(root, "schemaVersion") || version <= 1;

            snapshot.SchemaVersion = SettingsDefaults.SchemaVersion;
            snapshot.QualityTier = v1 ? SettingsDefaults.QualityTier : SettingsDefaults.ClampTier(ReadInt(root, "qualityTier", SettingsDefaults.QualityTier));
            snapshot.MasterVolume = SettingsDefaults.ClampVolume(ReadFloat(root, "masterVolume", SettingsDefaults.MasterVolume));
            snapshot.SfxVolume = SettingsDefaults.ClampVolume(ReadFloat(root, "sfxVolume", SettingsDefaults.SfxVolume));
            snapshot.MusicVolume = v1 ? SettingsDefaults.MusicVolume : SettingsDefaults.ClampVolume(ReadFloat(root, "musicVolume", SettingsDefaults.MusicVolume));
            snapshot.Sensitivity = SettingsDefaults.ClampSensitivity(ReadFloat(root, "sensitivity", SettingsDefaults.Sensitivity));
            snapshot.Fov = SettingsDefaults.ClampFov(ReadFloat(root, "fov", SettingsDefaults.Fov));
            snapshot.CrosshairColor = v1 ? SettingsDefaults.CrosshairColor : SettingsDefaults.NearestCrosshairColor(ReadInt(root, "crosshairColor", SettingsDefaults.CrosshairColor));
            snapshot.CrosshairScale = version < 3 ? SettingsDefaults.CrosshairScale : SettingsDefaults.ClampCrosshairScale(ReadFloat(root, "crosshairScale", SettingsDefaults.CrosshairScale));
            snapshot.CrosshairThickness = version < 3 ? SettingsDefaults.CrosshairThickness : SettingsDefaults.ClampCrosshairThickness(ReadFloat(root, "crosshairThickness", SettingsDefaults.CrosshairThickness));
            snapshot.CrosshairGap = version < 3 ? SettingsDefaults.CrosshairGap : SettingsDefaults.ClampCrosshairGap(ReadFloat(root, "crosshairGap", SettingsDefaults.CrosshairGap));
            snapshot.CrosshairDynamic = version < 3 ? SettingsDefaults.CrosshairDynamic : ReadBool(root, "crosshairDynamic", SettingsDefaults.CrosshairDynamic);
            snapshot.ColorblindSafe = ReadBool(root, "colorblindSafe", SettingsDefaults.ColorblindSafe);
            snapshot.ReduceMotion = ReadBool(root, "reduceMotion", SettingsDefaults.ReduceMotion);
            snapshot.KeyBindings = v1 ? (string[])SettingsDefaults.KeyBindings.Clone() : ReadKeys(root);
            return true;
        }

        // ---- 取值适配：类型不符/缺失一律回落给定的默认值（§5：不整份丢弃），未知键直接读不到 ----

        private static bool HasMember(JsonValue root, string key)
        {
            JsonValue ignored;
            return TryMember(root, key, out ignored);
        }

        // 重复键取最后一次出现，沿用旧扁平读取的 last-wins
        private static bool TryMember(JsonValue root, string key, out JsonValue value)
        {
            for (var i = root.Count - 1; i >= 0; i--)
            {
                var member = root.MemberAt(i);
                if (member.Key == key) { value = member.Value; return true; }
            }
            value = null;
            return false;
        }

        // 数字只认 Number；带引号的数字串沿用旧读取的容错（"0.80" 也当数字），其余一律 NaN（= 类型不符）
        private static double Number(JsonValue root, string key)
        {
            JsonValue value;
            if (!TryMember(root, key, out value)) return double.NaN;
            if (value.Kind == JsonKind.Number) return value.AsDouble();
            if (value.Kind != JsonKind.String) return double.NaN;
            double parsed;
            return double.TryParse(value.AsString().Trim(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out parsed) ? parsed : double.NaN;
        }

        // 版本号按数值读：合法的 2.0 / 2e0 不能被当成"缺版本号"（那会把 v2 文件当 v1 迁移并丢键）
        private static int ReadVersion(JsonValue root)
        {
            var value = Number(root, "schemaVersion");
            if (double.IsNaN(value) || value < 0.0) return 1;
            if (value > SettingsDefaults.SchemaVersion) return int.MaxValue;   // 只读；顺带避开 (int) 溢出
            return (int)value;
        }

        private static int ReadInt(JsonValue root, string key, int fallback)
        {
            var value = Number(root, key);
            if (double.IsNaN(value) || value != Math.Floor(value) || value < int.MinValue || value > int.MaxValue) return fallback;
            return (int)value;
        }

        private static float ReadFloat(JsonValue root, string key, float fallback)
        {
            var value = Number(root, key);
            return double.IsNaN(value) ? fallback : (float)value;
        }

        private static bool ReadBool(JsonValue root, string key, bool fallback)
        {
            JsonValue value;
            if (!TryMember(root, key, out value)) return fallback;
            if (value.Kind == JsonKind.Bool) return value.AsBool();
            if (value.Kind != JsonKind.String) return fallback;
            var text = value.AsString().Trim();
            if (text == "true") return true;
            if (text == "false") return false;
            return fallback;
        }

        // 键位数组：短了的部分保持默认表；非字符串元素与空串一律保持默认
        private static string[] ReadKeys(JsonValue root)
        {
            var keys = (string[])SettingsDefaults.KeyBindings.Clone();
            JsonValue value;
            if (!TryMember(root, "keyBindings", out value) || value.Kind != JsonKind.Array) return keys;
            var count = value.Count < SettingsDefaults.ActionCount ? value.Count : SettingsDefaults.ActionCount;
            for (var i = 0; i < count; i++)
            {
                var item = value[i];
                if (item.Kind != JsonKind.String) continue;
                var name = item.AsString();
                if (name.Length > 0) keys[i] = name;
            }
            return keys;
        }
    }
}
