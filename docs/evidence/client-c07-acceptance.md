# C07 验收记录（MC07）

- 计划：[`docs/plans-v2/client/C07-程序化场地与光照.md`](../plans-v2/client/C07-程序化场地与光照.md)
- 固定点：`29fd227`（MC06）→ 本份提交
- 自检：`SELFTEST OK cases=41`（本份新增 6 条：`arena.*`）

## 1. §6 的五条验证

| # | 命令 | 输出 | 结论 |
|---|---|---|---|
| 1 | `client/Logs/selftest.cmd` | `SELFTEST START cases=41`、6 条 `PASS arena.*`、`SELFTEST OK cases=41`，exit=0，全输出无 `FAIL` | ✅ |
| 2 | 同上输出 | `arena.mesh_budget` / `arena.mesh_determinism` / `arena.fence_barn_values` / `arena.colliders` / `arena.materials` / `arena.lighting` 共 6 条（≥ 4） | ✅ |
| 3 | `Select-String ArenaMesh.cs -Pattern 'OuterRingAmplitudeM\|Height\('` | `OuterRingAmplitudeM` 命中定义行与 `Height()` 内唯一使用点；`Height()` 的调用点全部在 `BuildOuterRing` 的 `Emit` 内，可走区域构建里没有任何调用 | ✅ |
| 4 | `node tools/check-assets.mjs` | `OK：仓库零外部素材，依赖白名单未被破坏。`，exit=0（贴图全部 `Texture2D` 代码生成，无图片文件） | ✅ |
| 5 | 人工：编辑器播放 1080p 绕场一周 | **本批未跑**（批处理无渲染上下文，且编辑器被另一会话占用）。代偿：预算由 `arena.mesh_budget` 断言（顶点 1514 ≤ 4096、三角形 1400 ≤ 3072、材质 6、绘制调用 6 ≤ 8），退化阶梯见计划 §8 | ⏸ 记录 |

## 2. §7 的 6 条 DoD

| # | 判据 | 证据 | 结论 |
|---|---|---|---|
| 1 | 80m×80m、单元 4m；地面 21×21 顶点、800 三角形；同种子逐顶点一致 | `arena.mesh_budget`（`GroundMesh.vertexCount == 441`、`triangles.Length/3 == 800`）、`arena.mesh_determinism`（两次 Build 的地面与环带顶点逐元素相等） | ✅ |
| 2 | 围栏 3.0×0.5 四边闭合；谷仓六值与 S06 逐值相等 | `arena.fence_barn_values`（半场 40、内表面 39.75、可走上界 39.35、谷仓 x/z/高度与 `MoveConfig` 同值，并反向断言 `MoveConfig` 本身是 −4..4 / −4..4 / 5 与半径 0.4）、`arena.colliders`（四面内表面 = 39.75、盒高 3.0、谷仓盒六值） | ✅ |
| 3 | 顶点 ≤ 4096、三角形 ≤ 3072、材质 ≤ 6、绘制调用 ≤ 8 | `arena.mesh_budget`：实测 **1514 / 1400 / 6 / 6**，统计值由七张部件 Mesh 的 `vertexCount` 与 `triangles.Length` 求和而来（没有凑数常量） | ✅ |
| 4 | 材质表 6 行与 §5(c) 逐值一致；贴图全部代码生成 | `arena.materials`：6 个基色按 RGB 整数逐个断言、6 行粗糙度/金属度逐个断言、三张贴图 256/256/128 且挂在草/土/木材质上（`mainTexture` 同一实例）、铺贴单元 6/4/2.4m 常量断言；`check-assets` 零素材 | ✅ |
| 5 | 光照与阴影：方向光 1、贴图 2048、距离 60m、2 级级联、雾 42–138m | `arena.lighting`：方向光类型/俯角 50°/方位 −30°/强度 1.1/颜色/软阴影、线性雾 42–138、纯色环境光 0.45、相机只改清屏色且 FOV 仍是 75；阴影预算**直接断言管线资产** `client/Assets/Settings/UniversalRenderPipeline.asset` 的四行（`m_ShadowDistance: 60`、`m_ShadowCascadeCount: 2`、`m_SoftShadowsSupported: 1`、`m_MainLightShadowmapResolution: 2048`） | ✅ |
| 6 | 自检无 `FAIL` 且末行 `SELFTEST OK`；两份仓库门禁 exit 0 | §1 第 1、4 行 + `node tools/check-docs.mjs` 全绿 | ✅ |

## 3. 计划修订与跨链

### 3.1 本份修订的计划条目
- §3/§4：测试文件名 `arena_mesh_test.cs` → `ArenaSuite.cs`（命名约定）。
- §5(a) 网格产出：每个部件（地面/环带/围栏/谷仓/顶/土场/干草）都造出真 `Mesh`（顶点 + 索引 + UV + 法线 + 包围盒），`MeshStats` 由各 Mesh 求和；渲染器与 Collider 的场景装配留给场景步骤；`MeshStats.DrawCalls` 是材质分组数（预算计数）。
- §5(a) 远景环带：改成**四条矩形带**（南北 z∈±[40,60]、东西 x∈±[40,60]，带宽 20m 用 4m 格、长度方向 8m 格），共 2×(5×15 + 10×5) = 250 格、1000 顶点。原先按整格中心判据会剔掉跨越 40m 的格子，留下 40–44m 空洞（规格审查 M3 实算发现）；现在内缘正好落在 40m，`arena.mesh_determinism` 断言最近点 == 40m。
- §5(c)：三张代码生成贴图挂进材质（`mainTexture`/`_BaseMap`），铺贴单元 6/4/2.4m 写进网格 UV 并作为 `ArenaParams` 常量断言。
- §5(d)：阴影预算的**落点是 URP 管线资产**，不是 `QualitySettings`——URP 14.2 运行时只读管线资产（`UniversalRenderPipeline.cs` 的 `settings.shadowDistance` / `shadowCascadeCount` / `mainLightShadowmapResolution` / `supportsSoftShadows`），写 `QualitySettings` 一点效果都没有（规格审查 M1）。本份已把资产改成 60m / 2 级 / 软阴影开启（2048 本来就是），`LightingRig` 不再写 `QualitySettings`，用例直接断言资产文本。「2 级分割 0.25」在 URP 资产里没有对应字段，改由资产级联数 2 覆盖。
- §6 第 5 条（人工帧时间）本批未跑，代偿与退化阶梯见 §1 第 5 行。
- §9：`ArenaColliders.Build() → ArenaColliders`（`Boxes[5]` + 锚点 + 两个上界）、`Materials` 增 `CreateTextures`/`Roughness`/`Metallic`、`LightingRig` 保留 `Apply`（= `ApplySun` + `ApplyEnvironment`）、`ArenaMesh` 为构造器注入 + `Build()`（原写 `Build(ArenaParams)`，照原样写会编译不过）。

### 3.2 有意保留的判断题
- `ArenaMesh.Build` 不创建 `GameObject`/`MeshRenderer`：批处理自检没有渲染上下文，造对象只会污染场景；`DrawCalls` 因此按材质分组数计（6），真正的渲染器装配与绘制调用统计留给场景步骤。
- 哈希原语 `ArenaMesh.Hash` 一份、地形与贴图共用；它与 `Ac.Sim.Rng` 的流式 mulberry32 不是同一种东西（这里要的是任意输入哈希），合并需要 `Ac.Core` 出一个哈希原语，留作收敛项。
- 干草垛落在可走区内且不生成碰撞体：它只是观感物件，§5(b) 的静态碰撞体只有围栏与谷仓两组，波次与模拟都不读它。
- 土场圆盘用 `System.Math.Cos/Sin`：`Ac.View` 是 ADR-010 的豁免层（C04 判例），圆盘不参与模拟；12 个生成点则刻意写成坐标表以保证四个正方向点逐位精确。
- 浮点等于比较集中在冻结常量与同源写入值之间（本轮已核对），带容差的比较见 `arena.fence_barn_values` 的半径与间距断言。

### 3.3 跨链事项
- 几何数值只有一处定义：`ArenaParams.Default()` 全部从 S06 的 `MoveConfig` 派生（半场、围栏半厚、谷仓六值、玩家半径），用例再反向断言 `MoveConfig` 本身，所以「视觉与模拟不打架」是结构性的。
- 可走上界 `39.75 − 0.4 = 39.35` 与 S06 的 `limit` 同值，由 `arena.fence_barn_values` 断言；今后改一处即可同时影响模拟与围栏内表面。
- 生成点半径 38m、最小间距 19.67m（≥ 15m）与 S09 的取位约束一致，波次导演可直接取用 `SpawnPoints`。
- 相机参数沿用 C05 冻结值（FOV 75、近 0.1、远 300），本份只加清屏色与环境光/雾。
- 自检不再写全局 `QualitySettings`：早先的写法会把它序列化回 `ProjectSettings/QualitySettings.asset`，污染共享工作区（本份实跑踩到，已改）。

## 4. 双轴审查（本轮 · 固定点 `29fd227`）

两个子代理并行跑规格轴与规范轴（各自读计划/标准与全部新增文件，各自复跑可跑的门禁）。完整报告：`client/Logs/review-c07-spec.md`、`client/Logs/review-c07-standards.md`（gitignore，不入提交）。

### 4.1 规格轴（6 必须修 + 7 判断题）
| 发现 | 处置 |
|---|---|
| M1 阴影 60m/2 级/软阴影完全未生效：只写 `QualitySettings`，URP 只读管线资产 | ✅ 已修：资产改成 60m / 2 级 / 软阴影开启；`LightingRig` 删掉 `QualitySettings` 写入路径；`arena.lighting` 改为直接断言资产四行 |
| M2 围栏/谷仓/顶/土场/干草的顶点数组造完即弃，且 barn/roof 按 12 槽计只写 8 槽 | ✅ 已修：每个部件都造真 `Mesh` 并暴露，统计值由各 Mesh 求和；数组尺寸与写入量对齐 |
| M3 环带实际只覆盖 44–60m（40–44m 空洞） | ✅ 已修：改成四条矩形带，内缘正好 40m（见 §3.1）；用例断言最近环带点 == 40m |
| M4 三张代码生成贴图没挂到材质，6/4/2.4m 铺贴单元无实现 | ✅ 已修：贴图挂进材质，铺贴单元写进 UV 并断言常量 |
| M5 验收记录的网格数字与代码不符（记录 1127/1200/150格/600顶点） | ✅ 已修：按当前代码重算并钉进用例（1514/1400，环带 250 格 1000 顶点），本记录同步 |
| M6 §9 仍冻结 `Build(ArenaParams)`，实现是构造器注入 | ✅ 已修计划 §9 |
| J1–J7（光源数/投影渲染器只在常量里、S09 口径、`MeshStats` 取值来源、干草垛、`ShadowResolution.High` 出处、超越函数豁免等） | ⏸ 记录：J1 属场景装配阶段；J2 属计划口径表述，已在 §3.3 按 S09 原文修正口径；J3 已随 M2 消解（统计由 Mesh 求和）；J5/J7 见 §3.2；J6 随 M1 消解（不再引用 Unity 枚举） |

### 4.2 规范轴（10 必须修 + 4 判断题）
| 发现 | 处置 |
|---|---|
| 死属性只写不读：`ArenaMesh.Stats` | ✅ 已删（`Build()` 直接返回结构） |
| `Params` 只有内部读者 | ⏸ 保留：它是构造器注入的参数集合，`Height`/`OuterRingCells` 都读它；改成私有字段会让「参数从哪来」不可见 |
| 无调用方 API 一串（`CreateTextures`/`SetPalette`/`CreateColliders`/常量等） | ✅ 部分：`CreateTextures`/`Roughness`/`Metallic`/锚点常量/`SpawnPointRadiusM`/`BoxCount` 现在都被用例读；`SetPalette`/`CreateColliders` 是 §9 冻结接口（场景装配与色盲调色板） |
| mulberry32 逐字两份 | ✅ 部分：合并为 `ArenaMesh.Hash` 一份，Materials 调它；与 `Ac.Sim.Rng` 的合并需要 `Ac.Core` 出哈希原语，记为收敛项（§3.2） |
| `Create` 与 `ApplyPalette` 各写一遍六行 `Bind` | ✅ 已修：参数表只写一份（`Names`/`RoughnessTable`/`MetallicTable` + `ColorOf`），`Create` 复用 `ApplyPalette` |
| `stats.Materials/DrawCalls` 双写常量、几何三角形是凑数常量 | ✅ 已修：材质数取自 `Materials.MaterialCount`，三角形/顶点取自各 Mesh |
| 恒真断言（`GridCells+1`/`GroundTriangles` 自证、交叉断言只把常量重算一遍） | ✅ 已修：改成字面量 441/800/1514/1400/1000 与预算比较 |
| 用例裸 5/4/12 与零引用常量各自为政 | ✅ 已修：用例读 `ArenaColliders.BoxCount`/`PlayerSpawnCount`/`SpawnPointCount`/`SpawnPointRadiusM` |
| `Fence()` 每个盒子重算 `ArenaParams.Default()` | ✅ 已修：`Build()` 只取一次并传 `in ArenaParams` |
| 计划未同步（`arena_mesh_test.cs`、`ColliderSet`、`Build(ArenaParams)`） | ✅ 已修计划 §3/§4/§9 |
| 判断题（浮点等于、`OuterRingVerticesCap`、`Apply` 无生产调用方、`SetPalette` 不销毁旧对象等） | ⏸ 记录：浮点等于集中在冻结值同源比较，已在 §3.2 说明；`Apply`/`SetPalette`/`CreateColliders` 的调用方是 C08 起的场景装配 |

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd'); "exit=$LASTEXITCODE"
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^PASS arena|^FAIL ' | ForEach-Object { $_.LineNumber.ToString() + ': ' + $_.Line.Trim() }
Select-String -Path client/Assets/Settings/UniversalRenderPipeline.asset -Pattern 'm_ShadowDistance|m_ShadowCascadeCount|m_SoftShadowsSupported|m_MainLightShadowmapResolution'
Select-String -Path client/Assets/Scripts/View/ArenaMesh.cs -Pattern 'OuterRingAmplitudeM|Height\('
node tools/check-assets.mjs; node tools/check-docs.mjs
```
