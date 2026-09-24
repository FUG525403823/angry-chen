# C14 §7 帧预算报告

## 5 类报告字段（§5）

| 字段 | 实测 | 预算 | 判定 |
|---|---|---|---|
| frameP95Ms | 0.0069 | ≤ 20 | PASS（仅 CPU 帧回路） |
| frameP99Ms | 0.0088 | ≤ 33 | PASS（仅 CPU 帧回路） |
| managedAllocBytesPerFrame | 0 | 0 | PASS |
| gc0Delta | 0 | 0 | PASS |
| drawCalls | 未测得 | ≤ 120 | **无法判定** |
| triangles | 未测得 | ≤ 180000 | **无法判定** |
| particles | 未测得 | ≤ 256 | **无法判定** |
| materials | 未测得 | ≤ 24 | **无法判定** |
| stageP95.input/sync/predict/hud | 0.0001 / 0.0033 / 0.0001 / 0.0001 ms | 0.5/1.5/1.0/1.0 | PASS |
| stageP95.fx/audio/draw/overlay | 未标记 | 4.0/1.0/10.0/1.0 | **无法判定** |

## 复现

```
pwsh -File client/tools/frame-bench.ps1 -Runs 3 -Frames 600        # §6 的冻结命令（-Frames，不是 -SampleFrames）
Tuanjie.exe -batchmode -nographics -projectPath client -executeMethod Ac.Tests.FrameBench.Run \
  -frameBenchScene bench-4p60sheep -frameBenchOut <abs>.json -frameBenchWarmup 120 -frameBenchSample 300 -frameBenchQuality 1
```

## 结论（含不能声称的部分）

完整细节与冲突记录见 `docs/evidence/client-v2-frame-acceptance.md`。两句话版本：

1. **CPU 帧回路（输入→预测/和解→镜像→视图→HUD）预算达标且余量极大**，稳态逐帧 0 B 分配由常驻用例 `boot.steady_state_zero_alloc` 守着。
2. **§6/DoD 里"1920×1080 下 P95 ≤ 20ms"没有被验收**：本机没有图形设备（`Null Device`、`Screen` 640×480），仓库里也还没有任何渲染装配（0 场景、`Batching/InstancePool` 零生产调用者）。走 §5 测量法 1（禁 `-nographics`）时编辑器起不来 ⇒ 门禁按 §9 **退出码 2 = 环境不可用**；走 `-nographics` 只能得到上表的 CPU 数字，且 `stageP95.fx/audio/draw/overlay` 缺段会被门禁判 FAIL。**两种走法都不允许出现 PASS**，这是刻意设计。
