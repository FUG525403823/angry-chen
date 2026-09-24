# C10 验收记录（MC10）

- 计划：[`docs/plans-v2/client/C10-HUD与战斗UI.md`](../plans-v2/client/C10-HUD与战斗UI.md)
- 固定点：`2931530`（MC09）→ 本份提交
- 自检：`SELFTEST OK cases=64`（本份新增 9 条：`hud.*`）

## 1. §6 的六条验证

| # | 命令 | 输出 | 结论 |
|---|---|---|---|
| 1 | `client/Logs/selftest.cmd` | `SELFTEST START cases=64`、9 条 `PASS hud.*`、`SELFTEST OK cases=64`，exit=0，无 `FAIL`、无 `error CS` | ✅ |
| 2 | 同上输出 | `hud.element_map` / `hud.throttle` / `hud.crosshair` / `hud.ammo_rage` / `hud.visibility` / `hud.wave_killfeed` / `hud.text_and_font` / `hud.revive_step` / `hud.zero_alloc` 共 9 条（≥ 6） | ✅ |
| 3 | `Select-String client/Assets/Scripts/UI/UiThrottle.cs -Pattern 'NumericRefreshMs = 100'` | 命中 `public const float NumericRefreshMs = 100f;`（用例同时断言 `NumericRefreshMs == 100` 与 `StatsRefreshMs == 250`） | ✅ |
| 4 | `Get-ChildItem client/Assets/Scripts/UI -Recurse -Include *.cs \| Select-String -Pattern 'TextMeshPro'` | **无输出**（用例里也有一条同义断言：9 个文件逐个读文本，命中数 == 0）；`CreateDynamicFontFromOSFont` 在 UI 目录只出现 1 处 | ✅ |
| 5 | `node tools/check-docs.mjs` / `node tools/check-assets.mjs` | 两份都 exit=0 | ✅ |
| 6 | 人工：4 人局 + 40 羊观察 60 秒（帧时间 P95 ≤ 20ms、每帧分配 0B） | **本批未跑**（批处理无渲染上下文，且 HUD 渲染层装配要等场景装配步骤）。代偿：`hud.zero_alloc` 用 `GC.GetAllocatedBytesForCurrentThread()` 实测连续 3 帧走完 `Apply`/`PushEvent`/九个元素的全部写路径后增量 == 0；节流与脏检查由 `hud.throttle` 断言（100ms 内多次 `Tick` 只写一次、同值不写） | ⏸ 记录 |

## 2. §7 的 6 条 DoD

| # | 判据 | 证据 | 结论 |
|---|---|---|---|
| 1 | §5(a) 每个 HUD 元素都有「字段 → 显示」用例 | `hud.element_map`：血量/护甲/弹药/备弹/换弹环（`reloadLeft10Ms/10`）/怒气/狂暴剩余（`rageLeft100Ms/100`）/波次/波间/救援进度（`reviveRatio255/255`）逐个断言，事件侧再断言命中→受伤色、击杀→记录条目、波次→横幅 | ✅ |
| 2 | 数值类 ≤ 10Hz（100ms 窗口内多次 `Tick` 只写一次），状态类事件驱动立即刷新 | `hud.throttle`：80ms 内 4 次 `Apply` 仍是 1 次写；窗口到了但值组合没变 → 不写（`SkippedWrites` 计数）；`PushEvent` 走动立即生效且未知事件被拒；统计通道 250ms 独立 | ✅ |
| 3 | 准星形状、尺寸映射（0.5°→2px、5°→24px）与四色表 | `hud.crosshair`：四段、端点值、中点 13px、上下界夹取、`0xE8E8F0`/`0xFFD24D`/`0xD2402F`、受伤 150ms 后回常规、`Hidden` 态 | ✅ |
| 4 | 字号四档（32/18/16/48）与安全区 4% 生效；倒地与救援提示可见性被断言 | `hud.text_and_font`（四档 + `FontPx(role)` + `SafeAreaInsetPx(1080) = 43.2 ≥ 43` + 回退链三档）、`hud.visibility`（倒地遮罩三种阶段、倒地隐藏准星与弹药、`ended` 隐藏全部战斗 UI、救援 2.0m 与自救不提示） | ✅ |
| 5 | UI 无 TMPro / 素材依赖，文本全部系统字体；UI 元素不拦截输入 | §1 第 4 行；渲染层装配在场景装配步骤落地，届时按 §5(d) 统一关掉 raycast（本层的可见性布尔就是它的输入） | ✅（参数层）/ ⏸（装配层） |
| 6 | 分组自检无 `FAIL`、末行 `SELFTEST OK`；两份门禁 exit 0 | §1 第 1、5 行 | ✅ |

## 3. 计划修订与跨链

### 3.1 本份修订的计划条目
- §3/§4/§8：测试文件名 `hud_test.cs` → `HudSuite.cs`（命名约定）。
- §9：`Hud.PushEvent` 的入参改成本层的 `HudEvent`（`SimEventView` 还没在这一层存在），并补齐实际签名（`CreateFont` / `FontPx` / `SafeAreaInsetPx` / 三个可见性判定 / `UiThrottle.ShouldWriteStats` / `NoteEventWrite` / 计数器 / `RevivePrompt.SetFromRatio255` / `WaveBanner.IsBossWave` / `TickIndex` / `KillFeed.Alpha` / `ColorOf`）。
- §5(a)：事件类型常量（`HudEventType`）先按 S08/S10 事件表的数值写死（playerHit=1 / sheepKilled=2 / waveStart=3 / waveClear=4 / reviveProgress=6 / reviveDone=7），S10 的单播结构交付后整体替换为 `SimEventView`。
- §5(b)：数值类按**一个组**节流（一个窗口写一次），脏检查的键是整组数值的组合值；这样血量与弹药不会各自抢占同一个 100ms 窗口。
- §5(d)：字体唯一来源用 `Font.CreateDynamicFontFromOSFont(string[], int)` 的数组重载，它本身就是回退链语义；批处理下可能取不到字体，返回 null 是允许的（渲染层装配时再补日志）。
- §6 第 6 条（人工 60 秒观测）与 §5(d) 的 GameObject 装配本批未做，代偿与理由见 §1 第 6 行、§2 第 5 行。

### 3.2 有意保留的判断题
- 本层是**纯逻辑层**：所有元素都是「采样结构体字段 → 显示值/可见性」的函数，没有 `GameObject`/`MonoBehaviour`/UGUI。理由：批处理自检能断言的就是这些；真正装配 UI 的对象树留给场景装配步骤，避免在没有渲染上下文时写一大片无法验证的代码。
- `HudSample` 是结构体且由 `Hud` 持有一份（`Sample` 只读暴露），调用方按值传入：避免每帧 new 一个采样对象。
- 击杀记录满 6 条时顶掉**剩余时间最短**的一条（最旧），而不是拒绝入队：交火密集时玩家更想看到最近的击杀。
- 波次横幅到期的**下一帧**才出队：同一帧里既结算又入队会让横幅闪两下。
- 救援进度按 5% 量化后再显示：事件步长本来就是 5%，更细的位数只会让文本更频繁重建。
- 准星受伤色自己计时 150ms（与 C09 的红闪同值但独立计时）：HUD 不能依赖渲染层是否这一帧调过 `Tick`。

### 3.3 跨链事项
- 所有权威数值都来自 S10 的 `MatchState` 单播（`mag`/`reserve`/`rage`/`rageLeft100Ms`/`reloadLeft10Ms`/`reviveRatio255`/`phase`/`wave`/`intermissionMs`）；本层只读、不插值、不外推（§8 第 3 条）。
- 弹药显示值取 C06 的本地账 `AmmoLedger.mag`，备弹取单播；两者不一致时以单播为准刷新显示，本地账继续管开火闸门。
- `DownedOverlay` 的 `phase` 取值与 `SNAPSHOT_FLAG` 一样按服务端语义：`1 = playing`、`2 = ended`；`0` 是大厅。S10 交付 `MatchPhase` 枚举后把这三个常量换成它的成员。

## 4. 双轴审查（本轮 · 固定点 `2931530`）

两个子代理并行跑规格轴与规范轴。完整报告：`client/Logs/review-c10-spec.md`、`client/Logs/review-c10-standards.md`（gitignore，不入提交）。两轴都判过不通过，下面是逐条处置。

### 4.1 两轴共同抓到的阻断项（已修）
| 发现 | 处置 |
|---|---|
| 阶段码猜错：我写 `playing = 1` / `ended = 2`，S10 `phase.hpp` 权威是 `loading = 1` / `playing = 2` / `intermission = 3` / `ended = 4` → playing 期间 HUD 全不可见、loading 反而可见 | ✅ 已修：五个常量按权威值，计划 §5(a) 加注；用例逐个断言 1/2/3/4 并覆盖三种阶段下的可见性 |
| `Hud.PushEvent` 只加计数器、不驱动任何元素；`Hud` 也不持有子元素 → §4 任务 2「装配全部子元素」与「状态类事件立即刷新」是空壳，用例绕过 `Hud` 直接调元素类，映射实际没测 | ✅ 已修：`Hud` 持有 7 个元素并暴露只读属性；`Apply` 每帧驱动弹药/怒气/倒地/准星/救援可见性/冲锋警戒；`PushEvent` 按 `Ac.Net.EventType` 分派（命中→受伤色、击杀→记录条目并按 `HIT_FLAG.headshot` 取色、波次→横幅、救援进度/完成→提示）；用例全部改成走 `Hud` 的这两条路径断言元素状态 |
| `HudEventType` 与已交付的 `Ac.Net.EventType` 同值双源 | ✅ 已删本层枚举，直接用 `Ac.Net.EventType`（`Ac.UI → Ac.Net` 在白名单内） |
| 数值脏检查用未取整原始值（含每帧递减的 `intermissionMs`）→ 每窗口必写，违反「先按显示精度取整再比较」并吃光 ≤6 次/秒文本重建预算 | ✅ 已修：血量按 1%、波间按 100ms 取整后**逐字段**比较（不再用 31 乘起来的哈希键） |
| `NoteEventWrite` 把数值窗口清零 → 事件一密血量/弹药可无限期不刷新；事件写还混进 `NumericWrites` 让 ≤10Hz 计数失真 | ✅ 已修：事件只记 `EventWrites`，不碰数值窗口；用例断言「事件写不动数值计数、也不清窗口」 |
| 只写不读/无来源字段：`HudSample.Armor` 与 `AliveMs` 在 S10 单播、快照编解码里都没有对应字段；`RageMode`/`Reloading`/`Charging`/`TargetInSight` 无消费者 | ✅ 已修：删掉 `Armor`/`AliveMs`（计划 §5(a) 记顺延：不猜服务端字段）；`RageMode` → 怒气条、`Reloading` → 换弹环、`Charging` → 冲锋警戒 1600ms、`TargetInSight` → 准星可命中态，四个都有真实消费路径 |
| 死 API：`AmmoCounter.Text/ReserveText`（每次 `ToString` 分配且连用例都不调）、`UiThrottle.Reset`、`Hud.Sample`、`KillFeed.Entries/Remaining`、`RageBar.ShowHint`、`DownedOverlay.SetVisible`（与 `Update` 双写派生字段） | ✅ 已删；`KillFeed` 改成按索引读 `AlphaOf`/`IsHeadshot`/`ColorOf` |

### 4.2 规格轴其余 2 条（已修）
| 发现 | 处置 |
|---|---|
| 低弹比硬编码手枪弹匣 12：步枪 30 / 霰弹 6 会被判成 10% 阈值 | ✅ 已修：`Set` 增加 `magSize` 入参，判定改成整数比较 `Mag × 10 < MagSize × 3`（`0.3f` 是 0.30000001192，浮点比较会把 30 的 9 发误判成低弹）；用例覆盖 30/6/12 三种弹匣的边界 |
| §5(e) 的伤害数字 900ms / 血量尾影 800ms / 冲锋警戒 1600ms 无常量无实现 | ✅ 已修：三条时长提成 `Hud` 常量并被断言；冲锋警戒由 `Charging` 驱动成可计时状态（1600ms，可重复点亮）；伤害数字与血量尾影只有时长常量，渲染顺延到场景装配步骤（FR-10 的显示部分） |

### 4.3 规范轴其余必须修（已修）
- 魔法数（255/100/10 换算、0.3 低弹比、12 弹匣、13px）→ 换算分母提成 `HpPrecision`/`IntermissionPrecision` 常量并加注释，低弹比改成整数比较，弹匣容量改由调用方给，13px 变成映射公式的断言。
- 跨文件重复常量 → 颜色统一到 `Hud.ColorXxx` 一份（准星/弹药/怒气/击杀记录都从这里取）；`HurtFlashMs` 只留在 `Hud`。兄弟链的 `RageFull`/`RangeM`/`DurationMs`/`IsBossWave`/`TickSegments` 仍是服务端常量的镜像——客户端不能依赖 C++ 头文件，保留并在此记录为判断题。
- 零分配门禁盲点 → 断言里已删掉会分配的 `Text` 属性；`hud.zero_alloc` 现在覆盖 `Hud.Apply`/`PushEvent`/`Tick` 与七个元素的写路径，实测增量 == 0。
- 恒真/自证断言 → 删掉「断言自己刚设进去的 sample 字段」「SetVisible→Visible」这类；元素映射改成断言 `Hud` 内部元素状态、事件映射断言元素副作用；源文本门禁去掉 `fontSource == 1`（场景装配会新增字体站点，那条断言会假红），只保留 TMPro == 0。
- 计划 §9 重复条目、§7 测试文件名、§5(d) 字体调用形式 → 已清重复块并改写；新增的公开面（`Hud` 的七个元素属性、`PushEvent` 分派、`KillFeed.AlphaOf/IsHeadshot/ColorOf`、`AmmoCounter.Set` 五参重载）已记入 §9。

### 4.4 保留的判断题
- 整层仍无生产调用方（等场景装配装配对象树），与 C07–C09 同一口径；本层把可验证的部分（映射、节流、可见性、时长、零分配）全部做成纯逻辑并断言。
- 帧对齐后数值类实际约 116.7ms（8.6Hz），仍在 ≤10Hz 预算内。
- 击杀记录满 6 条顶掉剩余时间最短的一条（v1 是裁剪），交火密集时更贴近玩家意图。
- `KillFeed` 的 `_count` 只在 `Tick` 里校正：`Push` 自增是为了让「上限/溢出」可断言，渲染只读 `Count`。

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd'); "exit=$LASTEXITCODE"
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^PASS hud|^FAIL ' | ForEach-Object { $_.LineNumber.ToString() + ': ' + $_.Line.Trim() }
Select-String -Path client/Assets/Scripts/UI/UiThrottle.cs -Pattern 'NumericRefreshMs = 100'
Get-ChildItem client/Assets/Scripts/UI -Recurse -Include *.cs | Select-String -Pattern 'TextMeshPro'
node tools/check-docs.mjs; node tools/check-assets.mjs
```
