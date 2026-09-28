using System;
using Ac.Core;
using Ac.Sim;
using Ac.UI;
using Ac.View;
using UnityEngine;

namespace Ac.Boot
{
    // 运行期根：仓库里 0 个 .unity 场景、0 个 MonoBehaviour（审计 H6），所以这里必须自举——
    // 不依赖任何场景内容，BeforeSceneLoad 时把帧回路挂上，任何人按 Play 或出包都能跑起来。
    public static class GameBootstrap
    {
        public const string RootName = "Ac.Boot";

        public static GameLoop Loop { get; private set; }
        // B1：呈现层（相机/场地/羊群/特效/屏幕流/调试面板）。此前它整个不存在，所以画面是空的。
        public static PresentationLayer Presentation { get; private set; }
        public static SettingsStore Settings { get; private set; }

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        public static void Start()
        {
            if (Settings == null)
            {
                Settings = new SettingsStore();
                Settings.Load(Application.persistentDataPath);
            }
            var settings = Settings.Get();
            if (Loop == null) Loop = new GameLoop(new SnapshotView(), new EntityViews(), new Hud(), new FrameProfiler());
            // 呈现层：造出 Unity 对象，再把三条呈现缝与帧回路接起来（没接上的段就不打点）
            if (Presentation == null) Presentation = PresentationLayer.Create(settings);
            Presentation.Attach(Loop);
            Presentation.ApplySettings(settings);
            Loop.Fx = Presentation.FxSink;
            Loop.Draw = Presentation.DrawSink;
            Loop.Overlay = Presentation.OverlaySink;
            // 音频：设备不可用（无头/Null Device）时不要假装有，保持该段未打点。
            if (Loop.Audio == null)
            {
                try
                {
                    Ac.Audio.Mixer.EnsureStarted();      // 静态启动：设备就绪后才有 mix
                    var mixer = new Ac.Audio.Mixer();
                    Loop.Audio = new MixerSink(mixer);
                }
                catch (Exception ex)
                {
                    Debug.LogWarning("Ac.Boot: audio unavailable, 'audio' stage stays unmarked: " + ex.Message);
                }
            }
            if (GameObject.Find(RootName) != null) return;
            var root = new GameObject(RootName);
            UnityEngine.Object.DontDestroyOnLoad(root);
            root.AddComponent<GameLoopDriver>();
        }

        // 编辑器里反复 Play 时子场景加载完成后兜底一次（域重载被关掉时静态字段会残留）。
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void EnsureStarted()
        {
            // 残留的呈现层指向的是上一局已经销毁的 Unity 对象（Root 是 Unity 的"假 null"），必须重造
            if (Presentation != null && Presentation.Root == null)
            {
                Presentation.Dispose();
                Presentation = null;
            }
            if (Loop == null || Presentation == null) Start();
        }
    }

    internal sealed class MixerSink : IFrameStageSink
    {
        private readonly Ac.Audio.Mixer _mixer;
        internal MixerSink(Ac.Audio.Mixer mixer) { _mixer = mixer; }
        public void Tick(double dtMs) { _mixer.Tick((float)dtMs); }
    }

    // 唯一的 MonoBehaviour：只做"每帧把 dt 交给帧回路"这一件事，逻辑全在 GameLoop 里（可无头测试）。
    public sealed class GameLoopDriver : MonoBehaviour
    {
        private void Update()
        {
            var loop = GameBootstrap.Loop;
            if (loop == null) return;
            // F3 切调试面板（C13 §5）：面板默认关着，不要每帧刷屏
            if (Input.GetKeyDown(KeyCode.F3) && GameBootstrap.Presentation != null) GameBootstrap.Presentation.ToggleDebugPanel();
            loop.Frame(Time.unscaledDeltaTime * 1000.0);
        }
    }
}
