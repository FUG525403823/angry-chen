# C01 Unity 工程基线与构建
> 里程碑：MC01 ｜ 预估：1 人日 ｜ 上游移交物：HANDOFF-C00

## 1. 目标

本份结束后，`client/` 是一个能被 Unity 6000.0.32f1 打开、编译、并在一条命令下产出 Windows x64 可执行文件的工程：六层程序集边界单向，工程文件全部纯文本入库，`client/tools/verify-assets.ps1` 让零外部素材在本地可判定，`client/build.ps1` 是唯一构建入口。渲染、玩法与网络代码不在本份范围。

## 2. 入口条件

**入口条件**：HANDOFF-C00

跨链前置：无（客户端链第一份，只依赖 v2 起点 HANDOFF-C00）。

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 客户端工程根与说明文件就位 | `Test-Path client/README.md` | `True` |
| 2 | 引擎、协议、确定性三份 ADR 在库 | `Test-Path docs/00-共识/ADR/ADR-008-服务端语言与客户端引擎变更.md, docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md, docs/00-共识/ADR/ADR-010-跨语言确定性与对拍.md` | 三行 `True` |
| 3 | Unity Hub 已安装 | `Test-Path "$env:ProgramFiles\Unity Hub\Unity Hub.exe"` | `True`；否则先执行 `winget install Unity.UnityHub` |
| 4 | Unity Editor 6000.0.32f1 已安装 | `(Get-Item "$env:ProgramFiles\Unity\Hub\Editor\6000.0.32f1\Editor\Unity.exe").VersionInfo.ProductVersion` | `6000.0.32f1` |
| 5 | 编辑器许可已激活，可跑批处理 | `& "$env:ProgramFiles\Unity\Hub\Editor\6000.0.32f1\Editor\Unity.exe" -batchmode -quit -nographics -logFile -` | 日志含 `LICENSE SYSTEM`，退出码 `0` |
| 6 | 生成目录已忽略且 pwsh 可用 | `Select-String -Path client/.gitignore -Pattern "^Library/","^Temp/","^Build"` 与 `$PSVersionTable.PSVersion.Major` | 三处命中；版本号不小于 `7` |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `client/ProjectSettings/ProjectVersion.txt` | 【新建】 | 锁定编辑器补丁号与 changeset |
| `client/Packages/manifest.json` | 【新建】 | 依赖白名单：URP 17.0.4 与官方基础包 |
| `client/Assets/Scripts/{Core,Sim,Net,View,UI,Audio}/Ac.<层>.asmdef` | 【新建】 | 六个运行时程序集定义与单向引用 |
| `client/Assets/Editor/Ac.Editor.asmdef`、`client/Assets/Editor/BuildEntry.cs` | 【新建】 | 编辑器专用程序集与 `-executeMethod` 构建入口 |
| `client/Tests/Ac.Tests.asmdef` | 【新建】 | 编辑器专用分组自检程序集，不引 NUnit 与 Unity Test Framework |
| `client/tools/verify-assets.ps1` | 【新建】 | 零素材静态校验：扩展名黑名单与 NUL 嗅探 |
| `client/build.ps1` | 【新建】 | 唯一构建入口：批处理加 `-executeMethod` |
| `client/ProjectSettings/EditorSettings.asset`、`client/ProjectSettings/VersionControlSettings.asset` | 【修改】 | ForceText 序列化与 Visible Meta Files |
| `client/README.md` | 【修改】 | 写入打开、构建、自检命令与目录约定 |

## 4. 任务清单

1. 固化引擎版本：写 `client/ProjectSettings/ProjectVersion.txt`，两行分别为 `m_EditorVersion: 6000.0.32f1` 与 `m_EditorVersionWithRevision: 6000.0.32f1 (<changeset>)`。
2. 安装并激活编辑器：用 Unity Hub 安装 `6000.0.32f1`，登录激活许可，记录 changeset，让编辑器首次打开 `client/` 生成 `ProjectSettings/` 与 `Packages/manifest.json`。
3. 收窄依赖：改 `client/Packages/manifest.json`，只保留 `com.unity.render-pipelines.universal`、`com.unity.ugui`、`com.unity.modules.*`。
4. 建立六层程序集：写 `client/Assets/Scripts/{Core,Sim,Net,View,UI,Audio}/Ac.<层>.asmdef`，`references` 严格按 §5.2 表设置。
5. 建立编辑器与测试程序集：写 `client/Assets/Editor/Ac.Editor.asmdef`、`client/Assets/Editor/BuildEntry.cs`（实现 `BuildWindows64()` 与 `-output` 参数）、`client/Tests/Ac.Tests.asmdef`。
6. 写素材校验脚本 `client/tools/verify-assets.ps1`：扫描 `client/Assets` 与 `client/Packages` 并按 §5.5 判定，命中时每行打印 `BANNED <相对路径>` 并 `exit 1`。
7. 写构建脚本 `client/build.ps1`：拼装 `-batchmode -quit -nographics -projectPath client -executeMethod Ac.Editor.BuildEntry.BuildWindows64 -logFile -`，把 `-Target` 与 `-Output` 透传给 `BuildEntry`。
8. 锁定文本序列化：把 `ProjectSettings/EditorSettings.asset` 的 `m_SerializationMode` 置 `2`，`ProjectSettings/VersionControlSettings.asset` 的 `m_Mode` 置 `Visible Meta Files`。
9. 更新 `client/README.md`：写入 §6 的六条命令、§5.3 的目录约定与生成目录不入库规则。

## 5. 冻结契约

### 5.1 引擎与包
| 项 | 冻结值 |
|---|---|
| 编辑器 | `6000.0.32f1`（Unity 6 LTS 线） |
| 渲染 | `com.unity.render-pipelines.universal@17.0.4` |
| UI | `com.unity.ugui`（官方 uGUI，文本用系统字体） |
| 其余依赖 | 仅 `com.unity.modules.*`；任何其它包一律禁止 |
| 脚本后端 | `Mono2x`（发布计划再切 IL2CPP） |
| 构建目标 | `BuildTarget.StandaloneWindows64`、`BuildOptions.None` |
| 产物路径与标识 | `Build/Windows64/angry-chen.exe`；`productName = angry-chen`、`bundleVersion = 0.1.0` |
| 序列化 | `m_SerializationMode: 2`（ForceText）；`m_Mode: Visible Meta Files`；`*.meta` 入库 |
| 语言 | Unity 6 默认 C#（.NET Standard 2.1）；禁止 `unsafe` |

### 5.2 程序集与单向依赖
| 程序集 | 目录 | 允许引用 |
|---|---|---|
| `Ac.Core` | `client/Assets/Scripts/Core/` | 无（只用 UnityEngine） |
| `Ac.Sim` | `client/Assets/Scripts/Sim/` | `Ac.Core` |
| `Ac.Net` | `client/Assets/Scripts/Net/` | `Ac.Core`、`Ac.Sim` |
| `Ac.View` | `client/Assets/Scripts/View/` | `Ac.Core`、`Ac.Sim`、`Ac.Net` |
| `Ac.UI` | `client/Assets/Scripts/UI/` | `Ac.Core`、`Ac.Sim`、`Ac.Net`、`Ac.View` |
| `Ac.Audio` | `client/Assets/Scripts/Audio/` | `Ac.Core` |
| `Ac.Editor` | `client/Assets/Editor/` | 六个运行时程序集，平台仅 `Editor` |
| `Ac.Tests` | `client/Tests/` | 六个运行时程序集，平台仅 `Editor` |

依赖方向冻结为 `View → Net → Sim → Core`；任何反向或循环引用都按编译级失败处理。

### 5.3 目录约定与入口名
| 路径 | 用途 |
|---|---|
| `Core/`、`Sim/` | 基础设施（断言、受限 JSON）；客户端仿真（确定性数学、量化、编解码、局部步进） |
| `Net/`、`View/` | UDP 传输与会话；相机、插值、实体视图 |
| `UI/`、`Audio/` | 面板与 HUD；程序化合成与混音 |
| `Assets/Editor/`、`Tests/` | 编辑器工具与构建入口；编辑器专用自检，不进包体 |

| 入口 | 方法全名 | 用途 |
|---|---|---|
| 构建 | `Ac.Editor.BuildEntry.BuildWindows64` | `client/build.ps1` 的 `-executeMethod` 目标 |
| 核心自检 | `Ac.Core.SelfTest.Run` | 输出格式在本份冻结，实现在下一份计划落地 |
| 分组自检 | `Ac.Tests.SuiteRegistry.RunAll` | 运行 `client/Tests/` 下注册的用例分组 |

### 5.4 命名规则（承继 [工程约定](../../00-共识/工程约定.md) §11）
- 类型、方法、属性 `PascalCase`；私有字段 `_camelCase`；局部变量 `camelCase`；常量 `PascalCase`；文件与主类型同名。
- 命名空间为 `Ac.Core`、`Ac.Sim`、`Ac.Net`、`Ac.View`、`Ac.UI`、`Ac.Audio`、`Ac.Editor`、`Ac.Tests`；协议字段名与 [ADR-009](../../00-共识/ADR/ADR-009-UDP传输与协议重构.md) 字段表同名。

### 5.5 零素材校验（`client/tools/verify-assets.ps1`）
| 项 | 冻结值 |
|---|---|
| 扫描根与排除 | `client/Assets`、`client/Packages`；排除 `Library`、`Temp`、`obj`、`Logs`、`Build`、`Builds`、`UserSettings` |
| 黑名单扩展名 | `.png .jpg .jpeg .gif .webp .avif .bmp .ico .tga .tiff .svg .mp3 .wav .ogg .oga .m4a .aac .flac .opus .glb .gltf .fbx .obj .dae .stl .ply .ktx2 .hdr .exr .dds .basis .ttf .otf .woff .woff2 .eot .mp4 .webm .mov .avi`（与 [tools/check-assets.mjs](../../../tools/check-assets.mjs) 的 `BANNED_EXTENSIONS` 逐字相同） |
| 二道检查 | 文件前 8192 字节含 NUL 字节即判为二进制素材 |
| 输出与退出码 | 通过打印 `OK：client 零外部素材` 并 `exit 0`；命中每行打印 `BANNED <相对路径>` 并 `exit 1` |

### 5.6 项目版本文件
`client/ProjectSettings/ProjectVersion.txt` 恰两行：`m_EditorVersion: 6000.0.32f1` 与 `m_EditorVersionWithRevision: 6000.0.32f1 (<changeset>)`；`<changeset>` 必须逐字取自 `Unity.exe -version` 输出中的 12 位十六进制值，不得手写猜测。

## 6. 验证

1. 素材门：`pwsh -NoProfile -File client/tools/verify-assets.ps1` → 期望 `OK：client 零外部素材`；失败意味着有人把图片、音频、模型或字体放进了客户端工程，仓库门禁 `node tools/check-assets.mjs` 会同步判死。
2. 版本与序列化：`Get-Content client/ProjectSettings/ProjectVersion.txt` 首行为 `m_EditorVersion: 6000.0.32f1`；`Select-String -Path client/ProjectSettings/EditorSettings.asset -Pattern "m_SerializationMode: 2"` 命中；失败意味着补丁号被换掉或写回了二进制序列化，工程文件无法评审。
3. 程序集图：`Get-ChildItem client/Assets,client/Tests -Recurse -Filter *.asmdef | ForEach-Object { $_.Name; (Get-Content -Raw $_ | ConvertFrom-Json).references }` → 每份 `references` 是 §5.2 允许集合的子集；失败意味着出现反向或循环依赖，分层失效。
4. 构建：`pwsh -NoProfile -File client/build.ps1 -Target Windows64` → 期望日志末 `Build succeeded` 且 `Test-Path Build/Windows64/angry-chen.exe` 为 `True`；失败意味着 `BuildEntry` 未实现、场景缺失或程序集编译错误。
5. 生成目录不入库：`git status --porcelain client` → 只出现 §3 列出的文件；失败意味着 `Library/`、`Logs/`、`Build/` 等被提交。
6. 仓库质量门：`node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿（含 `node tools/check-docs.mjs`、`node tools/check-assets.mjs`）。

## 7. DoD（验收标准）

- [ ] §6 的 6 条验证全部通过，输出与期望逐字匹配。
- [ ] `ProjectVersion.txt` 恰两行，版本为 `6000.0.32f1`，changeset 与编辑器 `-version` 输出一致。
- [ ] `client/Packages/manifest.json` 的依赖集合是 `com.unity.render-pipelines.universal`、`com.unity.ugui`、`com.unity.modules.*` 的子集。
- [ ] 六个运行时程序集加 `Ac.Editor`、`Ac.Tests` 齐全，依赖方向与 §5.2 逐条一致。
- [ ] 往 `client/Assets/tmp.png` 放占位文件后，`verify-assets.ps1` 退出码为 `1` 并打印 `BANNED client/Assets/tmp.png`；验收后删除该占位文件。
- [ ] `client/build.ps1 -Target Windows64` 退出码 `0`，产物 `Build/Windows64/angry-chen.exe` 存在。
- [ ] `client/README.md` 含打开、构建、自检三条命令，与 §6 逐字一致。
- [ ] `node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| Hub 中取不到该补丁号 | 安装列表无 `6000.0.32f1` | 改钉同 LTS 线内可用的最新 `6000.0.x`，同步更新 `ProjectVersion.txt`、本文件 §5.1 与 `client/README.md` 三处 |
| 许可未激活 | 批处理日志出现 `No valid Unity Editor license` | 在 Hub 登录并激活个人许可后重跑；无许可时构建与自检都不可执行 |
| URP 补丁版本不匹配 | Package Manager 报版本约束不满足 | 回落到编辑器随附的 URP 补丁版本，并同步本文件 §5.1 与 `manifest.json` 两处 |
| 编辑器写回二进制序列化 | `m_SerializationMode` 不是 `2` | 在编辑器 Preferences 改回 ForceText 并重写该文件；否则工程 diff 不可读、评审与门禁失效 |

回滚目标：回到 HANDOFF-C00（`client/` 只有 `README.md` 与 `.gitignore`），删除 `Assets/`、`Packages/`、`ProjectSettings/`、`tools/`、`build.ps1`，并还原 `client/README.md`。

## 9. 移交物

**移交物 ID**：HANDOFF-C01

| 接口 | 位置 | 约定 |
|---|---|---|
| 构建入口 | `client/build.ps1` | 参数 `-Target Windows64`；退出码 `0` 代表出包成功，产物 `Build/Windows64/angry-chen.exe` |
| 素材门 | `client/tools/verify-assets.ps1` | 无参数；退出码 `0` 代表零素材，退出码 `1` 代表命中黑名单 |
| 程序集 | `Ac.Core`、`Ac.Sim`、`Ac.Net`、`Ac.View`、`Ac.UI`、`Ac.Audio`、`Ac.Editor`、`Ac.Tests` | 新代码只能落在 §5.3 的目录内，依赖方向见 §5.2 |
| 自检入口名 | `Ac.Core.SelfTest.Run`、`Ac.Tests.SuiteRegistry.RunAll` | 名字与输出格式在本份冻结 |
| 序列化 | ForceText 与 Visible Meta Files | 所有 `.unity`、`.asset`、`.meta` 为纯文本 |

已验证能力清单：工程可被批处理打开；八个程序集编译通过；Windows x64 构建产出可执行文件；零素材校验脚本可判定；依赖方向可静态检查。
