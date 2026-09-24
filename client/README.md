# client —— Unity 客户端

本目录是 v2 重构后的**客户端工程根**（Unity 工程）。它与 `server/`（C++ 权威服务器）**完全分离**，两者只通过 [ADR-009](../docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md) 冻结的 UDP 协议通信，不共享源码。

| 入口                                 | 位置                                                             |
| ------------------------------------ | ---------------------------------------------------------------- |
| 线性制作计划（C01 → C15）            | [docs/plans-v2/client/](../docs/plans-v2/client/)                |
| 计划索引、跨链依赖与推荐执行顺序     | [docs/plans-v2/README.md](../docs/plans-v2/README.md)            |
| 技术前提（引擎/版本/依赖白名单）     | [docs/03-技术选型-v2.md](../docs/03-技术选型-v2.md)              |
| 零外部素材与问界额标策略             | [ADR-003](../docs/00-共识/ADR/ADR-003-美术素材与问界标识策略.md) |
| 跨语言确定性契约                     | [ADR-010](../docs/00-共识/ADR/ADR-010-跨语言确定性与对拍.md)     |
| 旧 TypeScript 客户端（冻结对照实现） | `packages/client/`                          |

**当前状态**：C01（MC01）已交付工程基线与构建链（程序集边界、URP 管线资产、零素材门、唯一构建入口）；C02（MC02）在其上落地确定性数学（`Vec3`、mulberry32 三流、量化/反量化、共享角度表）、v2 协议解码（命令/快照/事件/MatchState）与批处理自检框架。渲染、玩法与表现由 C03 起落地。实测记录见 `docs/evidence/client-c01-acceptance.md` 与 `docs/evidence/client-c02-acceptance.md`。

**环境**：编辑器入口固定为环境变量 `AC_UNITY`（User 作用域），本机已设为 `D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe`（设置与实测见 `docs/evidence/env-bootstrap.md`）。未重启的终端里 `$env:AC_UNITY` 可能为空，`client/build.ps1` 会回读 HKCU 里的同名变量。工程首次生成用 `-createProject client`，此后一律用 `-projectPath client`。

## 命令

工作目录为仓库根；参数、输出与退出码以 [C01](../docs/plans-v2/client/C01-Unity工程基线与构建.md) §5.3/§6/§9 为准。

| 目的 | 命令 | 期望 |
|---|---|---|
| 打开工程 | `& $env:AC_UNITY -projectPath client` | 编辑器打开 `client/` |
| 构建 Windows x64 | `powershell -NoProfile -File client/build.ps1 -Target Windows64` | 末行 `Build succeeded`，退出码 `0`（`1` 构建失败、`2` 编辑器缺失）；产物 `client/Build/Windows64/angry-chen.exe`，引擎日志 `client/Logs/build.log` |
| 分组自检 | `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Tests.SuiteRegistry.RunAll -logFile -` | 首行 `SELFTEST START cases=<n>`，每条用例一行 `PASS <用例 id>`，末行 `SELFTEST OK cases=<n>`，退出码 `0`；有失败时逐行 `FAIL <用例 id> expected=<e> actual=<a>`，末行 `SELFTEST FAIL failures=<n> cases=<n>`，退出码 `1` |
| 核心自检 | `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Core.SelfTest.Run -logFile -` | 末行 `SELFTEST OK cases=<n>`，退出码 `0`（只覆盖 Core 层的受限 JSON 读取器） |
| 角度表校验（共享产物） | `node tools/export-trig-table.mjs --check` | `trig-table.json OK (65536 entries) sin=0x8BD9F737 atan=0x197C3A8D asin=0xAD2BD35E`，退出码 `0` |
| 零素材门 | `powershell -NoProfile -File client/tools/verify-assets.ps1` | `OK：client 零外部素材`，退出码 `0`；命中则逐行 `BANNED <路径>` 且退出码 `1` |
| URP 管线资产（幂等） | `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Editor.RenderPipelineSetup.Run -logFile -` | `[urp] pipeline=Assets/Settings/UniversalRenderPipeline.asset` |
| 仓库门禁 | `node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` | 两条都 `OK`，退出码 `0` |

> Tuanjie.exe 是 GUI 子系统程序：PowerShell 的 `&` 不会等它结束（`$LASTEXITCODE` 为空），`Start-Process -Wait` 会连它的后代进程一起等而挂死，`Start-Process -PassThru` 的 `ExitCode` 实测取不到值。`client/build.ps1` 因此把命令行写进 `client/Logs/build.cmd` 交给 `cmd /c` 代跑（cmd 会等 GUI 程序结束并原样传回退出码）；手工跑批处理也请用 `cmd /c`。

## 验证（C01 §6 六条命令）

按顺序逐条跑，输出与期望逐字匹配即算过；实测记录见 `docs/evidence/client-c01-acceptance.md`。

1. 素材门：`powershell -NoProfile -File client/tools/verify-assets.ps1` → 期望 `OK：client 零外部素材`。
2. 版本、序列化与依赖白名单：`Get-Content client/ProjectSettings/ProjectVersion.txt` 首行为 `m_EditorVersion: 2022.3.62t16`，第二行的 changeset 与 `(Get-Item $env:AC_UNITY).VersionInfo.ProductVersion` 一致；`Select-String -Path client/ProjectSettings/EditorSettings.asset -Pattern "m_SerializationMode: 2"` 命中；`client/Packages/manifest.json` 的 `dependencies` 全部命中 `tools/check-assets.mjs` 的 `UNITY_ALLOWED` 白名单。
3. 程序集图：`Get-ChildItem client/Assets -Recurse -Filter *.asmdef | ForEach-Object { $_.Name; (Get-Content -Raw $_ | ConvertFrom-Json).references }` → 每份 `references` 是 C01 §5.2 允许集合的子集。
4. 构建：`powershell -NoProfile -File client/build.ps1 -Target Windows64` → 退出码 `0`（`1` = 构建失败、`2` = 编辑器缺失），末行 `Build succeeded`，在 `client/` 下 `Test-Path Build/Windows64/angry-chen.exe` 为 `True`。
5. 生成目录不入库：`git status --porcelain -uall client` → 逐个列出的都是 C01 §3 的文件；出现 `Library/`、`Temp/`、`Logs/`、`Build/`、`UserSettings/` 下任何文件即失败。
6. 仓库质量门：`node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。

自检入口 `Ac.Tests.SuiteRegistry.RunAll` 注册的用例：C01 的 `c01.assemblies.references`、`c01.project.identity`、`c01.project.serialization`、`c01.build.backend`、`c01.render.pipeline`，加上 C02 的 `vec3.ops`、`rng.streams`、`quantize.edge`、`quantize.rounding`、`codec.roundtrip`、`fixtures.loader`。

## 验证（C02 §6 六条命令）

按顺序逐条跑，输出与期望逐字匹配即算过；实测记录见 `docs/evidence/client-c02-acceptance.md`。

1. 角度表：`node tools/export-trig-table.mjs --check` → `trig-table.json OK (65536 entries) sin=0x8BD9F737 atan=0x197C3A8D asin=0xAD2BD35E`。
2. 核心自检：`& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Core.SelfTest.Run -logFile -` → 末行 `SELFTEST OK cases=<n>`。
3. 禁超越函数扫描：`Get-ChildItem client/Assets/Scripts/Sim, client/Assets/Scripts/Net -Recurse -Include *.cs | Select-String -Pattern '(Math|MathF)\.(Sin|Cos|Tan|Sinh|Cosh|Tanh|Asin|Acos|Atan|Atan2|Exp|ExpM1|Log|Log2|Log10|Log1p|Pow|Cbrt|Hypot)'` → 无输出；`client/Assets/Scripts/Sim/` 与 `Net/` 内角度只能经 `TrigTable`。
4. 分组自检：第 2 条的 `-executeMethod` 换成 `Ac.Tests.SuiteRegistry.RunAll` → 含 `PASS codec.roundtrip`、`PASS quantize.edge`、`PASS rng.streams`、`PASS fixtures.loader` 与末行 `SELFTEST OK cases=<n>`。
5. 仓库门禁：`node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。
6. 构建不回归：`powershell -NoProfile -File client/build.ps1 -Target Windows64` → 退出码 `0`、末行 `Build succeeded`。

## 目录约定（C01 §5.3）

| 路径 | 用途 |
|---|---|
| `Assets/Scripts/Core/`、`Assets/Scripts/Sim/` | 基础设施（断言、受限 JSON）；客户端仿真（确定性数学、量化、编解码、局部步进） |
| `Assets/Scripts/Net/`、`Assets/Scripts/View/` | UDP 传输与会话；相机、插值、实体视图 |
| `Assets/Scripts/UI/`、`Assets/Scripts/Audio/` | 面板与 HUD；程序化合成与混音 |
| `Assets/Editor/`、`Assets/Tests/` | 编辑器工具与构建入口；编辑器专用自检，不进包体 |
| `Assets/Settings/`、`Assets/UniversalRenderPipelineGlobalSettings.asset` | URP 管线、渲染器与全局设置资产（纯文本 YAML） |
| `tools/`、`build.ps1` | 零素材门；唯一构建入口 |

程序集依赖方向冻结为 `View → Net → Sim → Core`（`Ac.Audio → Ac.Core`；`Ac.UI → Ac.View/Net/Sim/Core`）；`Ac.Editor` 与 `Ac.Tests` 平台仅 `Editor`（`Ac.Editor` 另引两个 URP 运行时程序集，仅用于管线资产引导）。新增代码只能落在上表目录内，方向由 `Ac.Tests.SuiteRegistry.RunAll` 的 `c01.assemblies.references` 用例守住。

## 生成目录与入库规则

- `Library/`、`Temp/`、`Obj/`、`Logs/`、`Build/`、`Builds/`、`UserSettings/` 由编辑器生成，**不入库**（见 `.gitignore`）：构建产物在 `Build/Windows64/`，引擎与批处理日志在 `Logs/`。
- `ProjectSettings/*.asset`、`Packages/manifest.json`、`Packages/packages-lock.json`、`Assets/**`（含 `*.meta`）全部纯文本入库（ForceText + Visible Meta Files）。
- 零外部素材：网格、材质、shader、音频、字体一律代码生成或用系统字体；图片/音频/模型/字体文件放不进仓库（`client/tools/verify-assets.ps1` 与 `node tools/check-assets.mjs` 双重判定）。

硬约束（来自 ADR-003 / ADR-008 / ADR-010）：

1. 零外部素材：网格、材质、shader、音频全部代码生成；**不导入 TMP Essentials 等素材包**，UI 文本用系统字体（`Font.CreateDynamicFontFromOSFont`）。
2. 依赖白名单：只用 Unity 官方包（URP；输入用内置 Input，不引 Input System），无第三方包、无 Asset Store 素材。
3. 模拟相关代码（预测/和解/数学）必须遵守 ADR-010 的确定性规则，并与服务端吃同一批对拍向量。
4. `Library/`、`Temp/`、`Build*/` 等生成目录不入库（见 `.gitignore`）。
