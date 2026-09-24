# C09 验收记录（MC09）

- 计划：[`docs/plans-v2/client/C09-武器视图模型与射击特效.md`](../plans-v2/client/C09-武器视图模型与射击特效.md)
- 固定点：`58a5fa8`（MC08 之后）→ 本份提交
- 自检：`SELFTEST OK cases=55`（本份新增 8 条：`effects.*`）

## 1. §6 的五条验证

| # | 命令 | 输出 | 结论 |
|---|---|---|---|
| 1 | `client/Logs/selftest.cmd` | `SELFTEST START cases=55`、8 条 `PASS effects.*`、`SELFTEST OK cases=55`，exit=0；全输出无 `FAIL`、无 `error CS` | ✅ |
| 2 | 同上输出 | `effects.viewmodel_params` / `effects.anim_table` / `effects.tracer_pool` / `effects.particle_pool` / `effects.hit_marker` / `effects.screen_feedback` / `effects.ammo_gate` / `effects.zero_alloc` 共 8 条（≥ 6） | ✅ |
| 3 | `Select-String client/Assets/Scripts/View/Tracer.cs -Pattern 'MuzzleWorld'` | 命中两处（文件头声明与 `IsCollinearWithEye` 的调用约定）；起点由调用方从 `ViewModel.MuzzleWorld` 取，`IsCollinearWithEye`/`OriginOk` 把「距眼位 < 0.5m 即缺陷」写成可断言函数 | ✅ |
| 4 | `node tools/check-docs.mjs` / `node tools/check-assets.mjs` | 两份都 exit=0（30 份计划线性链通过；零外部素材，依赖白名单未破坏） | ✅ |
| 5 | 人工：按住左键 1 秒（手枪）→ 曳光 ≤ 6 条；空弹匣按一次 → 0 条 | **本批未跑**（批处理无输入上下文）。代偿：`effects.tracer_pool` 按 `IntervalMs(300,1)=200ms` 现算 1 秒内的发数并断言 ≤ 6；`effects.ammo_gate` 断言闸门为 0 / 负数时 `SpawnTracer` 返回 false、`RejectedShots` 累加、曳光与枪口火焰都不产生 | ⏸ 记录 |

## 2. §7 的 6 条 DoD

| # | 判据 | 证据 | 结论 |
|---|---|---|---|
| 1 | 视图模型 `62 / 0.01 / 12 / (0.17, −0.19, −0.02)` 与枪口挂点 `(0.0, 0.02, 0.58)` 是常量并被断言 | `effects.viewmodel_params`：四个常量逐个断言；把 `ViewModel` 挂到带旋转的相机上，断言 `MuzzleWorld` 与 `EyeWorld` 距离 ≥ 0.5m（实测 0.5803），并断言挂点随眼位刷新 | ✅ |
| 2 | 换弹 1400/2000/2600ms、切枪 260ms、枪口火焰 45ms、后坐 `0.35°+0.2°` 与 §5(b) 一致 | `effects.anim_table`：常量逐值断言 + 行为断言（`OnReload` 倒计时 2000→1500→0 并驱动 0.42/0.30 姿态、`OnSwapDown` 260、一发 +0.35° 与 ±0.2° 左右交替、7.5/s 衰减、散布 +0.1/上限 0.25/350ms 后 6.0°/s、呼吸 0.42Hz、每米相位 0.55、倒地 0.46、开火间隔按 `60000/(rpm×mult)` 现算） | ✅ |
| 3 | 曳光起点 = 枪口且距眼位 ≥ 0.5m；命中时终点 = `playerHit` 的 `hitX/hitY/hitZ`；未命中 30m | `effects.tracer_pool`（起点 = 入参枪口、`OriginOk` 通过、终点 = 命中点、`FallbackEnd` 距离 30m、零方向回退前方）+ `effects.ammo_gate`（`SpawnTracerFromMuzzle` 命中/未命中两条路径的终点） | ✅ |
| 4 | 池容量（曳光 32 / 粒子 256 / 问号弹 24 / 警戒环 12）与复用规则被断言，连续帧零分配 | `effects.tracer_pool`（32 条满后覆盖最旧并计数）、`effects.particle_pool`（256 容量、溢出 3、点尺寸 64px、弹孔 8s、血屑 6 粒、毛屑 4 粒、减少动态 ×0.35）、`effects.screen_feedback`（问号弹 24 与警戒环 12 的溢出计数、警戒环 1150ms、问号弹按方向推进）、`effects.zero_alloc`（3 帧开火+命中+入池+Tick 后分配字节增量 == 0，曳光/粒子数组引用恒等） | ✅ |
| 5 | 三态颜色（`0xFFFFFF / 0xFF4D4D / 0xFFD24D`）与 140ms 时长一致 | `effects.hit_marker`：三色逐值、`LifetimeMs = 140`、爆头缩放 1.35 与非爆头 1.0、伤害倍率 2.0、`ShowFromFlags` 四种位组合（0→命中、killed→击杀、headshot→爆头、两者同时→爆头优先）、139ms 仍在 / 140ms 结束 | ✅ |
| 6 | 分组自检无 `FAIL`、末行 `SELFTEST OK`；两份门禁 exit 0 | §1 第 1、4 行 | ✅ |

## 3. 计划修订与跨链

### 3.1 本份修订的计划条目
- §3/§4/§8：测试文件名 `effects_pool_test.cs` → `EffectsSuite.cs`（命名约定）。
- §5(e)：`HIT_FLAG` 的位值以 S08 事件表为权威，客户端本层先用 `FlagKilled = 1` / `FlagHeadshot = 2` 占位，待 S08 交付后只改这两个常量（已在计划正文加注）。
- §9：补实际签名——`ViewModel.Attach(Camera)` / `MuzzleEyeDistanceM`；`WeaponAnim.OnSwapDown(int)` / `AddWalkDistance(float)` / `TickWalk(float)` / `IntervalMs(rpm, mult)` / `ReloadDurationMs(slot)`；`Tracer.FallbackEnd` / `IsCollinearWithEye` / `OriginOk` / `Segments` / `LiveCount`；`Effects.SetAmmoGate` / `SetHitAnchor` / `SpawnChargeRing` / `SpawnEliteBolt`（`SpawnTracer` 返回 `bool`：闸门拒绝即 false）；`HitMarker.ShowFromFlags` / `ColorOf` / `ScaleOf`；`Particles.Reset` / `Tick` / `ReducedMotion`。
- §5(c)：粒子抖动速度**归一化**成方向 × 固定速度，这样「减少动态 ×0.35」是可断言的等式（计划只写了倍数）。
- §5(c)：问号弹与警戒环只有自转/寿命/覆盖规则，没有独立寿命上限（警戒环有 1150ms；问号弹按计划仅自转 + 发光缩放，被覆盖时才退场）。
- §6 第 5 条（人工按住左键 + 空弹匣）本批未跑，代偿见 §1 第 5 行。

### 3.2 有意保留的判断题
- `Effects.SpawnTracer` 返回 `bool` 而不是 `void`：闸门拒绝要能被调用方与用例看见，`RejectedShots` 单独计数；池层不自己读弹药账本，只看闸门（§8 明确闸门取 C06 的 `GateMag`）。
- 曳光的「最旧覆盖」用环形游标实现（不是时间戳排序）：32 条一帧内写满时游标天然指向最旧一条，省一次扫描。
- 开火偏航抖动用左右交替的固定符号而不是随机数：回放与用例可复现，视觉上同样是抖动。
- `HitMarker.LifetimeMs` 三态共用 140ms，只有缩放不同——§5(e) 表里三态存活时间一致，不需要每态一个字段。
- 命中粒子先落在 `SetHitAnchor` 给的位置（事件给出的权威命中点），而不是本地预测点：与 §5(d)「命中点只信事件」同一条纪律。

### 3.3 跨链事项
- 本步只消费事件与闸门，不产生判定：曳光终点、三态反馈、血屑/毛屑都需要 S08 的 `playerHit` / `sheepKilled` 事件与 `HIT_FLAG` 位；事件通道缺失时要查事件层，不是渲染层（§8 第 4 条）。
- 视图模型走独立层参数（FOV 62 / 近 0.01 / 远 12），与 C05 冻结的主相机（远 300m）分离；实际分层渲染在场景装配步骤落地，本层先把参数与挂点冻住。
- 弹药闸门由 C06 的 `AmmoLedger`（`GateMag = min(optimistic, mag)`）供给：`Effects.SetAmmoGate` 是唯一入口，空弹匣不出曳光。

## 4. 双轴审查（本轮 · 固定点 `58a5fa8`）

两个子代理并行跑规格轴与规范轴（各自读计划/标准与全部新增文件，各自复跑可跑的门禁）。完整报告：`client/Logs/review-c09-spec.md`、`client/Logs/review-c09-standards.md`（gitignore，不入提交）。

### 4.1 规格轴（4 必须修 + 8 判断题）
| 发现 | 处置 |
|---|---|
| `HIT_FLAG` 位值与 S08 权威表相反（客户端写 killed=1/headshot=2，实际 headshot=1/downed=2/killed=4）→ 爆头判成击杀、击杀判成命中、倒地判成爆头 | ✅ 已修：改成 `FlagHeadshot = 1` / `FlagDowned = 2` / `FlagKilled = 4`（对齐 `server/src/config/combat.hpp:18-20`），用例改成用字面量断言并补 `downed` 反例；计划 §5(e) 的注也从「待 S08 交付」改成权威值（S08 早已交付，本就是 C09 的入口条件） |
| `EyeWorld` 取的是视图模型基准位而不是眼位（枪口到它 0.658m，超出 §5(d) 的 0.5–0.6m；验收里记的 0.5803 其实是到相机的距离） | ✅ 已修：`EyeWorld` = 相机原点，新增 `BaseWorld` = 眼位 + 旋转 × 基准位；枪口↔眼位实测 0.5803m，落在 0.5–0.6m 带内并被断言 |
| 呼吸相位 `0.42Hz` 未乘 `2π`（实为 0.42 rad/s，周期约 15s），同文件行走相位却正确 | ✅ 已修：`BreathPhase += BreathHz × 2π × dt`；用例改成断言「每秒 0.42 圈」 |
| §5(d) 的「连射 1 秒 ≤ 6 条」没有产品代码承载（用例只是把公式又算一遍） | ✅ 已修：`Effects` 增加射速闸门（`SetFireIntervalMs` + 帧钟 `_sinceLastShotMs` + `ThrottledShots`），默认间隔取 `IntervalMs(PistolRpm, FireRateMultiplier)`；用例改成 1 秒内 10 次尝试、断言放行 ≤ 6 且放行 + 节流 = 10（去掉节流就会红） |
| 判断题：`Reset()` 清 `Alive` 与「只复位游标」冲突、血屏无夹取且按帧衰减、爆头出血屑与三态都拉红闪、问号弹自转与警戒环阶梯只有常量、枪口烟无处发射、§5(a) 渲染状态未实现、冻结常量与 C05 重复、`i16` 厘米未换算 | ✅ 部分：冻结常量改成从 `ViewModelAnchor` 派生（单源）；血屏补 `SetBloodScreen` 夹取 + 按时间衰减；`MuzzleSmoke` 补独立寿命；其余记录——自转/发光缩放/警戒环阶梯是渲染参数，等场景装配消费；`i16` 厘米→米的换算由事件层负责（本层收到的已是世界坐标）；渲染状态与「挂相机子节点」在场景装配步骤落地（与 C07/C08 同一条口径） |

### 4.2 规范轴（12 必须修 + 9 判断题）
| 发现 | 处置 |
|---|---|
| M1 `HIT_FLAG` 位值相反（同规格轴第 1 条，两轴独立发现） | ✅ 已修 |
| M2 `SpawnHit` 三段分支里爆头分支与 else 逐字重复 | ✅ 已收成两段（击杀出毛屑 + 抖动；其余出血屑） |
| M3 `BloodScreen` 永无置位路径（死功能）+ 0.98f/0.01f 魔法数 | ✅ 已修：`SetBloodScreen` 带 0.55 夹取、按 `0.35/s` 时间衰减；用例断言夹取与清零 |
| M4 问号弹无寿命、`TickBolts` 从不置 `Alive = false`（池只写不清），用例还把缺陷固化成期望值 | ✅ 已修：新增 `EliteBoltLifetimeMs = 3000`（计划未给，实现补）并到期退场；用例改成断言「到期后不再存活」，溢出期望随到期重算 |
| M5 `OriginOk` 是 `IsCollinearWithEye` 的纯取反重复 API | ✅ 已删，改成正规入口 `Tracer.SpawnFromMuzzle(in ViewModel, in Vector3)`——它真的用 `ViewModel.MuzzleWorld`，也让 §6 验证 3 的 grep 命中实际代码而不只是注释 |
| M6 `TickWalk` 零调用方且与 `AddWalkDistance` 双入口同写 `WalkPhase` | ✅ 已删 `TickWalk`，只留按米推进的 `AddWalkDistance`（相位 = 距离 × 0.55，Hz 只是观测量） |
| M7 `SetReducedMotion` 零调用方（用例直接写字段） | ✅ 已修：用例改走 `Effects.SetReducedMotion` |
| M8 冻结常量与 C05 `ViewModelAnchor` 重复（含 0.5m 双写） | ✅ 已修：`ViewModel` 的 62/0.01/12/基准位改成从 `ViewModelAnchor` 派生，`Tracer` 的最小枪口距删掉、统一用 `ViewModel.MinMuzzleEyeDistanceM` |
| M9 魔法数 `2.399963f` / `0.7f` / `1.4f` / `0.98f` / `0.01f` | ✅ 已修：抖动角度与非均匀增益提成常量、血屏衰减提成常量、贴脸距离下限提成 `Culling`/粒子层常量 |
| M10 `FireRateMultiplier` 无调用方 | ✅ 已修：作为默认射速间隔的倍率进入 `DefaultFireIntervalMs` |
| M11 头注释与代码不符（声称每帧复位游标，实为轮转游标） | ✅ 已修注释 |
| M12 `ViewModel.Tick(float)` 死参数、`WeaponAnim.Slot` 三入口只写不读 | ✅ 已删 `Tick`（刷新改由 `Refresh()`/`OnFire` 承担，计划 §9 已记修订）与 `Slot` |
| J1–J9（仅用例读的常量、整层暂无生产调用方、`MuzzleSmoke`、连射用例区分度、池数组直暴露、字段声明位置等） | ⏸ 记录：`MuzzleSmoke` 已补寿命；连射用例已补区分度（见规格轴第 4 条）；整层生产调用方要等场景装配（C07/C08 同口径）；其余保持 §9 冻结面，理由与 §3.2 合并 |

### 4.3 审查旁的复跑
两轴都独立复跑了 `SELFTEST` 与 `check-docs`/`check-assets`，确认注册数与实跑一致（8 条 `effects.*` / cases=55）、`Ac.Sim` 与 `Ac.Net` 零改动（ADR-010）、`ContractSuite` 白名单无新增引用；规范轴另确认上一轮的重复哈希原语没有复发（本层直接复用 `ArenaMesh.Hash` 之外不再自带哈希）。

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd'); "exit=$LASTEXITCODE"
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^PASS effects|^FAIL ' | ForEach-Object { $_.LineNumber.ToString() + ': ' + $_.Line.Trim() }
Select-String -Path client/Assets/Scripts/View/Tracer.cs -Pattern 'MuzzleWorld'
node tools/check-docs.mjs; node tools/check-assets.mjs
```
