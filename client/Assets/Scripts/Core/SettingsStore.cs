using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace Ac.Core
{
    public enum SettingsKey : byte
    {
        QualityTier = 0, MasterVolume = 1, SfxVolume = 2, MusicVolume = 3, Sensitivity = 4,
        Fov = 5, CrosshairColor = 6, ColorblindSafe = 7, ReduceMotion = 8, KeyBindings = 9,
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
        public bool ColorblindSafe;
        public bool ReduceMotion;
        public string[] KeyBindings;
    }

    public static class SettingsDefaults
    {
        public const int SchemaVersion = 2;
        public const int QualityTier = 2;
        public const float MasterVolume = 0.80f;
        public const float SfxVolume = 0.80f;
        public const float MusicVolume = 0.50f;
        public const float Sensitivity = 1.00f;
        public const float Fov = 75f;
        public const int CrosshairColor = 0x00FF66;
        public const bool ColorblindSafe = false;
        public const bool ReduceMotion = false;

        public const int QualityTierMin = 0;
        public const int QualityTierMax = 2;
        public const float VolumeMin = 0.00f;
        public const float VolumeMax = 1.00f;
        public const float SensitivityMin = 0.20f;
        public const float SensitivityMax = 3.00f;
        public const float FovMin = 60f;
        public const float FovMax = 100f;

        public const int ActionCount = 14;

        // 顺序即 keyBindings 下标，逐条照抄 C05 §5.2
        public static readonly string[] KeyBindings =
        {
            "W", "S", "A", "D", "LeftShift", "Space", "Mouse0",
            "R", "E", "F", "Q", "Return", "O", "F3",
        };

        public static readonly int[] CrosshairColors = { 0x00FF66, 0x66E0FF, 0xFFFF66, 0xFF66CC };

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
        public bool ReadOnlyFile { get; private set; }      // schemaVersion > 2：只读不写
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

        // 帧末 flush：每帧最多落盘一次；schemaVersion > 2 的文件只读，绝不覆盖
        public bool FlushIfDirty(string directory, bool force = false)
        {
            if (!_dirty) return false;
            if (!force && _flushedThisFrame) return false;
            if (ReadOnlyFile) { _dirty = false; return false; }
            Save(directory);
            return true;
        }

        public void Save(string directory)
        {
            var path = PathOf(directory);
            var temp = TempPathOf(directory);
            var json = Serialize(_snapshot);
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(path));
                File.WriteAllText(temp, json, new UTF8Encoding(false));
                if (File.Exists(path)) File.Replace(temp, path, null);
                else File.Move(temp, path);
            }
            catch (IOException) { return; }                 // 磁盘满/被占用：设置仍在内存里生效，不崩
            catch (UnauthorizedAccessException) { return; }
            _dirty = false;
            _flushedThisFrame = true;
            FlushCount += 1;
        }

        public void Load(string directory)
        {
            LoadCount += 1;
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

        private static string Unescape(string text)
        {
            if (string.IsNullOrEmpty(text) || text.IndexOf('\\') < 0) return text;
            var builder = new StringBuilder(text.Length);
            for (var i = 0; i < text.Length; i++)
            {
                if (text[i] == '\\' && i + 1 < text.Length)
                {
                    i += 1;
                    var escape = text[i];
                    if (escape == 'n') builder.Append('\n');
                    else if (escape == 't') builder.Append('\t');
                    else if (escape == 'r') builder.Append('\r');
                    else builder.Append(escape);
                }
                else builder.Append(text[i]);
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

        // sourceVersion 是文件里原本的版本号（迁移判定必须用它，不能用出口快照里的 2）
        public static bool TryParse(string json, out SettingsSnapshot snapshot, out int sourceVersion)
        {
            snapshot = SettingsDefaults.Default();
            sourceVersion = 1;
            Dictionary<string, string> flat;
            if (!TryReadFlat(json, out flat)) return false;

            var version = ReadVersion(flat);
            sourceVersion = version;
            if (version > SettingsDefaults.SchemaVersion)
            {
                snapshot.SchemaVersion = version;
                return true;                                     // 只读：内存用默认值，文件保留
            }
            var v1 = !flat.ContainsKey("schemaVersion") || version <= 1;

            snapshot.SchemaVersion = SettingsDefaults.SchemaVersion;
            snapshot.QualityTier = v1 ? SettingsDefaults.QualityTier : SettingsDefaults.ClampTier(ReadInt(flat, "qualityTier", SettingsDefaults.QualityTier));
            snapshot.MasterVolume = SettingsDefaults.ClampVolume(ReadFloat(flat, "masterVolume", SettingsDefaults.MasterVolume));
            snapshot.SfxVolume = SettingsDefaults.ClampVolume(ReadFloat(flat, "sfxVolume", SettingsDefaults.SfxVolume));
            snapshot.MusicVolume = v1 ? SettingsDefaults.MusicVolume : SettingsDefaults.ClampVolume(ReadFloat(flat, "musicVolume", SettingsDefaults.MusicVolume));
            snapshot.Sensitivity = SettingsDefaults.ClampSensitivity(ReadFloat(flat, "sensitivity", SettingsDefaults.Sensitivity));
            snapshot.Fov = SettingsDefaults.ClampFov(ReadFloat(flat, "fov", SettingsDefaults.Fov));
            snapshot.CrosshairColor = v1 ? SettingsDefaults.CrosshairColor : SettingsDefaults.NearestCrosshairColor(ReadInt(flat, "crosshairColor", SettingsDefaults.CrosshairColor));
            snapshot.ColorblindSafe = ReadBool(flat, "colorblindSafe", SettingsDefaults.ColorblindSafe);
            snapshot.ReduceMotion = ReadBool(flat, "reduceMotion", SettingsDefaults.ReduceMotion);
            snapshot.KeyBindings = v1 ? (string[])SettingsDefaults.KeyBindings.Clone() : ReadKeys(flat);
            return true;
        }

        private static string[] ReadKeys(Dictionary<string, string> flat)
        {
            var keys = (string[])SettingsDefaults.KeyBindings.Clone();
            string raw;
            if (!flat.TryGetValue("keyBindings", out raw)) return keys;
            var parts = raw.Split(',');
            for (var i = 0; i < SettingsDefaults.ActionCount && i < parts.Length; i++)
            {
                var name = parts[i].Trim();
                if (name.Length >= 2 && name[0] == '"' && name[name.Length - 1] == '"') name = name.Substring(1, name.Length - 2);
                if (name.Length > 0) keys[i] = Unescape(name);
            }
            return keys;
        }

        // 版本号按浮点读：合法的 2.0 / 2e0 不能被当成"缺版本号"（那会把 v2 文件当 v1 迁移并丢键）
        private static int ReadVersion(Dictionary<string, string> flat)
        {
            string raw;
            if (!flat.TryGetValue("schemaVersion", out raw)) return 1;
            float value;
            if (!float.TryParse(raw.Trim(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out value)) return 1;
            if (float.IsNaN(value) || value < 0f) return 1;
            return (int)value;
        }

        private static int ReadInt(Dictionary<string, string> flat, string key, int fallback)
        {
            string raw;
            if (!flat.TryGetValue(key, out raw)) return fallback;
            int value;
            return int.TryParse(raw.Trim(), System.Globalization.NumberStyles.Integer, System.Globalization.CultureInfo.InvariantCulture, out value) ? value : fallback;
        }

        private static float ReadFloat(Dictionary<string, string> flat, string key, float fallback)
        {
            string raw;
            if (!flat.TryGetValue(key, out raw)) return fallback;
            float value;
            return float.TryParse(raw.Trim(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out value) ? value : fallback;
        }

        private static bool ReadBool(Dictionary<string, string> flat, string key, bool fallback)
        {
            string raw;
            if (!flat.TryGetValue(key, out raw)) return fallback;
            var text = raw.Trim();
            if (text == "true") return true;
            if (text == "false") return false;
            return fallback;
        }

        // 极简扁平 JSON 读取：顶层对象 → 键到原始值的映射；数组原样保留成一个串。
        // 未知键读出来但不使用（§5：未知键丢弃），坏 JSON 返回 false。
        private static bool TryReadFlat(string json, out Dictionary<string, string> flat)
        {
            flat = new Dictionary<string, string>();
            if (string.IsNullOrEmpty(json)) return false;
            var i = 0;
            Skip(json, ref i);
            if (i >= json.Length || json[i] != '{') return false;
            i += 1;
            while (true)
            {
                Skip(json, ref i);
                if (i >= json.Length) return false;
                if (json[i] == '}') return true;
                if (json[i] == ',') { i += 1; continue; }
                if (json[i] != '"') return false;
                var key = ReadString(json, ref i);
                Skip(json, ref i);
                if (i >= json.Length || json[i] != ':') return false;
                i += 1;
                Skip(json, ref i);
                if (!ReadValue(json, ref i, out var value)) return false;
                flat[key] = value;
            }
        }

        private static void Skip(string json, ref int i)
        {
            while (i < json.Length && (json[i] == ' ' || json[i] == '\t' || json[i] == '\n' || json[i] == '\r')) i += 1;
        }

        private static string ReadString(string json, ref int i)
        {
            var builder = new StringBuilder();
            i += 1;                                   // 跳过开引号
            while (i < json.Length && json[i] != '"')
            {
                if (json[i] == '\\' && i + 1 < json.Length)
                {
                    i += 1;
                    var escape = json[i];
                    if (escape == 'n') builder.Append('\n');
                    else if (escape == 't') builder.Append('\t');
                    else if (escape == 'r') builder.Append('\r');
                    else builder.Append(escape);            // \" 与 \\ 还原成原字符
                }
                else builder.Append(json[i]);
                i += 1;
            }
            i += 1;
            return builder.ToString();
        }

        private static bool ReadValue(string json, ref int i, out string value)
        {
            value = string.Empty;
            if (i >= json.Length) return false;
            if (json[i] == '"') { value = ReadString(json, ref i); return true; }
            if (json[i] == '[')
            {
                var start = i;
                var depth = 0;
                while (i < json.Length)
                {
                    if (json[i] == '[') depth += 1;
                    else if (json[i] == ']')
                    {
                        depth -= 1;
                        if (depth == 0) { i += 1; break; }
                    }
                    else if (json[i] == '"') { ReadString(json, ref i); continue; }
                    i += 1;
                }
                if (i - start - 1 < 0 || json.Length == 0 || i == start) return false;   // 数组没闭合 → 坏 JSON，不许抛异常
                if (i - start - 2 < 0) return false;
                value = json.Substring(start + 1, i - start - 2).Trim();
                return true;
            }
            if (json[i] == '{')
            {
                var start = i;                                  // 未知的嵌套对象：整块跳过，不影响其余键
                var depth = 0;
                while (i < json.Length)
                {
                    if (json[i] == '{') depth += 1;
                    else if (json[i] == '}')
                    {
                        depth -= 1;
                        if (depth == 0) { i += 1; break; }
                    }
                    else if (json[i] == '"') { ReadString(json, ref i); continue; }
                    i += 1;
                }
                value = json.Substring(start, i - start);
                return true;
            }
            var end = i;
            while (end < json.Length && json[end] != ',' && json[end] != '}' && json[end] != ' ' && json[end] != '\n' && json[end] != '\r' && json[end] != '\t') end += 1;
            if (end == i) return false;
            value = json.Substring(i, end - i);
            i = end;
            return true;
        }
    }
}
