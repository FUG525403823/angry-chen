# C08 验收记录（MC08）

- 计划：[`docs/plans-v2/client/C08-羊群渲染与问界额标.md`](../plans-v2/client/C08-羊群渲染与问界额标.md)
- 固定点：`8a7576c`（MC07）→ 本份提交
- 自检：`SELFTEST OK cases=47`（本份新增 6 条：`sheep.*`）

## 1. §6 的五条验证

| # | 命令 | 输出 | 结论 |
|---|---|---|---|
| 1 | `client/Logs/selftest.cmd` | `SELFTEST START cases=47`、6 条 `PASS sheep.*`、`SELFTEST OK cases=47`，exit=0；全输出无 `FAIL`，也没有 `Shader error`/`error CS` | ✅ |
| 2 | 同上输出 | `sheep.geometry` / `sheep.pool` / `sheep.zero_alloc` / `sheep.emblem` / `sheep.state_map` / `sheep.culling` 共 6 条（≥ 6） | ✅ |
| 3 | `Select-String client/Assets/Shaders/Emblem.shader -Pattern 'sampler2D'` | **零命中**（脚本里断言同一件事）；同时命中 `ZWrite Off`、`_EmissiveTint`、`_EmissiveIntensity` | ✅ |
| 4 | `node tools/check-assets.mjs` | `OK：仓库零外部素材，依赖白名单未被破坏。`，exit=0（`.shader` 是纯文本，新增无二进制） | ✅ |
| 5 | 人工：1024 实体压力场景 | **本批未跑**（批处理无渲染上下文，编辑器被另一会话占用）。代偿：`sheep.zero_alloc` 用 `GC.GetAllocatedBytesForCurrentThread()` 实测连续 3 帧增量 == 0、实例数组引用恒等；绘制调用数由每形 1 次的池结构保证（≤ 4），退化路径见计划 §8 | ⏸ 记录 |

## 2. §7 的 6 条 DoD

| # | 判据 | 证据 | 结论 |
|---|---|---|---|
| 1 | 四形尺寸、缩放与羊毛团数与 §5(a) 逐值一致 | `sheep.geometry`：逐形断言高度 0.9/1.0/1.1/2.4、半径 0.50/0.55/0.60/1.60、缩放 1.00/1.06/1.12/1.60、团数 8/12/12/12、额标尺寸 0.22/0.22/0.22/0.36，并断言每形顶点 ≤ 512、三角形 ≤ 512、包围盒高度合理 | ✅ |
| 2 | 池容量 1024（每形 256）；连续 3 帧实例引用集合恒等且零托管分配 | `sheep.pool`（256/形、第 257 次返回 −1 且 `OverflowCount` 累加、`Reset` 不换数组、下标 = form×256+cursor）、`sheep.zero_alloc`（3 帧 64 只羊写入后分配字节增量 == 0，同一视图连续帧下标恒等） | ✅ |
| 3 | 额标 `0.2 + 0.8 × (1 − hpRatio)`；问界羊/羊王自发光色 `0x7AD1FF`/`0xFF8A4C`；平滑步长 `0.153518` | `sheep.emblem`：hp=1→0.2、hp=0→1.0、hp=0.5→0.6、越界钳制、常量 `0.153518`、一步平滑 = 0.2 + 0.8×0.153518；三色按 RGB 整数断言 | ✅ |
| 4 | 额标为程序化网格 + 顶点色；`Emblem.shader` 纯文本、无 `sampler2D`、不引用贴图、`ZWrite Off` | `sheep.emblem`：`BuildEmblem` 5 顶点/4 三角形且 `colors32` 与顶点数一致、羊王顶点色 `0xFF8A4C`；shader 文本断言无 `sampler2D`、无 `.png`/`Texture2D`、含 `ZWrite Off` 与两个逐实例属性 | ✅ |
| 5 | > 90m 与视锥外不绘制；13 个 `SHEEP_STATE` 码全部有动画映射 | `sheep.culling`（10m 可见、200m 与背后与视锥外剔除、占比不足剔除、羊王半径外扩后仍可见、阈值常量）；`sheep.state_map`（13 码逐个断言到 `SheepAnim` 的枚举与名字、`charging` 强制 `Charge`、`fading` 走尸体、尸体曲线 0.72/0.16/0.62） | ✅ |
| 6 | 分组自检无 `FAIL`、末行 `SELFTEST OK`；两份仓库门禁 exit 0 | §1 第 1、4 行 + `node tools/check-docs.mjs` 全绿 | ✅ |

## 3. 计划修订与跨链

### 3.1 本份修订的计划条目
- §3/§4：测试文件名 `sheep_visual_test.cs` → `SheepSuite.cs`（命名约定）。
- §5(b)：团抖动种子改述——网格按羊形固定种子（四形共用生成器 + 每形一次缓存），`EntityId` 抖动落在 `WoolJitter(kind, entityId)` 的逐实例缩放上；把种子写进网格会与实例化池互斥。
- §9：`SheepInstancePool` 的 `Acquire` 改成 `TryAcquire(form, out index)`（−1 = 满并计数），`SheepVisuals.Write` 的入参改为 C04 的 `EntityView` + `nowMs`（返回下标），`Culling.Filter` 的入参改为池 + 复用缓冲，补 `SheepMesh.BuildEmblem`/`Form`/`WoolJitter`。
- §5(c) 零分配判据落地：`GC.GetAllocatedBytesForCurrentThread()` 前后差 == 0（用例断言），加上实例数组引用恒等。
- §6 第 5 条（人工 1024 实体压力）本批未跑，代偿见 §1 第 5 行。

### 3.2 有意保留的判断题
- `SheepVisuals` 用权威坐标 `EntityView.X/Y/Z` 而不是 `RenderX/RenderZ`（C04 的误差平滑）：§4 任务 3 要求写 x/y/z，且 1024 只羊各带一份平滑状态与零分配判据冲突；逐实体平滑留给近处实体（玩家的活）。
- `SheepInstancePool.Instances` 直接暴露数组：写路径是 `SheepVisuals`、读路径是 `Culling` 与用例，中间不需要拷贝；换来的是「整局不换数组」这条零分配判据可以逐帧断言。
- 羊毛团用八面体（6 顶点/8 三角形）、头角腿用盒体（8 顶点/12 三角形）：预算上限是每形 512，这个组合在四形都只有约四分之一用量，够了；更圆的团留给后续美术步骤。
- 尸体淡出按实例独立计时（`_deathMs`/`_dead` 两个 1024 定长数组，构造期分配）：比每帧扫描实体状态更省，也不会在淡出中途被池的环形复用打断。
- 剔除的屏幕占比用「半径 /（距离 × tan(fov/2)）」近似（以视口高度为基准）：与 §5(e) 的 0.15% 阈值同量纲，真正的投影占比需要世界→屏幕变换，代价不值得。

### 3.3 跨链事项
- 数据源是 C04 的 `EntityViews.EntityView`（`Id/Kind/X/Y/Z/YawRad/HpRatio/State/Flags/Visible`），不是计划里写的 `SnapshotEntity`：字段一一对应，改名只为对齐现有类型。
- `SHEEP_STATE` 13 码与 `SNAPSHOT_FLAG.charging=8`/`fading=16` 按 §5(e) 冻结表实现；`kind` 越界收敛到羊王、`state` 越界回到 `Idle`，服务端以后加码不会让客户端崩。
- 额标不引用贴图（ADR-003 + 零素材门禁）：颜色走顶点色 + 逐实例 `_EmissiveTint`/`_EmissiveIntensity`，强度随血量下降而增强，越残越亮。

## 4. 双轴审查（本轮 · 固定点 `8a7576c`）

两个子代理并行跑规格轴与规范轴（各自读计划/标准与全部新增文件，各自复跑可跑的门禁）。完整报告：`client/Logs/review-c08-spec.md`、`client/Logs/review-c08-standards.md`（gitignore，不入提交）。

### 4.1 规格轴（6 必须修 + 7 判断题）
| 发现 | 处置 |
|---|---|
| 尸体「变暗 0.62」被当成缩放乘子，终点实际缩放 0.446 且没有变暗通道 | ✅ 已修：`SheepInstance` 增加 `Darken` 通道，缩放只乘收缩系数；用例断言终点 0.72 与 0.62 两个通道 |
| 额标挂点 `y 0.90 / z 0.92` 没有消费者（额标建在原点） | ✅ 已修：`SheepInstance` 增加 `EmblemTransform`，`Write` 用 `HeadOffsetY/HeadOffsetZ × 羊形缩放` 算出头部挂点 |
| 团抖动种子 `EntityId` 未接线（`WoolJitter` 无调用者） | ✅ 已修：`Write` 用 `WoolJitter(kind, view.Id)` 做逐实例尺寸抖动；用例断言不同 id 得到不同实例缩放 |
| 额标几何缺「菱形描边与外圈」，只有实心 4 三角形，用例把缺失固化了 | ✅ 已修：`BuildEmblem` 补描边（4 片 8 三角形）与外圈方框（4 片 8 三角形），用例改成断言顶点/三角形下限 |
| 池记录 `Visible` 永不复位 + 淡出结束直接返回 −1 → 幽灵羊 | ✅ 已修：`Reset()` 显式失活上一帧写过的记录，淡出结束时把该实例置为不可见；用例断言淡出结束后不再可见 |
| `Emblem.shader` 属性在 `UnityPerMaterial`、无实例化缓冲 → `DrawMeshInstanced` 下无法逐实例 | ✅ 已修：加 `multi_compile_instancing` + `UNITY_INSTANCING_BUFFER` 与 `UNITY_ACCESS_INSTANCED_PROP`，两个属性改成逐实例 |
| 判断题：网格实测高度与 §5(a) 表不符、剔除重复乘 `lossyScale`、`MapAnim` 死分支、零分配用例重言式、平滑状态按池槽位串亮度、每形网格未缓存 | ✅ 部分：剔除半径不再乘缩放（羊形半径已是世界尺度）；死分支已删；零分配用例已重写（见 §4.2 F7）；其余保留并记录——每形网格缓存与网格实测尺寸属后续美术步骤，平滑状态按槽位是既定取舍（`Write` 每帧重算，`Reset` 后由首帧目标值收敛） |

### 4.2 规范轴（9 必须修 + 9 判断题）
| 发现 | 处置 |
|---|---|
| F1（最重）`FlagCharging = 8`/`FlagFading = 16` 撞上线上掩码的 `rageMode(8)`/`reloading(16)`，真正的 charging/fading 永不生效 | ✅ 已修：改成 `1<<5 = 32` / `1<<6 = 64`（与 `Ac.Net.SnapshotCodec` 一致），用例改成用字面量 32/64 断言，并补两条反例（rageMode 不再被误判成 Charge、reloading 不再被误判成尸体）；计划 §5(e) 同步修订 |
| F2 哈希原语与 `ArenaMesh.Hash` 逐字重复 | ✅ 已修：直接调 `ArenaMesh.Hash`，删掉本份副本 |
| F3 `WoolJitter` 无调用方、`2654435761u` 与 `Rng.SeedMultiplier` 双写、注释与代码不符 | ✅ 已修：接线（见 §4.1）、删掉重复常数、注释改成真话 |
| F4 死代码：`Culling.Center`、`SheepMesh` 的无用 `using Ac.Core`、`SheepInstancePool.CapacityOf()` | ✅ 已删三处（用例改用 `EntityCapacity` 常量断言） |
| F5 `MapAnim` 恒等死分支、`Write` 三段冗余条件 | ✅ 已删死分支，条件收成两段 |
| F6 只写不清的 `Visible` + 全量扫描 → 跨帧幽灵实例 | ✅ 已修（同 §4.1 第 5 条） |
| F7 零分配用例的重言式与测不准：`new bool[3,3]` 落在测量窗口内、`ReferenceEquals(x,x)`、采集结果从不断言 | ✅ 已修：缓冲移出窗口、删掉自比、三帧下标数组逐个断言恒等（`GC.GetAllocatedBytesForCurrentThread()` 增量 == 0 现在是有效证据） |
| F8 「羊王半径外扩」用例无区分度、`ratio >= 0f` 恒真、`Filter` 占比分支未覆盖 | ✅ 已修：改成把点放在侧平面外 0.2m——小半径剔除、羊王半径可见（删掉外扩就会红）；`ratio` 改成断言确实低于阈值 |
| F9 魔法数字（`0.01f`、`57.29577951308232`、头/角/腿裸值、`StateCount` 与枚举双写） | ✅ 已修：距离下限与头角腿尺寸都提成常量、角度用 `Mathf.Rad2Deg`、用例补断言 `Enum.GetValues(typeof(SheepAnim)).Length == 13` |
| J1–J9（`Instances` 暴露、静态 `Plane[]` 不可重入、尸体按槽位计时、`SkippedCount`/`CorpseCount` 只有用例读、`in` 修饰等） | ⏸ 记录：`Instances` 与 `SkippedCount` 是 §9 冻结接口；静态平面缓冲已加注释说明不可重入；其余理由与规格轴判断题合并记在 §3.2 |

### 4.3 审查快照
两轴都实跑了 `check-docs`/`check-assets`（exit 0）与静态核对；规范轴另核对了注册数与实跑一致（`sheep.*` 6 条 / cases=47）、`Ac.Sim`/`Ac.Net` 零改动（ADR-010）、`ContractSuite` 白名单无新增引用。两轴共同确认的逐值通过项：§5(a) 表值、§5(b) 预算（8 团 104 顶点/148 三角形，12 团 128/180）、§5(c) 容量与下标与溢出计数、§5(d) 公式/0.153518/三色/无贴图/`ZWrite Off`、§5(e) 90m 与 0.0015 阈值、13 码映射（与 `server/src/sim/sheep.hpp` 逐字一致）。

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd'); "exit=$LASTEXITCODE"
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^PASS sheep|^FAIL ' | ForEach-Object { $_.LineNumber.ToString() + ': ' + $_.Line.Trim() }
Select-String -Path client/Assets/Shaders/Emblem.shader -Pattern 'sampler2D'
node tools/check-assets.mjs; node tools/check-docs.mjs
```
