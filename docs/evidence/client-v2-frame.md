# C14 帧基准报告（client-v2-frame.md）

> 上游规格：docs/plans-v2/client/C14-性能剖析与优化.md §5 测量方法与报告字段
> 门禁脚本：client/tools/frame-bench.ps1（**不许加 -nographics**；缺 JSON / 缺字段 / 缺段一律 FAIL，绝不默认绿）

## 0. 当前结论

**verdict: 未执行（需真图形设备）** —— 本报告是**待填模板**，不是通过证明。

原因（如实记录，不用"顺延"两个字盖过去）：

1. 门禁脚本依赖 `Ac.Tests.FrameBench.Run` 这个基准入口点与 `bench-4p60sheep` 场景，**二者尚未装配**：当前客户端没有可运行的游戏循环。
2. §5 明确要求不加 -nographics —— 必须在有真实图形设备的会话里跑，软件光栅化的帧时间不具代表性。
3. 三次取中位、机器/驱动/提交号必须来自真实运行，**不能由实现方编造**。

## 1. 报告字段（§5 必备，缺一即判 FAIL）

| 字段 | 值 |
|---|---|
| machine | （待填） |
| cpu | （待填） |
| gpu | （待填） |
| driver | （待填） |
| unityVersion | （待填，取自 client/ProjectSettings/ProjectVersion.txt） |
| resolution | 1920×1080 |
| qualityTier | （待填，0/1/2） |
| scene | bench-4p60sheep（4 人 + 60 羊 grunt:ram:elite:king = 9:3:2:1，满特效） |
| warmupFrames | 120 |
| sampleFrames | 600 |
| runs | 3（取中位） |
| frameP95Ms | （待填，预算 ≤ 20.0） |
| frameP99Ms | （待填，预算 ≤ 33.0） |
| managedAllocBytesPerFrame | （待填，预算 = 0） |
| gc0Delta | （待填，预算 = 0） |
| drawCalls | （待填，预算 ≤ 120） |
| triangles | （待填，预算 ≤ 180000） |
| particles | （待填，预算 ≤ 256） |
| materials | （待填，预算 ≤ 24） |
| stageP95（8 段） | input ≤0.5 / sync ≤1.5 / predict ≤1.0 / fx ≤4.0 / audio ≤1.0 / draw ≤10.0 / overlay ≤1.0 / hud ≤1.0（待填） |
| commit | （待填，脚本自动取 git rev-parse --short HEAD） |
| verdict | 未执行 |

## 2. 怎么把它变成真结论

    # 1) 装配基准入口点 Ac.Tests.FrameBench.Run（写 client/Logs/frame-bench/run-*.json，字段见 §1），
    #    并在 bench-4p60sheep 场景里接上 FrameProfiler（8 段）+ Batching（实例化/合批）+ 满特效 + 24 音源
    # 2) 在有真实图形设备的会话里跑（不要加 -nographics）
    pwsh -File client/tools/frame-bench.ps1 -Runs 3 -WarmupFrames 120 -SampleFrames 600
    # 3) 退出码 0 = PASS（FRAME-BENCH PASS），非 0 = FAIL 并逐条列出超阈项

## 3. 已落地、当前就能证的部分

| 项 | 证据 |
|---|---|
| 8 段计时 + 240 帧环 + ceil(q*n)-1 分位口径 | perf.* 7 条用例（分位下标 227/237 直接断言、环回绕、同段多次 Mark 累加、越界返回 0） |
| 热路径零分配 | perf.zero_alloc_steady：2000 帧 Begin/Mark×8/End 与 200 次分位查询，GC.GetAllocatedBytesForCurrentThread() 差分为 **0** |
| 超阈判定有判别性 | perf.budget_flags：单帧自旋 22ms，同时越过 draw 段预算（10ms）与帧 P95 预算（20ms），断言 StageOverBudget 与 FrameOverBudget 双双为真 |
| 实例化/合批/剔除阈值与画质档映射 | batch.* 5 条用例（24→单绘 / 25→实例化、1024 实例→2 批、低档上限 256→4 批、剔除边界 60/80 含等值点、NaN 与负距离判不可见） |
| 池化零分配与句柄语义 | batch.pool_zero_alloc（500 轮借还分配为 0）、batch.pool_reuse（任意顺序归还、同一句柄不能回收两次、容量倍增不丢数据） |
| 门禁不会恒绿 | frame-bench.ps1 对缺 JSON、解析失败、缺字段、缺 8 段中任一段都直接 exit 1 |

## 4. 已知缺口

- **冻结值互斥（需人类裁决）**：剔除距离 C08 §5(e) = 90m vs C14 §5 = 羊 60m / 场地 80m；阴影距离 C08 = 60m vs C14 §5 = 20/35/50（按画质档）。代码保持 C08 的值不动（`View/Culling.cs` 90m、`View/LightingRig.cs` 60m），`View/Batching.cs` 的 C14 表并存但无调用者。**实现方不得自签规格**（C11/C13 审查两次判定），等裁决后一次改齐并补一致性用例。

- 基准入口点与 bench-4p60sheep 场景未装配 ⇒ 本报告无法给出数值（这是**唯一的**未兑现项）。
- 逐段收敛热点（§4 任务 5：先 draw 再 fx，每次记录改动前后 P95 差值）依赖上一条，同样未开始。
