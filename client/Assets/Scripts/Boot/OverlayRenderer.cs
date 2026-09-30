using Ac.UI;
using Ac.Core;
using UnityEngine;
using UnityEngine.Rendering;

namespace Ac.Boot
{
    // 薄 IMGUI 适配层：把 Ac.UI.OverlayModel 的绘制项翻译成 GUI 调用。这里**没有任何界面逻辑**
    //（相位、显隐、字符串、名次全在布局模型里），也没有第二条昵称输入源（不碰 TextField）。
    //
    // 只在真的有显示设备时才画：批处理 / -nographics 下 SystemInfo.graphicsDeviceType 是 Null，
    // OnGUI 即便被调用也在这里立刻返回，一条 GUI 调用都不发（纹理与 GUIStyle 也就永不创建），
    // 因此零分配用例与无头自测不受渲染器影响。
    public sealed class OverlayRenderer : MonoBehaviour
    {
        public const string RendererName = "Ac.OverlayRenderer";

        private const int RoleCount = 4;
        private const int AlignCount = 3;
        // 十字的四段 + 中心点
        private const int CrosshairArmCount = 4;

        private PresentationLayer _layer;
        private GUIStyle[] _styles;
        private Font[] _fonts;
        private float _stylesScale = -1f;
        private GUIStyle _defaultStyle;
        private readonly GUIContent _content = new GUIContent();
        private readonly Rect[] _arms = new Rect[CrosshairArmCount];
        private Texture2D _pixel;
        private int _drawnFrames;
        private readonly Crosshair _preview = new Crosshair();
        private Vector2 _settingsScroll;
        private KeyCode _settingsHintKey = KeyCode.None;
        private string _settingsHint = string.Empty;
        private SettingsPanel _cachedPanel;
        private int _cachedApplyCount = -1;
        private SettingsSnapshot _cachedSettings;
        private readonly float[] _sliderValues = { float.NaN, float.NaN, float.NaN, float.NaN };
        private readonly string[] _sliderLabels = new string[4];

        // 真的画过多少帧（无头下恒为 0，用它区分"装配了"与"真的上屏了"）
        public int DrawnFrames { get { return _drawnFrames; } }

        public static bool HasDisplayDevice(GraphicsDeviceType deviceType)
        {
            return deviceType != GraphicsDeviceType.Null;
        }

        public bool CanRender { get { return HasDisplayDevice(SystemInfo.graphicsDeviceType); } }

        public void Bind(PresentationLayer layer) { _layer = layer; }

        private void OnGUI()
        {
            var layer = _layer;
            if (layer == null || !CanRender) return;
            EnsureResources();
            var current = Event.current;
            if (current == null || current.type == EventType.Repaint)
            {
                var count = layer.BuildOverlay(Screen.width, Screen.height);
                var items = layer.Overlay.Items;
                for (var i = 0; i < count; i++) DrawItem(items[i]);
                _drawnFrames += 1;
            }
            DrawSettings(layer);
        }

        public static Rect SettingsButtonRect(int width, int height)
        {
            return new Rect(12f, 12f, 112f, 32f);
        }

        public static bool SettingsButtonContains(Vector3 mousePosition, int width, int height)
        {
            return SettingsButtonRect(width, height).Contains(new Vector2(mousePosition.x, height - mousePosition.y));
        }

        private float SettingSlider(int index, string label, float value, float min, float max, float step)
        {
            if (_sliderValues[index] != value)
            {
                _sliderValues[index] = value;
                _sliderLabels[index] = label + "  " + value.ToString("0.00");
            }
            GUILayout.Label(_sliderLabels[index], Style(OverlayTextRole.Label, OverlayAlign.Left));
            var next = GUILayout.HorizontalSlider(value, min, max);
            return Mathf.Approximately(next, value) ? value : Mathf.Clamp(Mathf.Round(next / step) * step, min, max);
        }

        private SettingsSnapshot PanelValues(SettingsPanel panel)
        {
            if (!ReferenceEquals(_cachedPanel, panel) || _cachedApplyCount != panel.ApplyCount)
            {
                _cachedPanel = panel;
                _cachedApplyCount = panel.ApplyCount;
                _cachedSettings = panel.Values;
            }
            return _cachedSettings;
        }

        private void DrawSettings(PresentationLayer layer)
        {
            var panel = layer.SettingsPanel;
            if (panel == null) return;
            var previousFont = GUI.skin.font;
            if (_fonts != null && _fonts[(int)OverlayTextRole.Label] != null) GUI.skin.font = _fonts[(int)OverlayTextRole.Label];
            try
            {
                if (!panel.Visible)
                {
                    if (_settingsHintKey != GameBootstrap.SettingsKeyCode)
                    {
                        _settingsHintKey = GameBootstrap.SettingsKeyCode;
                        _settingsHint = "设置：" + _settingsHintKey + " / Esc 解锁";
                    }
                    if (Cursor.lockState == CursorLockMode.Locked)
                        GUI.Label(new Rect(12, 12, 240, 32), _settingsHint, Style(OverlayTextRole.Feed, OverlayAlign.Left));
                    else if (GUI.Button(SettingsButtonRect(Screen.width, Screen.height), "设置")) layer.ToggleSettings();
                    return;
                }
                GUI.color = new Color(0f, 0f, 0f, 0.85f);
                GUI.DrawTexture(new Rect(0, 0, Screen.width, Screen.height), _pixel);
                GUI.color = Color.white;
                var width = Mathf.Max(1f, Mathf.Min(620f, Screen.width - 24f));
                var height = Mathf.Max(1f, Mathf.Min(680f, Screen.height - 24f));
                GUILayout.BeginArea(new Rect((Screen.width - width) * 0.5f, (Screen.height - height) * 0.5f, width, height), GUI.skin.box);
                _settingsScroll = GUILayout.BeginScrollView(_settingsScroll);
                GUILayout.Label("设置 · 修改立即生效", Style(OverlayTextRole.Label, OverlayAlign.Left));
                var values = PanelValues(panel);
                panel.SetSensitivity(SettingSlider(0, "鼠标灵敏度", values.Sensitivity, SettingsDefaults.SensitivityMin, SettingsDefaults.SensitivityMax, 0.05f));
                panel.SetCrosshairScale(SettingSlider(1, "准星大小", values.CrosshairScale, SettingsDefaults.CrosshairScaleMin, SettingsDefaults.CrosshairScaleMax, 0.05f));
                panel.SetCrosshairThickness(SettingSlider(2, "准星粗细（像素）", values.CrosshairThickness, SettingsDefaults.CrosshairThicknessMin, SettingsDefaults.CrosshairThicknessMax, 1f));
                panel.SetCrosshairGap(SettingSlider(3, "准星间距（像素）", values.CrosshairGap, SettingsDefaults.CrosshairGapMin, SettingsDefaults.CrosshairGapMax, 1f));
                panel.SetCrosshairDynamic(GUILayout.Toggle(values.CrosshairDynamic, "动态准星（随武器散布变化）"));
                if (GUILayout.Button("切换准星颜色")) panel.CycleCrosshairColor();
                panel.SetColorblindSafe(GUILayout.Toggle(values.ColorblindSafe, "色盲安全配色（覆盖所选颜色）"));
                values = PanelValues(panel);
                _preview.ApplySettings(values);
                var hud = layer.Sources().Hud;
                _preview.SetSpread(hud == null ? Crosshair.MinSpreadDeg : hud.Crosshair.SpreadDeg);
                var previewRect = GUILayoutUtility.GetRect(160f, 100f);
                GUI.Box(previewRect, GUIContent.none);
                if (Event.current.type == EventType.Repaint)
                {
                    var item = default(OverlayItem);
                    item.X = (int)previewRect.center.x;
                    item.Y = (int)previewRect.center.y;
                    item.SpreadPx = _preview.SizePx;
                    item.ThicknessPx = _preview.ThicknessPx;
                    item.GapPx = _preview.GapPx;
                    GUI.color = Rgb(_preview.CurrentColor);
                    DrawCrosshair(item);
                    GUI.color = Color.white;
                }
                GUILayout.Label("预览与实战共用准星绘制；联机对局不会暂停。", Style(OverlayTextRole.Feed, OverlayAlign.Left));
                if (panel.Store.ReadOnlyFile) GUILayout.Label("配置来自更高版本：本次修改仅在当前运行有效。");
                if (!string.IsNullOrEmpty(panel.Hint)) GUILayout.Label(panel.Hint);
                if (GUILayout.Button("恢复默认设置")) panel.RestoreDefaults();
                if (GUILayout.Button("关闭（Esc）")) layer.ToggleSettings();
                GUILayout.EndScrollView();
                GUILayout.EndArea();
            }
            finally { GUI.skin.font = previousFont; GUI.color = Color.white; }
        }

        // 模型的口径 → IMGUI 的矩形（两者必须在这里对齐，否则"居中"会画到屏幕外）：
        //   Left  ：X = 左边缘   → rect 起点 X（样式 MiddleLeft，正文从 X 开始）
        //   Center：X = 中心线   → rect 起点 X - W/2（样式 MiddleCenter 在矩形内居中 ⇒ 中心落在 X）
        //   Right ：X = 右边缘   → rect 起点 X - W  （样式 MiddleRight 让正文右端落在 X）
        // 出包实测（2560×1440）：Center 按 X 当左边缘时，标题/波次横幅被推到 1.5 倍屏宽处（右缘外），
        // 而 Right 按 X 当左边缘时弹药行整条落在屏幕外 —— 两个症状都是这一处口径错位造成的。
        // 公开给用例：`render.overlay_in_bounds` 直接按它把每条绘制项换算成屏幕矩形做越界判定。
        public static Rect ItemRect(in OverlayItem item)
        {
            var x = item.X;
            if (item.Align == OverlayAlign.Center) x -= item.W / 2;
            else if (item.Align == OverlayAlign.Right) x -= item.W;
            return new Rect(x, item.Y, item.W, item.H);
        }

        private void DrawItem(in OverlayItem item)
        {
            var color = Rgb(item.ColorRgb);
            color.a = item.Alpha;
            GUI.color = color;
            switch (item.Kind)
            {
                case OverlayItemKind.Text:
                    _content.text = item.Text;
                    GUI.Label(ItemRect(item), _content, Style(item.Role, item.Align));
                    break;
                case OverlayItemKind.Bar:
                    DrawBar(item);
                    break;
                case OverlayItemKind.Crosshair:
                    DrawCrosshair(item);
                    break;
            }
            GUI.color = Color.white;
        }

        // 条 = 底衬 + 填充两段纯色矩形（只有一张 1×1 白纹理，不引 Unity UI/TextMeshPro 包）
        private void DrawBar(in OverlayItem item)
        {
            var color = Rgb(item.ColorRgb);
            var back = color;
            back.a = item.Alpha * 0.4f;
            GUI.color = back;
            GUI.DrawTexture(new Rect(item.X, item.Y, item.W, item.H), _pixel, ScaleMode.StretchToFill, false);
            color.a = item.Alpha;
            GUI.color = color;
            var filled = item.W * item.Fill01;
            if (filled > 0f) GUI.DrawTexture(new Rect(item.X, item.Y, filled, item.H), _pixel, ScaleMode.StretchToFill, false);
        }

        // 四段十字：臂长就是布局模型给的 SpreadPx（由散布线性映射），中心留空隙，目标态加点
        private void DrawCrosshair(in OverlayItem item)
        {
            var size = item.SpreadPx < 1f ? 1f : item.SpreadPx;
            var thickness = item.ThicknessPx;
            var gap = item.GapPx;
            var x = item.X;
            var y = item.Y;
            _arms[0] = new Rect(x - size - gap, y - thickness * 0.5f, size, thickness);   // 左
            _arms[1] = new Rect(x + gap, y - thickness * 0.5f, size, thickness);          // 右
            _arms[2] = new Rect(x - thickness * 0.5f, y - size - gap, thickness, size);   // 上
            _arms[3] = new Rect(x - thickness * 0.5f, y + gap, thickness, size);          // 下
            for (var i = 0; i < CrosshairArmCount; i++) GUI.DrawTexture(_arms[i], _pixel, ScaleMode.StretchToFill, false);
            if (item.Target) GUI.DrawTexture(new Rect(x - 1f, y - 1f, 2f, 2f), _pixel, ScaleMode.StretchToFill, false);
        }

        private GUIStyle Style(OverlayTextRole role, OverlayAlign align)
        {
            if (_styles == null) return _defaultStyle;
            var index = (int)role * AlignCount + (int)align;
            return _styles[index];
        }

        // 资源只在**第一次真的要画**时创建（无头下永不创建，所以也不会泄漏纹理/字体）。
        // 字号随视口高度缩放（Hud.ScaleFor）：缩放值变了就重建样式与字体，否则 2560×1440 上还是 16~32px 的小字。
        private void EnsureResources()
        {
            if (_pixel == null)
            {
                _pixel = new Texture2D(1, 1, TextureFormat.RGBA32, false);
                _pixel.SetPixel(0, 0, Color.white);
                _pixel.Apply();
                _pixel.hideFlags = HideFlags.HideAndDontSave;
            }
            var scale = Hud.ScaleFor(Screen.height);
            if (_styles != null && Mathf.Approximately(_stylesScale, scale)) return;

            _defaultStyle = GUI.skin.label;
            _content.text = string.Empty;
            _styles = new GUIStyle[RoleCount * AlignCount];
            _fonts = new Font[RoleCount];
            _stylesScale = scale;
            var viewportHeight = Screen.height;
            for (var role = 0; role < RoleCount; role++)
            {
                // 字号只从 Hud 来（title/numeric/label/feed 四档 + 分辨率缩放），不另立一套
                var sizePx = Hud.ScaledFontPx(OverlayModel.RoleName((OverlayTextRole)role), viewportHeight);
                var font = Hud.CreateFont(sizePx);
                _fonts[role] = font;
                for (var align = 0; align < AlignCount; align++)
                {
                    var style = new GUIStyle(GUI.skin.label);
                    style.fontSize = sizePx;
                    if (font != null) style.font = font;
                    style.alignment = AlignOf((OverlayAlign)align);
                    style.richText = false;
                    style.wordWrap = false;
                    style.padding = new RectOffset(0, 0, 0, 0);
                    style.normal.textColor = Color.white;
                    _styles[role * AlignCount + align] = style;
                }
            }
        }

        // 动态字体是运行期造的，视口变化重建时要把旧的销毁，否则每换一次分辨率就漏一批。
        private void ReleaseStyles()
        {
            if (_fonts != null)
            {
                for (var i = 0; i < _fonts.Length; i++)
                {
                    if (_fonts[i] == null) continue;
                    Destroy(_fonts[i]);
                    _fonts[i] = null;
                }
            }
            _styles = null;
            _stylesScale = -1f;
        }

        private static TextAnchor AlignOf(OverlayAlign align)
        {
            if (align == OverlayAlign.Center) return TextAnchor.UpperCenter;
            if (align == OverlayAlign.Right) return TextAnchor.UpperRight;
            return TextAnchor.UpperLeft;
        }

        public static Color Rgb(int rgb)
        {
            return new Color(
                ((rgb >> 16) & 0xFF) / 255f,
                ((rgb >> 8) & 0xFF) / 255f,
                (rgb & 0xFF) / 255f,
                1f);
        }

        private void OnDestroy()
        {
            ReleaseStyles();
            if (_pixel == null) return;
            if (Application.isPlaying) Destroy(_pixel);
            else DestroyImmediate(_pixel);
            _pixel = null;
        }
    }
}
