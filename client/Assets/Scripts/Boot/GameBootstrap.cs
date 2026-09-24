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

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        public static void Start()
        {
            if (Loop != null) return;
            var views = new EntityViews();
            var profiler = new FrameProfiler();
            Loop = new GameLoop(new SnapshotView(), views, new Hud(), profiler);
            var root = new GameObject(RootName);
            Object.DontDestroyOnLoad(root);
            root.AddComponent<GameLoopDriver>();
        }

        // 编辑器里反复 Play 时子场景加载完成后兜底一次（域重载被关掉时静态字段会残留）。
        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void EnsureStarted()
        {
            if (Loop == null) Start();
        }
    }

    // 唯一的 MonoBehaviour：只做"每帧把 dt 交给帧回路"这一件事，逻辑全在 GameLoop 里（可无头测试）。
    public sealed class GameLoopDriver : MonoBehaviour
    {
        private void Update()
        {
            var loop = GameBootstrap.Loop;
            if (loop == null) return;
            loop.Frame(Time.unscaledDeltaTime * 1000.0);
        }
    }
}
