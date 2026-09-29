using System;
using System.Collections.Generic;
using Ac.Boot;
using Ac.Core;
using Ac.Net;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;
using UnityEngine.Rendering;

namespace Ac.Tests
{
    // B1 上屏的守门用例：HUD/准星/大厅/结算/波间/调试面板此前都是纯数据类（全仓 OnGUI 命中 0），
    // 这里把"布局模型的绘制项内容与数量"钉死——把任一条接线抽掉（少一个绘制项、少一行文字、
    // 颜色取错档、名次顺序反了）就必须变红。适配层的 GUI 调用本身在无头环境里测不到，
    // 所以断言打在绘制项上，并额外证明"没有显示设备就不画"。
    internal static class RenderSuite
    {
        public static void Register()
        {
            SelfTest.Add("render.renderer_attached", ChecksRendererAttached);
            SelfTest.Add("render.lobby_items", ChecksLobbyItems);
            SelfTest.Add("render.combat_hud_items", ChecksCombatHudItems);
            SelfTest.Add("render.crosshair_palette", ChecksCrosshairPalette);
            SelfTest.Add("render.kill_feed_lines", ChecksKillFeedLines);
            SelfTest.Add("render.intermission_countdown", ChecksIntermissionCountdown);
            SelfTest.Add("render.results_table", ChecksResultsTable);
            SelfTest.Add("render.debug_panel_items", ChecksDebugPanelItems);
            SelfTest.Add("render.no_viewport_no_items", ChecksNoViewportNoItems);
            SelfTest.Add("render.steady_state_zero_alloc", ChecksSteadyStateZeroAlloc);
            SelfTest.Add("render.overlay_in_bounds", ChecksOverlayInBounds);
        }

        // 分辨率 × 相位：**每一条绘制项**都必须完全落在视口内。这条缝此前整体错位 ——
        // OverlayItem.X 对 Center 是"中心线"、对 Right 是"右边缘"，而渲染侧一律按"左边缘"起矩形，
        // 于是居中标题/波次被推到 1.5 倍屏宽处（右缘外裁掉）、右对齐的弹药行整条出屏
        // （用户实跑反馈"波次、怒气等文字越界或者看不清"）。同一条用例钉住字号随视口缩放。
        private static void ChecksOverlayInBounds()
        {
            // 缩放口径本身（Hud.ScaleFor / ScaledFontPx）
            SelfTest.Equal(1000, (long)(Hud.ScaleFor(1080) * 1000.0));
            SelfTest.Equal(Hud.FontNumericPx, (long)Hud.ScaledFontPx("numeric", 1080));
            SelfTest.True(Hud.ScaledFontPx("numeric", 1440) > Hud.ScaledFontPx("numeric", 1080),
                "字号必须随视口高度变大（看不清的根因）",
                Hud.ScaledFontPx("numeric", 1080) + " -> " + Hud.ScaledFontPx("numeric", 1440));
            SelfTest.Equal(1, (long)Hud.ScaleFor(0));                       // 无高度信息时回退 1.0
            SelfTest.True(Hud.ScaleFor(100000) <= Hud.MaxUiScale, "缩放有上限", Hud.ScaleFor(100000).ToString());

            var viewports = new[] { new[] { 1280, 720 }, new[] { 1920, 1080 }, new[] { 2560, 1440 } };
            var phases = new[] { Hud.PhaseLobby, Hud.PhasePlaying, Hud.PhaseIntermission, Hud.PhaseEnded };
            foreach (var phase in phases)
            {
                using (var rig = new Rig())
                {
                    var wave = phase == Hud.PhasePlaying || phase == Hud.PhaseIntermission || phase == Hud.PhaseEnded ? 3 : 0;
                    rig.Loop.OnPacket(MatchStateHeader(),
                        MatchStateBytes(phase, (byte)wave, 0, new ushort[] { 1, 2 }, new[] { "牧羊人", "b" }, new[] { true, true }));
                    rig.Loop.Frame(1000.0 / 60.0);
                    foreach (var vp in viewports)
                    {
                        var w = vp[0];
                        var h = vp[1];
                        var count = rig.Layer.BuildOverlay(w, h);
                        SelfTest.True(count > 0, "有视口就必须产出绘制项 phase=" + phase, "一条都没有");
                        var items = rig.Layer.Overlay.Items;
                        for (var i = 0; i < count; i++)
                        {
                            var item = items[i];
                            var rect = OverlayRenderer.ItemRect(item);
                            var inside = rect.xMin >= -0.5f && rect.yMin >= -0.5f &&
                                rect.xMax <= w + 0.5f && rect.yMax <= h + 0.5f;
                            SelfTest.True(inside,
                                "绘制项必须在视口内（phase=" + phase + " " + w + "x" + h + " kind=" + item.Kind +
                                " align=" + item.Align + " text=" + item.Text + "）",
                                "rect=" + rect.xMin + "," + rect.yMin + "," + rect.xMax + "," + rect.yMax);
                        }
                    }
                }
            }
        }

        private const int ViewW = 1280;
        private const int ViewH = 720;

        // 装配 + 帧回路的最小组合（与 PresentationSuite 的 Rig 同形）：Dispose 里两条一起丢掉，
        // 并把 LightingRig 改过的 RenderSettings 放回去（不给后面的用例留"上一局的天光"）。
        private sealed class Rig : IDisposable
        {
            internal PresentationLayer Layer;
            internal GameLoop Loop;

            private readonly bool _fog;
            private readonly UnityEngine.Rendering.AmbientMode _ambientMode;
            private readonly UnityEngine.Color _ambientLight;
            private readonly float _ambientIntensity;
            private readonly UnityEngine.FogMode _fogMode;
            private readonly float _fogStart;
            private readonly float _fogEnd;
            private readonly UnityEngine.Color _fogColor;

            internal Rig()
            {
                _fog = RenderSettings.fog;
                _ambientMode = RenderSettings.ambientMode;
                _ambientLight = RenderSettings.ambientLight;
                _ambientIntensity = RenderSettings.ambientIntensity;
                _fogMode = RenderSettings.fogMode;
                _fogStart = RenderSettings.fogStartDistance;
                _fogEnd = RenderSettings.fogEndDistance;
                _fogColor = RenderSettings.fogColor;

                var settings = SettingsDefaults.Default();
                Layer = PresentationLayer.Create(settings);
                Loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
                Loop.LocalPlayerId = 1;
                Layer.Attach(Loop);
                Layer.ApplySettings(settings);
                // 生产里 GameBootstrap 做的就是这三行：接上之后相位才会从包走到屏幕流
                WireSeams();
            }

            internal void WireSeams()
            {
                Loop.Fx = Layer.FxSink;
                Loop.Draw = Layer.DrawSink;
                Loop.Overlay = Layer.OverlaySink;
            }

            // 相位只从 type=10 的载荷进来（与生产同一条路）：包 → 帧回路 → overlay 段 → 屏幕流。
            internal void PushState(byte phase, byte wave, ushort intermissionMs)
            {
                Loop.OnPacket(MatchStateHeader(), TwoPlayersBytes(phase, wave, intermissionMs));
                Loop.Frame(1000.0 / 60.0);
            }

            // pid <= 0 的行整行不渲染：需要一条"表里有玩家、但都不可见"的载荷。
            internal void PushStateZeroPid(byte phase, byte wave)
            {
                Loop.OnPacket(MatchStateHeader(), MatchStateBytes(phase, (byte)wave, 0, new ushort[] { 0 }, new[] { "ghost" }, new[] { false }));
                Loop.Frame(1000.0 / 60.0);
            }

            internal Hud Hud { get { return Loop.Hud; } }

            public void Dispose()
            {
                if (Layer != null) Layer.Dispose();
                Layer = null;
                Loop = null;
                RenderSettings.fog = _fog;
                RenderSettings.ambientMode = _ambientMode;
                RenderSettings.ambientLight = _ambientLight;
                RenderSettings.ambientIntensity = _ambientIntensity;
                RenderSettings.fogMode = _fogMode;
                RenderSettings.fogStartDistance = _fogStart;
                RenderSettings.fogEndDistance = _fogEnd;
                RenderSettings.fogColor = _fogColor;
            }
        }

        // ---- 几何/内容查询（用例只读绘制项，不碰 GUI） ----

        private static OverlayModel Build(Rig rig)
        {
            rig.Layer.BuildOverlay(ViewW, ViewH);
            return rig.Layer.Overlay;
        }

        private static int CountOf(OverlayModel model, OverlayItemKind kind)
        {
            var count = 0;
            var items = model.Items;
            for (var i = 0; i < model.Count; i++) if (items[i].Kind == kind) count += 1;
            return count;
        }

        private static bool TryFindText(OverlayModel model, string exact, out OverlayItem found)
        {
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != OverlayItemKind.Text || !string.Equals(items[i].Text, exact, StringComparison.Ordinal)) continue;
                found = items[i];
                return true;
            }
            found = default(OverlayItem);
            return false;
        }

        private static bool HasText(OverlayModel model, string exact)
        {
            OverlayItem item;
            return TryFindText(model, exact, out item);
        }

        private static bool HasTextPrefix(OverlayModel model, string prefix)
        {
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != OverlayItemKind.Text || items[i].Text == null) continue;
                if (items[i].Text.StartsWith(prefix, StringComparison.Ordinal)) return true;
            }
            return false;
        }

        private static int IndexOfText(OverlayModel model, string prefix)
        {
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != OverlayItemKind.Text || items[i].Text == null) continue;
                if (items[i].Text.StartsWith(prefix, StringComparison.Ordinal)) return i;
            }
            return -1;
        }

        private static bool TryFindKind(OverlayModel model, OverlayItemKind kind, out OverlayItem found)
        {
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != kind) continue;
                found = items[i];
                return true;
            }
            found = default(OverlayItem);
            return false;
        }

        private static bool HasBarWithFill(OverlayModel model, float fill)
        {
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != OverlayItemKind.Bar) continue;
                if (Math.Abs(items[i].Fill01 - fill) < 1e-4f) return true;
            }
            return false;
        }

        private static bool HasFullScreenBar(OverlayModel model)
        {
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != OverlayItemKind.Bar) continue;
                if (items[i].X == 0 && items[i].Y == 0 && items[i].W == ViewW && items[i].H == ViewH) return true;
            }
            return false;
        }

        // ---- 夹具 ----

        private static void Push(Hud hud, Ac.Net.EventType type, int hitFlags, int targetId)
        {
            var hudEvent = default(HudEvent);
            hudEvent.Type = type;
            hudEvent.HitFlags = hitFlags;
            hudEvent.TargetId = targetId;
            hudEvent.Wave = 1;
            hud.Apply(Sample(Hud.PhasePlaying, 255));
            hud.PushEvent(hudEvent);
        }

        private static HudSample Sample(byte phase, int hpRatio255)
        {
            var sample = default(HudSample);
            sample.Phase = phase;
            sample.HpRatio255 = hpRatio255;
            return sample;
        }

        private static PacketHeader MatchStateHeader()
        {
            var header = default(PacketHeader);
            header.Type = PacketType.MatchState;
            return header;
        }

        // ---- 用例 ----

        private static void ChecksRendererAttached()
        {
            var rig = new Rig();
            try
            {
                var layer = rig.Layer;
                SelfTest.True(layer.Overlay != null, "布局模型必须被构造", "是 null");
                SelfTest.True(layer.OverlayRenderer != null, "IMGUI 适配层必须被装配", "是 null");
                var found = GameObject.Find(OverlayRenderer.RendererName);
                SelfTest.True(found != null, "适配层要在场景里有实体", "找不到");
                SelfTest.True(found == null || found.transform.parent == layer.Root.transform, "适配层挂在装配根下", "挂错了");

                // 只有真的有显示设备才画：-nographics / 批处理下 SystemInfo 是 Null Device
                SelfTest.True(!OverlayRenderer.HasDisplayDevice(GraphicsDeviceType.Null), "Null Device 不许画", "会去画");
                SelfTest.True(OverlayRenderer.HasDisplayDevice(GraphicsDeviceType.Direct3D11), "真设备要画", "不画");
                SelfTest.True(layer.OverlayRenderer.CanRender == OverlayRenderer.HasDisplayDevice(SystemInfo.graphicsDeviceType),
                    "CanRender 只由显示设备决定", SystemInfo.graphicsDeviceType.ToString());
                SelfTest.Equal(0, (long)layer.OverlayRenderer.DrawnFrames);        // 无头下 OnGUI 一帧都没画

                // 绘制项的输入来自既有对象（帧回路 / 屏幕流），不是自己另造一份
                rig.Loop.OnPacket(MatchStateHeader(), MatchStateBytes(Hud.PhasePlaying, 3, 0, new ushort[] { 1, 2 }, new[] { "a", "b" }, new[] { true, false }));
                rig.Loop.Frame(1000.0 / 60.0);                                     // overlay 段把相位推给屏幕流
                var sources = layer.Sources();
                SelfTest.True(ReferenceEquals(sources.Hud, rig.Loop.Hud), "Sources 的 HUD 就是帧回路的 HUD", "不是");
                SelfTest.True(ReferenceEquals(sources.Lobby, layer.Flow.Lobby), "Sources 的大厅就是屏幕流的大厅", "不是");
                SelfTest.True(ReferenceEquals(sources.Debug, layer.DebugPanel), "Sources 的面板就是调试面板", "不是");
                SelfTest.True(sources.Players != null && sources.Players.Length == 2, "队伍表来自最近一次 match state", "是 null");

                // TickOverlay 不许偷偷每帧重建绘制项：只有显式 BuildOverlay 才计数
                var builds = layer.OverlayBuilds;
                for (var i = 0; i < 3; i++) rig.Loop.Frame(1000.0 / 60.0);
                SelfTest.Equal(builds, (long)layer.OverlayBuilds);
                SelfTest.True(layer.BuildOverlay(ViewW, ViewH) > 0, "有视口时对局相位必须产出绘制项", "一条都没有");
                SelfTest.Equal(builds + 1, (long)layer.OverlayBuilds);

                // 销毁后场景里不许留下适配层：按组件引用判（按名字找会因为别的装配根同名而误判）
                var gone = new Rig();
                var goneRenderer = gone.Layer.OverlayRenderer;
                var goneObject = goneRenderer == null ? null : goneRenderer.gameObject;
                gone.Dispose();
                SelfTest.True(goneRenderer == null, "销毁后适配层组件必须失效", "还在");
                SelfTest.True(goneObject == null, "销毁后适配层的实体必须从场景里消失", "还在");
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksLobbyItems()
        {
            var rig = new Rig();
            try
            {
                var flow = rig.Layer.Flow;
                rig.PushState(Hud.PhaseLobby, 0, 0);
                var model = Build(rig);

                // 大厅：标题 + 昵称 + 合法性 + 房间码 + 准备行 + 备战条 + 提示 + 队伍表头 + 2 行 = 10
                SelfTest.Equal(10, (long)model.Count);
                SelfTest.Equal(9, (long)CountOf(model, OverlayItemKind.Text));
                SelfTest.Equal(1, (long)CountOf(model, OverlayItemKind.Bar));
                SelfTest.Equal(0, (long)CountOf(model, OverlayItemKind.Crosshair));   // 大厅没有准星
                SelfTest.True(HasText(model, OverlayModel.LobbyTitle), "大厅标题要画出来", "没有");
                SelfTest.True(HasText(model, "昵称: (未设置)"), "昵称行要显示当前昵称", "没有");
                SelfTest.True(HasText(model, OverlayModel.NameInvalidText), "空昵称必须显示为不合法", "没有");
                SelfTest.True(HasText(model, "房间码: (未输入)"), "房间码行要画出来", "没有");
                SelfTest.True(HasText(model, "准备 1/2 · 主机"), "准备行要带人数与主机标记", "没有");
                SelfTest.True(HasText(model, OverlayModel.RosterHeader), "队伍表头要画出来", "没有");
                SelfTest.True(HasText(model, "#1 a 步枪 已准备"), "队伍表第 1 行", "没有");
                SelfTest.True(HasText(model, "#2 b 步枪 未准备"), "队伍表第 2 行", "没有");
                SelfTest.True(HasBarWithFill(model, 0.5f), "备战条填充 = 已准备/总数", "没有");

                // 昵称：只显示清洗后的值，且合法性跟着变
                flow.Lobby.SetName("bob");
                model = Build(rig);
                SelfTest.True(HasText(model, "昵称: bob"), "键入后的昵称要显示出来", "没有");
                SelfTest.True(HasText(model, OverlayModel.NameValidText), "合法昵称必须显示为合法", "没有");
                SelfTest.True(!HasText(model, OverlayModel.NameInvalidText), "合法时不许再显示不合法提示", "还在");

                flow.Lobby.SetName("a<b>&\"'c");
                model = Build(rig);
                SelfTest.True(HasText(model, "昵称: abc"), "显示的是清洗后的昵称（<>\"'& 被剥离）", "没有");

                flow.Lobby.SetRoomCode("ab3x");
                model = Build(rig);
                SelfTest.True(HasText(model, "房间码: AB3X"), "房间码按冻结规则归一化后显示", "没有");

                // 没有玩家表的行（pid <= 0 整行不渲染）⇒ 队伍表整块消失
                rig.PushStateZeroPid(Hud.PhaseLobby, 0);
                model = Build(rig);
                SelfTest.Equal(7, (long)model.Count);                              // 表头与两行都不画了
                SelfTest.True(!HasText(model, OverlayModel.RosterHeader), "没有可见玩家时不画队伍表", "还在");
                SelfTest.True(!HasText(model, "#0 ghost 步枪 未准备"), "pid <= 0 的行整行不渲染", "画了");

                // 载入相位：只有一条标题（"装配了但没渲染器"之前这条屏幕也不存在）
                rig.PushState(Hud.PhaseLoading, 0, 0);
                model = Build(rig);
                SelfTest.Equal(1, (long)model.Count);
                SelfTest.True(HasText(model, "载入中"), "载入相位要画出来", "没有");
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksCombatHudItems()
        {
            var rig = new Rig();
            try
            {
                rig.PushState(Hud.PhasePlaying, 3, 0);
                var hud = rig.Loop.Hud;

                var sample = default(HudSample);
                sample.Phase = Hud.PhasePlaying;
                sample.HpRatio255 = 128;         // → 显示 50%
                sample.Mag = 7;
                sample.MagSize = 30;
                sample.Reserve = 60;
                sample.Rage = 100;               // 满怒（未开狂暴）
                sample.Wave = 3;
                sample.TargetInSight = true;
                hud.Tick(UiThrottle.NumericRefreshMs * 2f);   // 打开 10Hz 窗口：数值类允许慢 100ms，但不能丢
                hud.Apply(sample);

                var model = Build(rig);
                SelfTest.True(HasText(model, "HP 50"), "血量按 1% 精度显示", "没有");
                SelfTest.True(HasText(model, "7 / 60"), "弹药 / 备弹", "没有");
                SelfTest.True(HasText(model, "怒气 100"), "怒气值", "没有");
                SelfTest.True(HasText(model, "波次 3"), "波次", "没有");
                SelfTest.True(HasBarWithFill(model, 0.5f), "血条填充 = 血量", "没有");
                SelfTest.True(HasBarWithFill(model, 1f), "怒气条填充 = 怒气/100", "没有");
                SelfTest.True(!HasTextPrefix(model, "昵称: "), "对局里不画大厅的昵称行", "还在");
                SelfTest.True(!HasText(model, OverlayModel.NameInvalidText), "对局里不画大厅的合法性提示", "还在");

                // 低弹比按**该武器的**弹匣容量算：7/30 → 低弹色
                OverlayItem ammo;
                SelfTest.True(TryFindText(model, "7 / 60", out ammo), "弹药行必须存在", "没有");
                SelfTest.Equal((long)Hud.ColorLowAmmo, (long)ammo.ColorRgb);

                // 准星：中心 + 扩散尺寸 + 压在目标上的档
                OverlayItem crosshair;
                SelfTest.True(TryFindKind(model, OverlayItemKind.Crosshair, out crosshair), "对局里必须有准星", "没有");
                SelfTest.Equal(ViewW / 2, (long)crosshair.X);
                SelfTest.Equal(ViewH / 2, (long)crosshair.Y);
                SelfTest.True(crosshair.Target, "压在目标上的准星必须标记 Target", "没标");
                SelfTest.Equal((long)Hud.ColorTarget, (long)crosshair.ColorRgb);
                SelfTest.True(Math.Abs(crosshair.SpreadPx - hud.Crosshair.SizePx) < 1e-4, "准星尺寸来自散布映射", crosshair.SpreadPx.ToString("R"));

                // 倒地：全屏遮罩 + 倒地文案（准星同时隐藏）
                sample.Downed = true;
                hud.Apply(sample);
                model = Build(rig);
                SelfTest.True(HasText(model, OverlayModel.DownedText), "倒地要画遮罩文案", "没有");
                SelfTest.Equal(0, (long)CountOf(model, OverlayItemKind.Crosshair));   // §5(c)：倒地一律隐藏准星
                SelfTest.True(HasFullScreenBar(model), "倒地遮罩是一块整屏的条", "没有");
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksCrosshairPalette()
        {
            var rig = new Rig();
            try
            {
                rig.PushState(Hud.PhasePlaying, 1, 0);
                var hud = rig.Loop.Hud;
                var sample = Sample(Hud.PhasePlaying, 255);
                hud.Apply(sample);

                OverlayItem crosshair;
                SelfTest.True(TryFindKind(Build(rig), OverlayItemKind.Crosshair, out crosshair), "对局里必须有准星", "没有");
                SelfTest.Equal((long)SettingsDefaults.CrosshairColor, (long)crosshair.ColorRgb);   // 设置里的准星色是行为读者
                SelfTest.True(!crosshair.Target, "没有目标时不许是 Target 档", "是");

                // 色盲安全档：不改调色板数据，而是在既有调色板里选对比最大的一档
                var expected = SettingsDefaults.MostContrastingCrosshairColor();
                SelfTest.True(expected != SettingsDefaults.CrosshairColor, "色盲档必须与默认档不同（否则这条断言没有判别力）", expected.ToString("X6"));
                hud.Crosshair.SetPalette(SettingsDefaults.CrosshairColor, true);
                hud.Apply(sample);
                SelfTest.True(TryFindKind(Build(rig), OverlayItemKind.Crosshair, out crosshair), "色盲档下准星还在", "没了");
                SelfTest.Equal((long)expected, (long)crosshair.ColorRgb);
                SelfTest.True(hud.Crosshair.ColorblindSafe, "色盲档要记在准星状态上", "没记");

                // 压在目标上：颜色让位给调色板的固定档（可读性优先）
                hud.Crosshair.SetPalette(SettingsDefaults.CrosshairColor, true);
                sample.TargetInSight = true;
                hud.Apply(sample);
                SelfTest.True(TryFindKind(Build(rig), OverlayItemKind.Crosshair, out crosshair), "目标态下准星还在", "没了");
                SelfTest.Equal((long)Hud.ColorTarget, (long)crosshair.ColorRgb);
                SelfTest.True(crosshair.Target, "目标态必须标记 Target", "没标");
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksKillFeedLines()
        {
            var rig = new Rig();
            try
            {
                rig.PushState(Hud.PhasePlaying, 2, 0);
                var hud = rig.Loop.Hud;
                hud.Apply(Sample(Hud.PhasePlaying, 255));

                OverlayItem item;
                SelfTest.True(!HasTextPrefix(Build(rig), "击杀 #"), "没有击杀时不许有记录行", "有");

                Push(hud, Ac.Net.EventType.SheepKilled, 0, 5);
                var model = Build(rig);
                SelfTest.True(TryFindText(model, "击杀 #5", out item), "击杀记录行必须画出来", "没有");
                SelfTest.Equal((long)Hud.ColorNormal, (long)item.ColorRgb);
                SelfTest.True(Math.Abs(item.Alpha - 1f) < 1e-4, "刚出现的记录不透明", item.Alpha.ToString("R"));

                Push(hud, Ac.Net.EventType.PlayerHit, CombatFlags.HitFlagKilled | CombatFlags.HitFlagHeadshot, 6);
                model = Build(rig);
                SelfTest.True(TryFindText(model, "击杀 #6 爆头", out item), "爆头记录行必须带标记", "没有");
                SelfTest.Equal((long)Hud.ColorTarget, (long)item.ColorRgb);        // 爆头单独样式
                SelfTest.Equal(2, (long)FeedRows(model));

                // 3000ms 之后整行消失（KillFeed 的窗口由 Tick 推进）
                hud.Tick(3001f);
                model = Build(rig);
                SelfTest.Equal(0, (long)FeedRows(model));
                SelfTest.True(!HasTextPrefix(model, "击杀 #"), "过期的记录不许再画", "还在");
            }
            finally { rig.Dispose(); }
        }

        private static int FeedRows(OverlayModel model)
        {
            var count = 0;
            var items = model.Items;
            for (var i = 0; i < model.Count; i++)
            {
                if (items[i].Kind != OverlayItemKind.Text || items[i].Text == null) continue;
                if (items[i].Text.StartsWith("击杀 #", StringComparison.Ordinal)) count += 1;
            }
            return count;
        }

        private static void ChecksIntermissionCountdown()
        {
            var rig = new Rig();
            try
            {
                rig.PushState(Hud.PhaseIntermission, 3, 12340);
                var model = Build(rig);
                SelfTest.True(HasText(model, OverlayModel.IntermissionTitle), "波间标题", "没有");
                SelfTest.True(HasText(model, "12.3 s"), "倒计时按权威值显示（100ms 精度）", "没有");
                SelfTest.True(HasText(model, "第 3 波准备中"), "波次行", "没有");
                SelfTest.True(HasText(model, OverlayModel.SkipEnabledText), "≤15s 时可跳过", "没有");
                SelfTest.True(!HasText(model, OverlayModel.SkipDisabledText), "可跳过时不许显示不可跳过", "还在");
                SelfTest.True(HasBarWithFill(model, 12340 / (float)Intermission.IntermissionInitialMs), "倒计时条按剩余/初值填充", "没有");
                SelfTest.True(HasText(model, "#1 a 步枪 已准备"), "波间也要看得见队伍表", "没有");

                rig.PushState(Hud.PhaseIntermission, 3, 16000);
                model = Build(rig);
                SelfTest.True(HasText(model, "16.0 s"), "倒计时要跟着服务器值走", "没有");
                SelfTest.True(HasText(model, OverlayModel.SkipDisabledText), ">15s 时不可跳过", "没有");
                SelfTest.True(!HasText(model, OverlayModel.SkipEnabledText), "不可跳过时不许显示可跳过", "还在");
            }
            finally { rig.Dispose(); }
        }

        private static MatchRecord Match(string matchId, int kills, int waveReached)
        {
            var record = default(MatchRecord);
            record.MatchId = matchId;
            record.WaveReached = waveReached;
            record.WinnerTeam = 1;
            record.PlayerCount = 1;
            record.Players = new[] { new PlayerRecord { Name = "a", Kills = kills } };
            return record;
        }

        private static void ChecksResultsTable()
        {
            var rig = new Rig();
            try
            {
                var flow = rig.Layer.Flow;
                rig.PushState(Hud.PhaseEnded, 5, 0);
                SelfTest.True(flow.ResultsVisible, "ended 相位必须显示结算", "没显示");

                var summary = default(ResultsSummary);
                summary.MatchId = "m2";
                summary.WaveReached = 5;
                summary.WinnerTeam = 1;
                summary.DurationMs = 60000;
                summary.Records = new[] { Match("m1", 4, 3), Match("m2", 9, 5) };
                flow.Results.Show(summary);

                var model = Build(rig);
                SelfTest.True(HasText(model, OverlayModel.ResultsTitle), "结算标题", "没有");
                SelfTest.True(HasText(model, "到达波次 5 · 胜方 队伍 1"), "结算摘要", "没有");
                SelfTest.True(HasText(model, "1. m2 · 击杀 9 · 波 5"), "名次表第 1 行", "没有");
                SelfTest.True(HasText(model, "2. m1 · 击杀 4 · 波 3"), "名次表第 2 行", "没有");
                var first = IndexOfText(model, "1. m2");
                var second = IndexOfText(model, "2. m1");
                SelfTest.True(first >= 0 && second > first, "名次顺序 = Results 的排序结果（击杀降序）", first + "/" + second);
                SelfTest.True(!HasText(model, OverlayModel.StaleBannerText), "榜单可用时不显示兜底提示", "还在");

                // 榜单为空（接口不可达）→ 本地摘要兜底 + "榜单暂不可用"
                var stale = default(ResultsSummary);
                stale.MatchId = "m3";
                stale.WaveReached = 2;
                stale.WinnerTeam = 0;
                flow.Results.Show(stale);
                model = Build(rig);
                SelfTest.True(HasText(model, OverlayModel.StaleBannerText), "空榜单必须显示兜底提示", "没有");
                SelfTest.True(HasText(model, "1. m3 · 击杀 0 · 波 2"), "兜底行来自本局摘要", "没有");

                // 还有分数行之外的队伍表：结算屏上 RosterVisible（对局中才交给 HUD）
                SelfTest.True(HasText(model, "#1 a 步枪 已准备"), "结算屏也要看得见队伍表", "没有");
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksDebugPanelItems()
        {
            var rig = new Rig();
            try
            {
                rig.WireSeams();
                rig.PushState(Hud.PhaseLobby, 0, 0);
                var model = Build(rig);
                SelfTest.True(!HasTextPrefix(model, "playerTick: "), "面板默认关着，不许画信息行", "画了");
                var withoutPanel = model.Count;

                rig.Layer.ToggleDebugPanel();
                for (var i = 0; i < 20; i++) rig.Loop.Frame(20.0);     // 面板是 250ms 节拍：至少刷一次
                model = Build(rig);
                SelfTest.True(rig.Layer.DebugPanel.RefreshCount > 0, "面板可见时至少要刷新一次", "没刷");
                SelfTest.True(HasTextPrefix(model, "playerTick: "), "面板信息行必须画出来", "没有");
                SelfTest.True(HasTextPrefix(model, "versionLine: "), "面板的版本行", "没有");
                SelfTest.True(HasTextPrefix(model, DebugPanel.FieldNames[11] + ": "), "面板字段顺序冻结（第 12 项是 versionLine）", "没有");
                SelfTest.Equal(withoutPanel + DebugPanel.FieldCount + 1, (long)model.Count);   // 12 行 + 1 块背景
                var foundShade = false;
                for (var i = 0; i < model.Count; i++)
                {
                    var item = model.Items[i];
                    if (item.Kind == OverlayItemKind.Bar && item.ColorRgb == OverlayModel.PanelShadeRgb) foundShade = true;
                }
                SelfTest.True(foundShade, "面板要有自己的底衬（否则文字压在场景上看不清）", "没有");

                rig.Layer.ToggleDebugPanel();
                model = Build(rig);
                SelfTest.True(!HasTextPrefix(model, "playerTick: "), "关掉面板后不许再画信息行", "还在");
                SelfTest.Equal(withoutPanel, (long)model.Count);
            }
            finally { rig.Dispose(); }
        }

        private static void ChecksNoViewportNoItems()
        {
            var rig = new Rig();
            try
            {
                rig.PushState(Hud.PhasePlaying, 1, 0);
                // 没有视口 = 没有显示设备：一条绘制项都不产（-nographics 下的第二道闸）
                SelfTest.Equal(0, (long)rig.Layer.BuildOverlay(0, 0));
                SelfTest.Equal(0, (long)rig.Layer.BuildOverlay(ViewW, 0));
                SelfTest.Equal(0, (long)rig.Layer.BuildOverlay(0, ViewH));
                SelfTest.Equal(0, (long)rig.Layer.Overlay.Count);
                SelfTest.True(rig.Layer.BuildOverlay(ViewW, ViewH) > 0, "有视口就必须产出绘制项", "一条都没有");
                SelfTest.True(rig.Layer.Overlay.Count <= OverlayModel.ItemCapacity, "绘制项不许越过固定缓冲", rig.Layer.Overlay.Count.ToString());
            }
            finally { rig.Dispose(); }
        }

        // 布局模型在真实装配上逐帧产出（含字符串缓存），稳态必须 0 B/帧：抽掉任一条缓存
        //（每帧重拼血量/弹药/怒气/倒计时）这条闸就红。
        //
        // 分配读数走 AllocMeter（引擎的 GC Allocated In Frame 计数器，逐字节精确）：本机的
        // GC.GetAllocatedBytesForCurrentThread() 恒为 0，用它等于这条闸根本不存在。
        // 下面两条引用恒等判据仍然保留：它们定位的是"哪一条缓存被抽掉"，比一个字节数更能说明问题。
        private static void ChecksSteadyStateZeroAlloc()
        {
            var rig = new Rig();
            try
            {
                rig.Loop.OnPacket(MatchStateHeader(), TwoPlayersBytes(Hud.PhasePlaying, 3, 0));   // 真相位 → 真屏幕流
                for (var i = 0; i < 90; i++)
                {
                    rig.Loop.Frame(1000.0 / 60.0);
                    rig.Layer.BuildOverlay(ViewW, ViewH);
                }
                SelfTest.True(rig.Layer.Flow.PlayingVisible, "零分配测量必须在对局界面上进行", "不在对局界面");
                var model = rig.Layer.Overlay;
                SelfTest.True(HasTextPrefix(model, "HP "), "测量窗口里必须真的有 HUD 数值行", "没有");
                SelfTest.Equal(1, (long)CountOf(model, OverlayItemKind.Crosshair));   // 准星也要真的在画
                SelfTest.True(!rig.Layer.DebugPanel.Visible, "零分配测量必须在面板关着的状态下进行", "面板开着");

                // 预热完的快照：绘制缓冲 + 每条绘制项的字符串引用
                var warmCount = model.Count;
                var warmBuffer = model.Items;
                var warmTexts = new string[warmCount];
                var warmKinds = new OverlayItemKind[warmCount];
                for (var i = 0; i < warmCount; i++)
                {
                    warmTexts[i] = model.Items[i].Text;
                    warmKinds[i] = model.Items[i].Kind;
                }

                var before = AllocMeter.Begin();
                for (var i = 0; i < 160; i++)
                {
                    rig.Loop.Frame(1000.0 / 60.0);
                    rig.Layer.BuildOverlay(ViewW, ViewH);
                }
                AllocMeter.AssertZero(before);

                // 判据 1：绘制缓冲必须预分配并复用（每帧 new 一组绘制项会在这里露出来）
                SelfTest.True(ReferenceEquals(warmBuffer, model.Items), "绘制缓冲必须预分配并复用", "换了一块");
                // 判据 2：绘制项的条数/种类/字符串引用都不许变（变了 = 这一帧重拼了字符串）
                SelfTest.Equal(warmCount, (long)model.Count);
                for (var i = 0; i < warmCount; i++)
                {
                    SelfTest.Equal((long)warmKinds[i], (long)model.Items[i].Kind);
                    SelfTest.True(ReferenceEquals(warmTexts[i], model.Items[i].Text),
                        "第 " + i + " 条绘制项的字符串必须沿用缓存（这一帧重拼了）", "换了新对象");
                }
            }
            finally { rig.Dispose(); }
        }

        // type=10 的载荷（phase u8 | wave u8 | intermissionMs u16 | count u8 | 玩家块，见 MatchStateCodec.Decode）。
        // 生产侧没有 match state 编码器（该包只有服务端→客户端一个方向），所以用例自带一个小写入器。
        private static byte[] MatchStateBytes(byte phase, byte wave, ushort intermissionMs, ushort[] pids, string[] names, bool[] ready)
        {
            var bytes = new List<byte>(40);
            bytes.Add(phase);
            bytes.Add(wave);
            PutU16(bytes, intermissionMs);
            bytes.Add((byte)pids.Length);
            for (var i = 0; i < pids.Length; i++) WritePlayer(bytes, pids[i], names[i], ready[i]);
            return bytes.ToArray();
        }

        // 标准两名玩家（pid 1 = 已准备，pid 2 = 未准备，都用步枪）：绝大部分用例要的就是这一份。
        private static byte[] TwoPlayersBytes(byte phase, byte wave, ushort intermissionMs)
        {
            return MatchStateBytes(phase, wave, intermissionMs, new ushort[] { 1, 2 }, new[] { "a", "b" }, new[] { true, false });
        }

        private static void WritePlayer(List<byte> bytes, ushort pid, string name, bool ready)
        {
            PutU16(bytes, pid);
            var nameBytes = System.Text.Encoding.UTF8.GetBytes(name);
            bytes.Add((byte)nameBytes.Length);
            for (var i = 0; i < nameBytes.Length; i++) bytes.Add(nameBytes[i]);
            bytes.Add((byte)(ready ? 1 : 0));
            bytes.Add(1);        // weapon = 步枪
            bytes.Add(255);      // hpRatio
            PutU16(bytes, 0);    // kills
            bytes.Add(0);        // mag
            PutU16(bytes, 0);    // reserve
            bytes.Add(0);        // reloadLeft10Ms
            bytes.Add(0);        // rage
            bytes.Add(0);        // rageLeft100Ms
            bytes.Add(0);        // downed
            bytes.Add(0);        // reviveRatio255
        }

        private static void PutU16(List<byte> bytes, ushort value)
        {
            bytes.Add((byte)(value & 0xFF));
            bytes.Add((byte)((value >> 8) & 0xFF));
        }
    }
}
