# C01 Unity 工程基线与构建
> 里程碑：MC01 ｜ 预估：1 人日 ｜ 上游移交物：HANDOFF-C00

## 1. 目标

本份结束后，`client/` 是一个能被团结引擎（Tuanjie，编辑器 `2022.3.62t16`）打开、编译、并在一条命令下产出 Windows x64 可执行文件的工程：六层程序集边界单向，工程文件全部纯文本入库，`client/tools/verify-assets.ps1` 让零外部素材在本地可判定，`client/build.ps1` 是唯一构建入口。渲染（除 URP 管线资产的构建前置）、玩法与网络代码不在本份范围。

## 2. 入口条件

**入口条件**：HANDOFF-C00

跨链前置：无（客户端链第一份，只依赖 v2 起点 HANDOFF-C00）。

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 客户端工程根与说明文件就位 | `Test-Path client/README.md` | `True` |
| 2 | 引擎、协议、确定性三份 ADR 在库 | `Test-Path docs/00-共识/ADR/ADR-008-服务端语言与客户端引擎变更.md, docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md, docs/00-共识/ADR/ADR-010-跨语言确定性与对拍.md` | 三行 `True` |
| 3 | 团结引擎 Hub（Tuanjie Cowork）已安装 | `Test-Path 'D:\tools\unity\Tuanjie Cowork\hub\tuanjie.exe'` | `True` |
| 4 | 团结引擎编辑器 `2022.3.62t16` 已安装 | `Test-Path 'D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe'` 与 `(Get-Item 'D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe').VersionInfo.ProductVersion` | `True` 与 `2022.3.62t16_ab17684acf1d`（下划线前即 `ProjectVersion.txt` 首行的 `2022.3.62t16`） |
| 5 | 许可已激活，编辑器批处理能起来 | `& 'D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe' -batchmode -quit -nographics -logFile -` | 退出码 `0`；日志出现 `[Licensing::Module] Connected to LicensingClient` 与 `[Licensing::Client] Successfully updated license`，许可组为 `Unity Personal`（ULF）——本机实测日志**没有** `LICENSE SYSTEM` 字样 |
| 6 | 生成目录已忽略且本机 PowerShell 可用 | `Select-String -Path client/.gitignore -Pattern '^\[Ll\]ibrary/','^\[Tt\]emp/','^\[Bb\]uild'` 与 `$PSVersionTable.PSVersion.Major` | 三个模式各有命中（`[Ll]ibrary/`、`[Tt]emp/`、`[Bb]uild/` 与 `[Bb]uilds/`）；版本号不小于 `5` |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `client/ProjectSettings/ProjectVersion.txt` | 【新建】 | 以编辑器首次打开工程**实际生成的两行**为准（本份不硬编码 changeset） |
| `client/Packages/manifest.json` | 【新建】 | 依赖白名单：编辑器随附 URP 与官方基础包；非白名单项（如 `cn.tuanjie.*`）一律删除，白名单见 `tools/check-assets.mjs` |
| `client/Packages/packages-lock.json` | 【新建】 | 编辑器解析出的依赖锁（含 URP 的间接依赖），纯文本入库 |
| `client/Assets/Scripts/{Core,Sim,Net,View,UI,Audio}/Ac.<层>.asmdef` | 【新建】 | 六个运行时程序集定义与单向引用 |
| `client/Assets/Editor/Ac.Editor.asmdef`、`client/Assets/Editor/BuildEntry.cs`、`client/Assets/Editor/RenderPipelineSetup.cs` | 【新建】 | 编辑器专用程序集、`-executeMethod` 构建入口、URP 管线资产引导（幂等） |
| `client/Assets/Tests/Ac.Tests.asmdef`、`client/Assets/Tests/SuiteRegistry.cs`、`client/Assets/Tests/ContractSuite.cs` | 【新建】 | 编辑器专用分组自检程序集（不引 NUnit 与 Unity Test Framework）、`RunAll` 入口与本份契约用例 |
| `client/Assets/Settings/UniversalRenderPipeline.asset`、`client/Assets/Settings/UniversalRenderer.asset` | 【新建】 | URP 管线与渲染器资产（URP 构建前置，纯文本 YAML，由 `Ac.Editor.RenderPipelineSetup.Run` 生成） |
| `client/Assets/UniversalRenderPipelineGlobalSettings.asset` | 【引擎生成】 | URP 全局设置：编辑器随 URP 包生成，`GraphicsSettings` 的 `m_SRPDefaultSettings` 指向它，原样纯文本入库 |
| `client/tools/verify-assets.ps1` | 【新建】 | 零素材静态校验：扩展名黑名单与 NUL 嗅探 |
| `client/build.ps1` | 【新建】 | 唯一构建入口：批处理加 `-executeMethod` |
| `client/ProjectSettings/EditorSettings.asset`、`client/ProjectSettings/VersionControlSettings.asset` | 【修改】 | ForceText 序列化与 Visible Meta Files |
| `client/ProjectSettings/ProjectSettings.asset`、`client/ProjectSettings/GraphicsSettings.asset` | 【修改】 | §5.1 的产物标识（`productName`/`companyName`/`bundleVersion`）与 URP 默认管线指派 |
| `client/ProjectSettings/` 其余引擎生成资产（22 份 `.asset`/`.json`） | 【引擎生成】 | 引擎与 URP 包生成的默认设置，纯文本入库；本表只逐条点名被本份改写的几份 |
| `client/README.md` | 【修改】 | 写入 §6 的六条命令、§5.3 的目录约定与生成目录不入库规则 |

> 状态列：【新建】= 本份新写的文件；【修改】= 先由编辑器生成、再由本份改写内容；【引擎生成】= 引擎生成后原样入库。

## 4. 任务清单

- [ ] 1. 固化引擎版本：让编辑器首次打开 `client/` 时**实际生成** `client/ProjectSettings/ProjectVersion.txt`（本机实测为 3 行：`m_EditorVersion`、`m_EditorVersionWithRevision`、`m_TuanjieEditorVersion`），再核对该文件与 `(Get-Item $env:AC_UNITY).VersionInfo.ProductVersion` 一致（本机实测该值是 `2022.3.62t16_ab17684acf1d`：下划线前是版本号、括号外是 changeset）。
- [ ] 2. 安装并激活编辑器：用团结引擎 Hub（Tuanjie Cowork，`D:\tools\unity\Tuanjie Cowork\hub\tuanjie.exe`）安装编辑器 `2022.3.62t16`，登录激活许可，再用 `& $env:AC_UNITY -batchmode -quit -nographics -createProject client -logFile client\Logs\create-project.log` 在 `client/` 原地生成 `ProjectSettings/` 与 `Packages/manifest.json`（本机实测：`-projectPath` 打不开尚无 `ProjectSettings/` 的目录，报 `Couldn't set project path to ...` 并退出码 1）。
- [ ] 3. 收窄依赖：改 `client/Packages/manifest.json`，只保留 `com.unity.render-pipelines.universal`（编辑器随附，**版本逐字写 `14.2.0-t1`**）、`com.unity.ugui`、`com.unity.modules.*`；删掉非白名单项（如 `cn.tuanjie.*`），白名单见 `tools/check-assets.mjs`。编辑器 `-createProject` 实测只生成 `com.unity.modules.*`，URP 与 uGUI 需手工补入；URP 的间接依赖（`render-pipelines.core`/`shadergraph`/`universal-config`/`burst`/`mathematics`）**只能出现在 `packages-lock.json`**，写进 manifest 会触发白名单门禁失败。
- [ ] 4. 建立六层程序集：写 `client/Assets/Scripts/{Core,Sim,Net,View,UI,Audio}/Ac.<层>.asmdef`，`references` 严格按 §5.2 表设置。
- [ ] 5. 建立编辑器与测试程序集：写 `client/Assets/Editor/Ac.Editor.asmdef`、`client/Assets/Editor/BuildEntry.cs`（实现 `BuildWindows64()` 与 `-output` 参数）、`client/Assets/Editor/RenderPipelineSetup.cs`（URP 管线资产引导与指派）、`client/Assets/Tests/Ac.Tests.asmdef`、`client/Assets/Tests/SuiteRegistry.cs` 与 `ContractSuite.cs`（`c01.*` 契约用例）。
- [ ] 6. 写素材校验脚本 `client/tools/verify-assets.ps1`：扫描 `client/Assets` 与 `client/Packages` 并按 §5.5 判定，命中时每行打印 `BANNED <相对路径>` 并 `exit 1`。
- [ ] 7. 写构建脚本 `client/build.ps1`：拼装 `-batchmode -quit -nographics -projectPath client -executeMethod Ac.Editor.BuildEntry.BuildWindows64 -logFile -`，把 `-Target` 与 `-Output` 透传给 `BuildEntry`；成功末行 `Build succeeded`、退出码 `0`（构建失败 `1`、编辑器缺失 `2`），引擎日志落 `client/Logs/build.log`。
- [ ] 8. 锁定文本序列化：把 `ProjectSettings/EditorSettings.asset` 的 `m_SerializationMode` 置 `2`，`ProjectSettings/VersionControlSettings.asset` 的 `m_Mode` 置 `Visible Meta Files`。
- [ ] 9. 更新 `client/README.md`：写入 §6 的六条命令、§5.3 的目录约定与生成目录不入库规则。

## 5. 冻结契约

### 5.1 引擎与包
| 项 | 冻结值 |
|---|---|
| 引擎 | 团结引擎（Tuanjie，Unity 2022.3 线）；编辑器 `2022.3.62t16` |
| Hub | Tuanjie Cowork：`D:\tools\unity\Tuanjie Cowork\hub\tuanjie.exe` |
| 编辑器路径 | `D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe`；若实际安装路径或补丁号不同，执行时回写本表 |
| 编辑器发现 | 环境变量 `AC_UNITY` 指向上述 `Tuanjie.exe`；本链所有批处理命令只写 `& $env:AC_UNITY`，不写第二份路径 |
| 渲染 | URP 随编辑器随附（离线内置，本机实测 `14.2.0-t1`）：manifest 里逐字写该随附版本，`packages-lock.json` 的 `source` 必须是 `builtin`；构建前置：`GraphicsSettings` 的默认管线必须指向 `Assets/Settings/UniversalRenderPipeline.asset`（URP 14 的 `ShaderBuildPreprocessor` 收集不到 URP 资产时抛 `BuildFailedException` 顶掉出包），由 `Ac.Editor.RenderPipelineSetup.Run` 幂等生成并指派 |
| UI | `com.unity.ugui`（官方 uGUI，文本用系统字体） |
| 其余依赖 | 仅 `com.unity.modules.*`；任何其它包一律禁止（白名单见 [tools/check-assets.mjs](../../../tools/check-assets.mjs) 的 `UNITY_ALLOWED`） |
| 脚本后端 | `Mono2x`（发布计划再切 IL2CPP） |
| 构建目标 | `BuildTarget.StandaloneWindows64`、`BuildOptions.None` |
| 引导场景 | C01 尚无场景资产：`BuildEntry` 在 `EditorBuildSettings` 没有启用场景时临时生成 `Assets/__BuildEntryTemp/Boot.unity` 充当引导场景，构建结束（含失败）即 `AssetDatabase.DeleteAsset` 删除；下一次构建开场先清理可能的残留。该临时场景不进产物、不入库 |
| 产物路径与标识 | `Build/Windows64/angry-chen.exe`（相对 `client/`，仓库根下即 `client/Build/Windows64/angry-chen.exe`，不入库）；`productName = angry-chen`、`companyName = AngryChen`、`bundleVersion = 0.1.0` |
| 许可与模块 | 许可由 Tuanjie Cowork Hub 管理（登录后激活个人许可，离线机器走 Hub 的离线激活）；构建 Windows x64 只用编辑器内置模块，不额外装平台支持包 |
| 序列化 | `m_SerializationMode: 2`（ForceText）；`m_Mode: Visible Meta Files`；`*.meta` 入库 |
| 语言 | 团结引擎（2022.3 线）默认 C#（.NET Standard 2.1）；禁止 `unsafe` |

### 5.2 程序集与单向依赖
| 程序集 | 目录 | 允许引用 |
|---|---|---|
| `Ac.Core` | `client/Assets/Scripts/Core/` | 无（只用 UnityEngine） |
| `Ac.Sim` | `client/Assets/Scripts/Sim/` | `Ac.Core` |
| `Ac.Net` | `client/Assets/Scripts/Net/` | `Ac.Core`、`Ac.Sim` |
| `Ac.View` | `client/Assets/Scripts/View/` | `Ac.Core`、`Ac.Sim`、`Ac.Net` |
| `Ac.UI` | `client/Assets/Scripts/UI/` | `Ac.Core`、`Ac.Sim`、`Ac.Net`、`Ac.View` |
| `Ac.Audio` | `client/Assets/Scripts/Audio/` | `Ac.Core` |
| `Ac.Editor` | `client/Assets/Editor/` | 六个运行时程序集加 URP 运行时程序集（`Unity.RenderPipelines.Universal.Runtime`、`Unity.RenderPipelines.Core.Runtime`，仅用于管线资产引导），平台仅 `Editor` |
| `Ac.Tests` | `client/Assets/Tests/` | 六个运行时程序集，平台仅 `Editor` |

依赖方向冻结为 `View → Net → Sim → Core`；任何反向或循环引用都按编译级失败处理。

### 5.3 目录约定与入口名
| 路径 | 用途 |
|---|---|
| `Core/`、`Sim/` | 基础设施（断言、受限 JSON）；客户端仿真（确定性数学、量化、编解码、局部步进） |
| `Net/`、`View/` | UDP 传输与会话；相机、插值、实体视图 |
| `UI/`、`Audio/` | 面板与 HUD；程序化合成与混音 |
| `Assets/Editor/`、`Assets/Tests/` | 编辑器工具与构建入口；编辑器专用自检，不进包体 |

| 入口 | 方法全名 | 用途 |
|---|---|---|
| 编辑器 | `$env:AC_UNITY` | 唯一编辑器入口变量，指向 §5.1 的 `Tuanjie.exe`；所有批处理命令统一用它 |
| 构建 | `Ac.Editor.BuildEntry.BuildWindows64` | `client/build.ps1` 的 `-executeMethod` 目标 |
| 渲染前置 | `Ac.Editor.RenderPipelineSetup.Run` | 幂等生成并指派 URP 管线资产；`BuildEntry` 出包前调用其 `Ensure()` |
| 核心自检 | `Ac.Core.SelfTest.Run` | 输出格式在本份冻结（`PASS`/`FAIL` 行 + 末行 `SELFTEST OK`），实现在下一份计划落地；C02 §5.7 在末行补 `cases=<n>` 计数并让两个自检入口共用同一实现 |
| 分组自检 | `Ac.Tests.SuiteRegistry.RunAll` | 运行 `client/Assets/Tests/` 下注册的用例分组 |

### 5.4 命名规则（承继 [工程约定](../../00-共识/工程约定.md) §11）
- 类型、方法、属性 `PascalCase`；私有字段 `_camelCase`；局部变量 `camelCase`；常量 `PascalCase`；文件与主类型同名。
- 命名空间为 `Ac.Core`、`Ac.Sim`、`Ac.Net`、`Ac.View`、`Ac.UI`、`Ac.Audio`、`Ac.Editor`、`Ac.Tests`；协议字段名与 [ADR-009](../../00-共识/ADR/ADR-009-UDP传输与协议重构.md) 字段表同名。

### 5.5 零素材校验（`client/tools/verify-assets.ps1`）
| 项 | 冻结值 |
|---|---|
| 扫描根与排除 | `client/Assets`、`client/Packages`；排除 `Library`、`Temp`、`obj`、`Logs`、`Build`、`Builds`、`UserSettings` |
| 黑名单扩展名 | `.png .jpg .jpeg .gif .bmp .tga .psd .webp .ico .svg .wav .mp3 .ogg .m4a .aiff .flac .ttf .otf .woff .woff2 .fbx .obj .gltf .glb .blend .dae .mp4 .mov .webm .unitypackage .assetbundle .dll .so .dylib .zip .7z .rar`（与 [tools/check-assets.mjs](../../../tools/check-assets.mjs) 的 `BANNED_ASSET` 逐字相同，逐项对齐，不得增删） |
| 二道检查 | 文件前 8192 字节含 NUL 字节即判为二进制素材 |
| 输出与退出码 | 通过打印 `OK：client 零外部素材` 并 `exit 0`；命中每行打印 `BANNED <相对路径>` 并 `exit 1` |

### 5.6 项目版本文件
`client/ProjectSettings/ProjectVersion.txt` 以编辑器首次打开工程时**实际生成的内容为准**，本份不硬编码 changeset：第一行是 `m_EditorVersion: 2022.3.62t16`，第二行是 `m_EditorVersionWithRevision: 2022.3.62t16 (<changeset>)`；`<changeset>` 必须与编辑器的 `ProductVersion` 一致（`(Get-Item $env:AC_UNITY).VersionInfo.ProductVersion` 实测为 `2022.3.62t16_ab17684acf1d`，下划线后即 changeset），不得手写猜测。本机团结引擎 `2022.3.62t16` 实测还会写入**第三行** `m_TuanjieEditorVersion: 1.10.4`：该行由引擎维护，原样入库，不得删除。

## 6. 验证

1. 素材门：`powershell -NoProfile -File client/tools/verify-assets.ps1` → 期望 `OK：client 零外部素材`；失败意味着有人把图片、音频、模型或字体放进了客户端工程，仓库门禁 `node tools/check-assets.mjs` 会同步判死。
2. 版本、序列化与依赖白名单：`Get-Content client/ProjectSettings/ProjectVersion.txt` 首行为 `m_EditorVersion: 2022.3.62t16`，第二行的 changeset 与 `(Get-Item $env:AC_UNITY).VersionInfo.ProductVersion`（实测 `2022.3.62t16_ab17684acf1d`）一致（本机实测该文件为 3 行，第三行 `m_TuanjieEditorVersion: 1.10.4` 由引擎维护）；`Select-String -Path client/ProjectSettings/EditorSettings.asset -Pattern "m_SerializationMode: 2"` 命中；`client/Packages/manifest.json` 的 `dependencies` 全部命中 `tools/check-assets.mjs` 的 `UNITY_ALLOWED` 白名单，命中白名单外的项（如 `cn.tuanjie.*`）即**删掉该项**后重跑；失败意味着补丁号被换掉、写回了二进制序列化，或依赖白名单被破坏，工程文件无法评审。
3. 程序集图：`Get-ChildItem client/Assets -Recurse -Filter *.asmdef | ForEach-Object { $_.Name; (Get-Content -Raw $_ | ConvertFrom-Json).references }` → 每份 `references` 是 §5.2 允许集合的子集；失败意味着出现反向或循环依赖，分层失效。
4. 构建：`powershell -NoProfile -File client/build.ps1 -Target Windows64` → 退出码 `0`（`1` = 构建失败、`2` = 编辑器缺失），输出末行 `Build succeeded`，且在 `client/` 下 `Test-Path Build/Windows64/angry-chen.exe` 为 `True`（产物相对 `client/`）；失败意味着 `BuildEntry` 未实现、URP 管线资产缺失、场景缺失或程序集编译错误。
5. 生成目录不入库：`git status --porcelain -uall client` → 逐个列出 `client/` 下所有未忽略文件，全部属于 §3（`ProjectSettings/*.asset`、`Packages/*`、`Assets/**` 及其 `.meta`、`tools/verify-assets.ps1`、`build.ps1`、`README.md`）；出现 `Library/`、`Temp/`、`Logs/`、`Build/`、`UserSettings/` 下任何文件即失败。
6. 仓库质量门：`node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿（含 `node tools/check-docs.mjs`、`node tools/check-assets.mjs`）。

## 7. DoD（验收标准）

- [ ] §6 的 6 条验证全部通过，输出与期望逐字匹配。
- [ ] `ProjectVersion.txt` 由编辑器首次打开工程实际生成，版本为 `2022.3.62t16`，changeset 与编辑器 `ProductVersion` 一致（本机实测 3 行：`m_EditorVersion: 2022.3.62t16`、`m_EditorVersionWithRevision: 2022.3.62t16 (ab17684acf1d)`、`m_TuanjieEditorVersion: 1.10.4`）。
- [ ] `client/Packages/manifest.json` 的依赖集合是 `com.unity.render-pipelines.universal`（编辑器随附）、`com.unity.ugui`、`com.unity.modules.*` 的子集，且无 `cn.tuanjie.*` 等白名单外项。
- [ ] 六个运行时程序集加 `Ac.Editor`、`Ac.Tests` 齐全，依赖方向与 §5.2 逐条一致。
- [ ] 往 `client/Assets/tmp.png` 放占位文件后，`verify-assets.ps1` 退出码为 `1` 并打印 `BANNED client/Assets/tmp.png`；验收后删除该占位文件。
- [ ] `client/build.ps1 -Target Windows64` 退出码 `0`，产物 `Build/Windows64/angry-chen.exe` 存在。
- [ ] `client/README.md` 含打开、构建、自检三条命令，与 §6 逐字一致。
- [ ] `node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| Tuanjie Cowork Hub 中取不到该补丁号 | 安装列表无 `2022.3.62t16` | 改钉团结引擎 2022.3 线内可用的最新补丁号，同步更新 `ProjectVersion.txt`、本文件 §5.1 与 `client/README.md` 三处 |
| 许可未激活 | 批处理日志无 `[Licensing::Client] Successfully updated license` 行，或退出码非 `0` | 在 Tuanjie Cowork Hub 登录并激活个人许可后重跑；无许可时构建与自检都不可执行 |
| URP 构建前置缺失（无管线资产） | 构建日志 `BuildFailedException: [URP] No URP Asset collected for the current build target` | 跑 `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Editor.RenderPipelineSetup.Run -logFile -` 生成并指派 `Assets/Settings/UniversalRenderPipeline.asset`，再重跑 §6 第 4 条；`BuildEntry` 出包前也会自动补建 |
| URP 补丁版本不匹配 | Package Manager 报版本约束不满足 | URP 只允许用编辑器随附（离线内置）的那一份：改回随附版本，并同步本文件 §5.1 与 `manifest.json` 两处 |
| 引擎大版本升级需迁移 URP 资产 | 换引擎线后材质或着色器报错、资产变粉 | 按 ADR-008 的已知代价处理：升级前冻结 `Packages/` 与 `ProjectSettings/`，逐包迁移 URP 资产，再重跑 §6 第 4 条构建 |
| 编辑器写回二进制序列化 | `m_SerializationMode` 不是 `2` | 在编辑器 Preferences 改回 ForceText 并重写该文件；否则工程 diff 不可读、评审与门禁失效 |

回滚目标：回到 HANDOFF-C00（`client/` 只有 `README.md` 与 `.gitignore`），删除 `Assets/`、`Packages/`、`ProjectSettings/`、`tools/`、`build.ps1`，并还原 `client/README.md`。

## 9. 移交物

**移交物 ID**：HANDOFF-C01

| 接口 | 位置 | 约定 |
|---|---|---|
| 构建入口 | `client/build.ps1` | 参数 `-Target Windows64`、`-Output <目录或 .exe 路径>`（缺省 `client/Build/Windows64`）；退出码 `0` = 出包成功、`1` = 构建失败、`2` = 编辑器缺失；产物 `Build/Windows64/angry-chen.exe`（相对 `client/`） |
| 引导场景 | `Assets/__BuildEntryTemp/Boot.unity`（临时） | 由 `Ac.Editor.BuildEntry.BuildWindows64()` 在无启用场景时生成并即时删除；C02 落地真实场景后不再使用 |
| 素材门 | `client/tools/verify-assets.ps1` | 无参数；退出码 `0` 代表零素材，退出码 `1` 代表命中黑名单 |
| 程序集 | `Ac.Core`、`Ac.Sim`、`Ac.Net`、`Ac.View`、`Ac.UI`、`Ac.Audio`、`Ac.Editor`、`Ac.Tests` | 新代码只能落在 §5.3 的目录内，依赖方向见 §5.2 |
| 自检入口名 | `Ac.Core.SelfTest.Run`、`Ac.Tests.SuiteRegistry.RunAll` | 名字与输出格式在本份冻结 |
| 序列化 | ForceText 与 Visible Meta Files | 所有 `.unity`、`.asset`、`.meta` 为纯文本 |
| 渲染前置 | `Ac.Editor.RenderPipelineSetup.Run`（及 `Ensure()`） | 幂等：已指派则原样返回，否则生成 `Assets/Settings/` 下两个资产并写入 `GraphicsSettings` 默认管线 |

已验证能力清单：工程可被批处理打开；两个 Editor 程序集（`Ac.Editor`、`Ac.Tests`）编译通过，六个运行时 asmdef 已注册（本份无脚本，引擎按 `no scripts associated with it` 跳过编译，C02 起落地代码后自动纳入编译）；Windows x64 构建产出可执行文件；零素材校验脚本可判定；依赖方向可静态检查（`Ac.Tests.SuiteRegistry.RunAll` 的 `c01.*` 用例）。
