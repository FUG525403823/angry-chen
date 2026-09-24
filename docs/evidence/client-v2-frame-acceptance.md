# C14 验收记录 + H6 装配根的处理

## 1. 装配根（审计 H6）已补齐

`client/Assets` 原先 0 个 `MonoBehaviour`、0 个 `.unity/.prefab`、`EditorBuildSettings.asset` 的 `m_Scenes` 为空、也没有 `RuntimeInitializeOnLoadMethod` —— **没有任何运行期装配根**，C01–C15 无一份计划交付它。本轮补上：

- 新程序集 **`Ac.Boot`**（组合根，引用全部六个程序集；不被任何运行期程序集引用 ⇒ 依赖图仍是 DAG，C01 §5.2 引用白名单相应扩充）。
- `GameLoop`：真的把帧回路接起来（命令→`Predictor`/`CommandBuffer`→收包解码 type 5/6/10→`LocalAuthority.TryProject`+`Reconciler`→`EntityViews.SyncFrame`→`Hud.Apply/Tick`），8 段 `FrameProfiler` 打点；**刻意不引用 UnityEngine**，帧基准与游戏共用同一条回路。
- `GameBootstrap`：`[RuntimeInitializeOnLoadMethod(BeforeSceneLoad)]` 自举，不依赖场景内容（"0 个 .unity"下唯一可行做法）。
- 补上此前**只存在于测试里**的 `EventEntry → HudEvent` 适配。
- `Ac.Tests.FrameBench.Run`：§9 的批处理入口，输出 §9 的 JSON；整体 try/catch + `EditorApplication.Exit(1)`，异常不会挂住门禁。

## 2. 实测（CPU 帧回路）

环境：Ryzen 9 5900HX / Tuanjie 2022.3.62t16 / **GPU = Null Device** / commit `ac9b9ec` / 64 实体 / `-frameBenchQuality 1`。

| 指标 | 实测 | §5 预算 |
|---|---|---|
| frameP95Ms | **0.0069** | ≤ 20 |
| frameP99Ms | **0.0088** | ≤ 33 |
| managedAllocBytesPerFrame | **0** | 0 |
| gc0Delta | **0** | 0 |
| stageP95 sync | 0.0033 ms | ≤ 1.5 |
| stageP95 input/predict/hud | 0.0001 ms 各 | 0.5/1.0/1.0 |

常驻用例：`boot.frame_loop`（帧回路端到端）、`boot.steady_state_zero_alloc`（64 实体 × 120 帧稳态 **0 B** 分配）；`SELFTEST OK cases=129`。

## 3. 门禁的真实行为（**修正上一版本文中的错误归因**）

上一版这份文档把"门禁退出 1"解释成"-1 触发 fail-closed"。**那是误归因**：当时的脚本读 `$sample.stageP95.<段>` 而基准写的是扁平键，实际是**模式不匹配**导致每轮都退出 1；而且 `-1 -gt 120` 为假，哨兵值其实会"通过"。双轴审查（`client/Logs/review-c14-{standards,spec}.md`）把这两条列为 blocker，现已修：

- 基准输出**嵌套 `stageP95`**，且只包含真的被 `Mark` 过的段；
- 任一图形指标 < 0 ⇒ 直接 `exit 2`（未测得绝不等于达标）；
- 出现 `Null Device` / `headless` ⇒ `exit 2`；
- 删掉我自己加的 `-Headless` 开关（它注入 `-nographics`，与 §5 测量法 1 和脚本第 3 行的 "never pass -nographics" 自相矛盾）；
- 汇总写入 §9 冻结的 `-Out` 路径（原先只校验不使用，§6 的 `Get-Content client/TestResults/frame-bench.json` 读的是永不存在的文件）；`sampleFrames = $Frames`（原先用了未定义的 `$SampleFrames`，且文档让人传不存在的 `-SampleFrames`）；
- 分位口径改成 §5 冻结的 `ceil(q*n)-1`（原先第三份实现 `floor(q*(n-1))`）；
- `GameLoop` 把镜像同步从 `draw` 段改回 `sync` 段。

**当前真实结论**：走 §5 合规命令（不加 `-nographics`）时编辑器在本机起不来 ⇒ 无 JSON ⇒ `exit 2`（环境不可用，不是"超预算"）；走 `-nographics` 得到上表 CPU 数字，但 `stageP95.fx/audio/draw/overlay` 缺段 ⇒ 门禁判 FAIL。**两种走法都无法得到 PASS**，因此 **C14 的 §6/DoD 在本机未验收**，未打 `MC14` 标签。

## 4. 仍未装配（C15/后续）

音频 `Layers/Mixer`、特效 `Effects/Particles`、`FpsCamera`、`AmmoLedger`/武器 HUD 数值、HUD 字符串渲染与场景 UI、`ArenaMesh/Colliders` 实例化；`Batching` §5 的档位表（剔除/阴影/实例上限）目前**没有生产调用者**，`View/Culling` 仍在用 C08 的 90m/60m ⇒ 该冲突（C08×C14）仍需裁决，未自签。
