using System;
using System.Text;
using UnityEngine;

namespace Ac.Boot
{
    // 唯一的新 MonoBehaviour：把 PlanBench 的生命周期摊在引擎帧循环上（装配 → 采样 → 取渲染证据 → 结算 → Quit）。
    // 文件与类同名是 Unity 的硬要求：MonoBehaviour 放在别的文件名里，AddComponent 会找不到脚本类。
    public sealed class FrameBenchDriver : MonoBehaviour
    {
        private FrameBenchPlanOptions _options;
        private PlanBench _bench;
        private int _stage;   // 0 待装配 / 1 采样中 / 2 等一帧取屏幕证据 / 3 已收工
        private int _rtBefore;
        private double _driverMemBefore;

        public void Init(FrameBenchPlanOptions options) { _options = options; }

        private void Update()
        {
            if (_stage == 3) return;
            if (_stage == 0) { Prepare(); return; }
            if (_stage == 1)
            {
                _bench.Tick();
                if (_bench.SampleComplete) _stage = 2;
                return;
            }
            // 测量窗外：引擎刚渲完最后一帧，拿窗口截图当"真的有画面"的证据（不计入任何帧时间样本）。
            CaptureScreenEvidence();
            AddRenderExtras();
            _bench.Finish();
            _stage = 3;
            Application.Quit(_bench.ExitCode);
        }

        private void Prepare()
        {
            _rtBefore = BenchJson.LiveRenderTextureCount();
            _driverMemBefore = BenchJson.DriverMemoryMb();
            _bench = new PlanBench(_options);
            // player 机制：不手动渲染，引擎帧循环负责；ManualRender=false 也让帧时间口径切到"相邻两帧间隔"。
            _bench.ManualRender = false;
            if (!_bench.Prepare())
            {
                // §9：环境不可用 = exit 2（既不是 PASS 也不是 FAIL），不打 JSON。
                var message = "FRAMEBENCH ENV mechanism=" + PlanBench.MechanismPlayer + " " + _bench.EnvError;
                Console.Out.WriteLine(message);
                Console.Out.Flush();
                Debug.LogError(message);
                _stage = 3;
                Application.Quit(2);
                return;
            }
            _stage = 1;
        }

        // 渲染证据：引擎自己渲到窗口上的那一帧截图，按与 editor 机制相同的口径数"与左上角像素差 >24"的比例。
        // 全屏 1920x1080 每 4 个像素取一个样（= 480x270 个样本，与 editor 的小 RT 读回同量级）。
        private void CaptureScreenEvidence()
        {
            try
            {
                var texture = ScreenCapture.CaptureScreenshotAsTexture();
                if (texture == null)
                {
                    _bench.PixelCoverage = -1.0;
                    _bench.PixelNote = "窗口截图不可用（CaptureScreenshotAsTexture 返回 null）：不拿它判 PASS，渲染证据由引擎渲染统计（graphics.engineDrawCallsMax/engineTrianglesMax）承担";
                    return;
                }
                var width = texture.width;
                var height = texture.height;
                var pixels = texture.GetPixels32();
                var total = 0;
                var different = 0;
                var reference = pixels[0];
                for (var y = 0; y < height; y += 4)
                {
                    var row = y * width;
                    for (var x = 0; x < width; x += 4)
                    {
                        var p = pixels[row + x];
                        var delta = Math.Abs(p.r - reference.r) + Math.Abs(p.g - reference.g) + Math.Abs(p.b - reference.b);
                        total += 1;
                        if (delta > 24) different += 1;
                    }
                }
                UnityEngine.Object.Destroy(texture);
                _bench.PixelCoverage = total == 0 ? -1.0 : (double)different / total;
                _bench.PixelNote = "引擎帧循环渲出的窗口截图（ScreenCapture.CaptureScreenshotAsTexture，采样窗口外取一帧）" + width + "x" + height
                    + "，每 4 像素取样，与左上角像素差 >24 记一比：" + different + "/" + total;
            }
            catch (Exception error)
            {
                _bench.PixelCoverage = -1.0;
                _bench.PixelNote = "窗口截图失败（" + error.GetType().Name + "：" + error.Message + "）：不拿它判 PASS，渲染证据由引擎渲染统计承担";
            }
        }

        // player 机制的 render 块：与 editor 的 render 块同名字段对得上（vSync / targetFrameRate / 显存 / RT 数），
        // editor 那几条"手动渲染地板"本机制没有对手，因此不存在。
        private void AddRenderExtras()
        {
            var rtAfter = BenchJson.LiveRenderTextureCount();
            var driverMemAfter = BenchJson.DriverMemoryMb();
            _bench.ExtraJson.Add(delegate(StringBuilder sb)
            {
                sb.Append("  \"render\": {\n");
                BenchJson.Num(sb, 4, "vSyncBefore", _bench.VSyncBefore);
                BenchJson.Num(sb, 4, "vSyncDuring", 0);
                BenchJson.Num(sb, 4, "targetFrameRate", -1);
                BenchJson.Num(sb, 4, "rtCountBefore", _rtBefore);
                BenchJson.Num(sb, 4, "rtCountAfter", rtAfter);
                BenchJson.Num(sb, 4, "driverMemBeforeMb", _driverMemBefore);
                BenchJson.Num(sb, 4, "driverMemAfterMb", driverMemAfter);
                BenchJson.Num(sb, 4, "driverMemDeltaMb", driverMemAfter - _driverMemBefore);
                BenchJson.Meta(sb, 4, "manualRenderCalls", "0（引擎帧循环渲染；全程没有一次 Camera.Render()）");
                BenchJson.TrimLastComma(sb);
                sb.Append("  },\n");
            });
        }
    }
}
