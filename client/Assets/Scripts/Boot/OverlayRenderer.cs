using Ac.UI;
using Ac.Core;
using UnityEngine;
using UnityEngine.Rendering;

namespace Ac.Boot
{
    // IMGUI 适配层：绘制布局模型，并将菜单操作交给现有输入和设置模型。
    // 昵称仍由 ScreenFlow 捕获，不创建第二条 TextField 输入源。
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
        private int _settingsTab;
        private GUIStyle _menuButton, _menuLabel, _menuTitle, _menuSmall;
        private static readonly string[] SettingsTabs = { "操控", "准星", "装备" };
        private static readonly string[] WeaponNames = { "01   手枪", "02   步枪", "03   霰弹枪" };
        private static readonly string[] WeaponNotes = { "精准点射 · 稳定可靠", "持续火力 · 中距离压制", "多弹丸散射 · 近距离爆发" };
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

        private void Fill(Rect rect, int rgb)
        {
            GUI.color = Rgb(rgb);
            GUI.DrawTexture(rect, _pixel);
            GUI.color = Color.white;
        }

        private bool MenuButton(Rect rect, string text, bool primary = false)
        {
            var hover = rect.Contains(Event.current.mousePosition);
            Fill(rect, primary ? (hover ? 0x83F5DC : 0x50DDBB) : (hover ? 0x293D50 : 0x1A2B3B));
            GUI.contentColor = primary ? Rgb(0x091A22) : Color.white;
            var pressed = GUI.Button(rect, text, _menuButton);
            GUI.contentColor = Color.white;
            return pressed;
        }

        private float SettingSlider(int index, string label, float value, float min, float max, float step, float y)
        {
            if (_sliderValues[index] != value)
            {
                _sliderValues[index] = value;
                _sliderLabels[index] = value.ToString("0.00");
            }
            GUI.Label(new Rect(24, y, 290, 26), label, _menuLabel);
            GUI.Label(new Rect(338, y, 90, 26), _sliderLabels[index], _menuSmall);
            GUI.backgroundColor = Rgb(0x50DDBB);
            var next = GUI.HorizontalSlider(new Rect(24, y + 35, 396, 22), value, min, max);
            GUI.backgroundColor = Color.white;
            return Mathf.Approximately(next, value) ? value : Mathf.Clamp(Mathf.Round(next / step) * step, min, max);
        }

        private bool SettingToggle(float y, string label, bool value)
        {
            GUI.Label(new Rect(24, y, 320, 32), label, _menuLabel);
            return MenuButton(new Rect(350, y, 70, 32), value ? "开启" : "关闭", value) ? !value : value;
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
            if (!panel.Visible)
            {
                if (_settingsHintKey != GameBootstrap.SettingsKeyCode)
                {
                    _settingsHintKey = GameBootstrap.SettingsKeyCode;
                    _settingsHint = "设置 " + _settingsHintKey + " / Esc";
                }
                if (Cursor.lockState == CursorLockMode.Locked)
                    GUI.Label(new Rect(16, 12, 260, 32), _settingsHint, Style(OverlayTextRole.Feed, OverlayAlign.Left));
                else if (MenuButton(SettingsButtonRect(Screen.width, Screen.height), "设置")) layer.ToggleSettings();
                if (layer.Flow.LobbyVisible)
                {
                    int x, y, w, h;
                    OverlayModel.LobbyActionArea(Screen.width, Screen.height, out x, out y, out w, out h);
                    var sampler = GameBootstrap.Loop == null ? null : GameBootstrap.Loop.Sampler;
                    GUI.enabled = sampler != null && layer.Flow.Lobby.IsNameValid;
                    if (MenuButton(new Rect(x, y, w, h), sampler != null && sampler.ReadyHeld ? "已准备  /  点击取消" : "准备出战  →", true)) sampler.SetReadyHeld(!sampler.ReadyHeld);
                    GUI.enabled = true;
                }
                return;
            }
            var previousMatrix = GUI.matrix;
            var scale = Mathf.Min(Screen.width / 1000f, Screen.height / 720f);
            GUI.color = new Color(0.015f, 0.025f, 0.04f, 0.94f);
            GUI.DrawTexture(new Rect(0, 0, Screen.width, Screen.height), _pixel);
            GUI.color = Color.white;
            GUI.matrix = Matrix4x4.TRS(new Vector3((Screen.width - 960 * scale) / 2, (Screen.height - 660 * scale) / 2, 0), Quaternion.identity, new Vector3(scale, scale, 1));
            try
            {
                Fill(new Rect(0, 0, 960, 660), 0x0D1824);
                Fill(new Rect(0, 0, 4, 660), 0x50DDBB);
                GUI.Label(new Rect(32, 22, 500, 24), "ANGRY CHEN  /  FIELD SETTINGS", _menuSmall);
                GUI.Label(new Rect(32, 57, 600, 52), "调整你的作战方式", _menuTitle);
                GUI.Label(new Rect(32, 114, 740, 28), "修改即时生效并自动保存  /  联机对局不会暂停", _menuSmall);
                Fill(new Rect(32, 160, 896, 1), 0x293D50);
                for (var i = 0; i < SettingsTabs.Length; i++)
                    if (MenuButton(new Rect(32, 187 + i * 58, 146, 46), SettingsTabs[i], _settingsTab == i)) { _settingsTab = i; _settingsScroll = Vector2.zero; }
                GUI.Label(new Rect(32, 415, 145, 100), "FIELD KIT\n个人作战配置\n\nESC  返回", _menuSmall);
                Fill(new Rect(198, 184, 452, 350), 0x122131);
                _settingsScroll = GUI.BeginScrollView(new Rect(198, 184, 452, 350), _settingsScroll, new Rect(0, 0, 432, _settingsTab == 1 ? 402 : 330));
                var values = PanelValues(panel);
                if (_settingsTab == 0)
                {
                    panel.SetSensitivity(SettingSlider(0, "鼠标灵敏度", values.Sensitivity, SettingsDefaults.SensitivityMin, SettingsDefaults.SensitivityMax, 0.05f, 22));
                    GUI.Label(new Rect(24, 109, 400, 195), "W A S D   移动      Shift   冲刺\nSpace   跳跃          鼠标左键   开火\nR   换弹                 Q   循环切枪\n1 / 2 / 3   直接选择枪械\nE   交互                 F   怒气技能", _menuLabel);
                }
                else if (_settingsTab == 1)
                {
                    panel.SetCrosshairScale(SettingSlider(1, "准星大小", values.CrosshairScale, SettingsDefaults.CrosshairScaleMin, SettingsDefaults.CrosshairScaleMax, 0.05f, 16));
                    panel.SetCrosshairThickness(SettingSlider(2, "线条粗细", values.CrosshairThickness, SettingsDefaults.CrosshairThicknessMin, SettingsDefaults.CrosshairThicknessMax, 1f, 88));
                    panel.SetCrosshairGap(SettingSlider(3, "中心间距", values.CrosshairGap, SettingsDefaults.CrosshairGapMin, SettingsDefaults.CrosshairGapMax, 1f, 160));
                    panel.SetCrosshairDynamic(SettingToggle(237, "动态散布反馈", values.CrosshairDynamic));
                    panel.SetColorblindSafe(SettingToggle(285, "色盲安全配色", values.ColorblindSafe));
                    if (MenuButton(new Rect(24, 337, 396, 40), "切换准星颜色")) panel.CycleCrosshairColor();
                }
                else
                {
                    var sampler = GameBootstrap.Loop == null ? null : GameBootstrap.Loop.Sampler;
                    GUI.enabled = sampler != null;
                    for (var i = 0; i < WeaponNames.Length; i++)
                    {
                        if (MenuButton(new Rect(24, 16 + i * 90, 396, 46), WeaponNames[i], sampler != null && sampler.CurrentSwitchTo == i)) sampler.RequestWeaponSlot(i);
                        GUI.Label(new Rect(28, 66 + i * 90, 390, 26), WeaponNotes[i], _menuSmall);
                    }
                    GUI.enabled = true;
                    GUI.Label(new Rect(24, 289, 400, 30), "选择装备后，返回战场继续作战", _menuSmall);
                }
                GUI.EndScrollView();
                Fill(new Rect(672, 184, 256, 350), 0x09121C);
                GUI.Label(new Rect(694, 204, 215, 30), "LIVE PREVIEW", _menuSmall);
                Fill(new Rect(694, 348, 212, 1), 0x203345);
                Fill(new Rect(800, 256, 1, 184), 0x203345);
                values = PanelValues(panel);
                _preview.ApplySettings(values);
                var hud = layer.Sources().Hud;
                _preview.SetSpread(hud == null ? Crosshair.MinSpreadDeg : hud.Crosshair.SpreadDeg);
                if (Event.current.type == EventType.Repaint)
                {
                    var item = default(OverlayItem);
                    item.X = 800; item.Y = 349;
                    item.SpreadPx = _preview.SizePx;
                    item.ThicknessPx = _preview.ThicknessPx;
                    item.GapPx = _preview.GapPx;
                    GUI.color = Rgb(_preview.CurrentColor);
                    DrawCrosshair(item);
                    GUI.color = Color.white;
                }
                GUI.Label(new Rect(694, 475, 214, 45), "实战准星 · 实时预览", _menuSmall);
                var hint = panel.Store.ReadOnlyFile ? "配置版本较新：修改仅本次有效" : panel.Hint;
                GUI.Label(new Rect(32, 548, 890, 32), string.IsNullOrEmpty(hint) ? "让视野更清晰，让每一次射击更准确。" : hint, _menuSmall);
                if (MenuButton(new Rect(32, 598, 210, 40), "恢复默认设置")) panel.RestoreDefaults();
                if (MenuButton(new Rect(672, 590, 256, 48), "返回  /  ESC", true)) layer.ToggleSettings();
            }
            finally { GUI.matrix = previousMatrix; GUI.color = Color.white; GUI.enabled = true; }
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

            ReleaseStyles();
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
            _menuLabel = new GUIStyle(Style(OverlayTextRole.Label, OverlayAlign.Left)) { fontSize = 17, wordWrap = true };
            _menuSmall = new GUIStyle(_menuLabel) { fontSize = 13 };
            _menuSmall.normal.textColor = Rgb(0x91A9BC);
            _menuTitle = new GUIStyle(_menuLabel) { fontSize = 34, fontStyle = FontStyle.Bold };
            _menuButton = new GUIStyle(_menuLabel) { alignment = TextAnchor.MiddleCenter, fontSize = 16 };
            _menuButton.hover.textColor = Color.white;
            _menuButton.active.textColor = Color.white;
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
