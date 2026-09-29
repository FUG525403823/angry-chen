using System;
using UnityEngine;

namespace Ac.Boot
{
    // C14 §5 的 player 机制入口（裁决第 (2) 条）：出包后的**窗口化 player** 里，帧循环归引擎所有。
    //
    // 这里只做两件事：
    //   ① BeforeSceneLoad 钩子看命令行有没有 -frameBenchOut，没有就一个字节都不改（正常启动路径不受影响）；
    //   ② 挂一个 DontDestroyOnLoad 的 MonoBehaviour，每帧调 PlanBench.Tick()（合成来件 + GameLoop.Frame(16.67ms)），
    //      渲染由引擎自己的帧循环完成 —— 全程没有一次手动 Camera.Render()。
    //
    // 装配、来件、统计、JSON 全部复用 Ac.Boot.PlanBench（与 editor 机制同一份实现，不复制逻辑）。
    public static class FrameBenchPlayer
    {
        public const string OutArgument = "-frameBenchOut";

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void Boot()
        {
            var args = Environment.GetCommandLineArgs();
            if (BenchJson.Arg(args, OutArgument, null) == null) return;
            var options = FrameBenchPlanOptions.FromArgs(args, PlanBench.MechanismPlayer);
            var go = new GameObject("FrameBenchDriver");
            UnityEngine.Object.DontDestroyOnLoad(go);
            go.AddComponent<FrameBenchDriver>().Init(options);
        }
    }
}
