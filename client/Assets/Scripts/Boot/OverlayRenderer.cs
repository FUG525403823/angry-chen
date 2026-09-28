using Ac.UI;
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
        private GUIStyle _defaultStyle;
        private readonly GUIContent _content = new GUIContent();
        private readonly Rect[] _arms = new Rect[CrosshairArmCount];
        private Texture2D _pixel;
        private int _drawnFrames;

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
            // 一帧只画一次（OnGUI 每帧会被 Layout/Repaint 多次调用），只认 Repaint。
            var current = Event.current;
            if (current != null && current.type != EventType.Repaint) return;

            var width = Screen.width;
            var height = Screen.height;
            var count = layer.BuildOverlay(width, height);
            if (count <= 0) return;
            EnsureResources();

            var items = layer.Overlay.Items;
            for (var i = 0; i < count; i++) DrawItem(items[i]);
            _drawnFrames += 1;
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
                    GUI.Label(new Rect(item.X, item.Y, item.W, item.H), _content, Style(item.Role, item.Align));
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
            var thickness = 2f;
            var gap = 2f;
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
        private void EnsureResources()
        {
            if (_pixel == null)
            {
                _pixel = new Texture2D(1, 1, TextureFormat.RGBA32, false);
                _pixel.SetPixel(0, 0, Color.white);
                _pixel.Apply();
                _pixel.hideFlags = HideFlags.HideAndDontSave;
            }
            if (_styles != null) return;

            _defaultStyle = GUI.skin.label;
            _content.text = string.Empty;
            _styles = new GUIStyle[RoleCount * AlignCount];
            for (var role = 0; role < RoleCount; role++)
            {
                // 字号只从 Hud.FontPx 来（title/numeric/label/feed 四档），不另立一套
                var font = Hud.CreateFont(Hud.FontPx(OverlayModel.RoleName((OverlayTextRole)role)));
                for (var align = 0; align < AlignCount; align++)
                {
                    var style = new GUIStyle(GUI.skin.label);
                    style.fontSize = Hud.FontPx(OverlayModel.RoleName((OverlayTextRole)role));
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
            if (_pixel == null) return;
            if (Application.isPlaying) Destroy(_pixel);
            else DestroyImmediate(_pixel);
            _pixel = null;
        }
    }
}
