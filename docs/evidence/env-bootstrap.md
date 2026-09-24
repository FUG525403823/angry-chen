# 环境装配证据（团结引擎客户端基线 + 双端工具链）

> 记录 2026-09-24 本机（DESKTOP-OCL09JJ，Windows 11 10.0.26200）的实测命令与输出。
> 全部命令按 Windows PowerShell 5.1 语法执行；带路径的引号与反斜杠照抄可复跑。
> 本文件只记录**已实测**的结果，不写期望值。

## 1. 工具链（两端首份计划的入口条件）

| 项 | 命令 | 实测输出 |
|---|---|---|
| mingw g++ | `g++ --version` | `g++.exe (x86_64-win32-seh-rev1, Built by MinGW-Builds project) 15.2.0`；`g++ -dumpmachine` = `x86_64-w64-mingw32` |
| vswhere | `Test-Path "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"` | `True` |
| VS | `& $vswhere -latest -products * -property installationPath` | `D:\tools\Visualstudio\software`；`catalog_productDisplayVersion` = `18.4.2` |
| MSVC 工具集 | `Get-ChildItem <VS>\VC\Tools\MSVC` | `14.29.30133`、`14.44.35207`、`14.50.35717`；`<VS>\VC\Auxiliary\Build\vcvars64.bat` 存在 |
| CMake | `& '<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --version` | `cmake version 4.2.3-msvc3`（不在 PATH） |
| Ninja | `& '<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe' --version` | `1.12.1`（不在 PATH） |
| Windows SDK | `Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include"` | `10.0.26100.0` |
| PowerShell | `$PSVersionTable.PSVersion` | `5.1.26100.9444` |
| Node / git | `node --version` / `git --version` | `v24.14.1` / `2.54.0.windows.1` |

> 结论：**无需安装任何工具**；S01 的 g++ 兜底、vswhere、VS 自带 CMake/Ninja、Windows SDK 均已就位。

## 2. 编辑器与许可

| 项 | 命令 | 实测输出 |
|---|---|---|
| Hub | `Test-Path 'D:\tools\unity\Tuanjie Cowork\hub\tuanjie.exe'` | `True` |
| 编辑器 | `(Get-Item '<Editor>\Tuanjie.exe').VersionInfo` | `ProductVersion = 2022.3.62t16_ab17684acf1d`，`FileVersion = 2022.3.62.11212648` |
| 出包模块 | `Get-ChildItem <Editor>\Data\PlaybackEngines\windowsstandalonesupport\Variations` | 6 项且**全是 mono**：`win32_player_development_mono`、`win32_player_nondevelopment_mono`、`win64_player_development_mono`、`win64_player_development_mono_instantasset`、`win64_player_nondevelopment_mono`、`win64_player_nondevelopment_mono_instantasset`——**无 IL2CPP 变体** |
| 随附包 | `<Editor>\Data\Resources\PackageManager\BuiltInPackages` | 含 `com.unity.render-pipelines.universal`（`14.2.0-t1`）、`com.unity.render-pipelines.core`、`com.unity.shadergraph`、`com.unity.ugui`（`1.0.0`）与全部 `com.unity.modules.*` |
| 入口变量 | `[Environment]::SetEnvironmentVariable('AC_UNITY','<Editor>\Tuanjie.exe','User')` | workspace-write 下报 `SecurityException: Requested registry access is not allowed`；提升为完整访问后写入成功，回读得 `D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe` |
| 许可探测 | `& $env:AC_UNITY -batchmode -quit -nographics -logFile client\Logs\license-probe.log` | 退出码 `0`，耗时 11 s；日志含 `[Licensing::Module] Connected to LicensingClient`、`[Licensing::Client] Successfully updated license`、许可组 `{"productName":"Unity Personal","productType":"TuanjieEditor","licenseType":"ULF","expiration":1790566859000}`（换算到期 2026-09-28） |
| 无 `LICENSE SYSTEM` | `Get-ChildItem client\Logs\*.log \| Select-String -Pattern 'SYSTEM' -SimpleMatch` | 只有 `Input System module state changed to: Initialized.`；**没有** `LICENSE SYSTEM` 字样（C01 §2 第 5 行原文与本机实测不符，已改写） |

## 3. 工程骨架生成（C01 §4 任务 1–2）

| 项 | 命令 | 实测输出 |
|---|---|---|
| 直接打开空目录 | `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -logFile ...` | 退出码 `1`，日志末 `Couldn't set project path to: D:/projects/tmp/angry-chen/client`——尚无 `ProjectSettings/` 的目录不能被 `-projectPath` 打开 |
| 原地生成 | `& $env:AC_UNITY -batchmode -quit -nographics -createProject client -logFile client\Logs\create-project.log` | 退出码 `0`；生成 `Assets/`、`Library/`、`Logs/`、`Packages/`、`ProjectSettings/`、`UserSettings/`；`client/README.md` 与 `client/.gitignore` 的 SHA256 前后不变 |
| 版本文件 | `Get-Content client\ProjectSettings\ProjectVersion.txt` | 3 行：`m_EditorVersion: 2022.3.62t16`、`m_EditorVersionWithRevision: 2022.3.62t16 (ab17684acf1d)`、`m_TuanjieEditorVersion: 1.10.4` |
| 生成清单 | `Get-ChildItem client\ProjectSettings -File` | 23 个文件（`ProjectVersion.txt` + 22 个 `.asset`，含 `EditorSettings.asset`、`GraphicsSettings.asset`、`QualitySettings.asset`、`ProjectSettings.asset` 等） |
| `Assets/` | `Get-ChildItem client\Assets -Recurse -Force` | `UniversalRenderPipelineGlobalSettings.asset` + 其 `.meta`（URP 进 manifest 后由引擎生成） |
| 序列化 | `Select-String -Path client\ProjectSettings\EditorSettings.asset -Pattern 'm_SerializationMode'` | `m_SerializationMode: 2`（ForceText，无需改动） |
| 版本控制模式 | `Select-String -Path client\ProjectSettings\VersionControlSettings.asset -Pattern 'm_Mode'` | `m_Mode: Visible Meta Files` |
| 无工程上下文启动的副作用 | 许可探测与失败的空目录打开都在**当前工作目录（仓库根）**被当成工程 | 仓库根被写入 `Library/`（521 项）、`Logs/`、`Packages/`、`ProjectSettings/`（22 项）、`UserSettings/`；仓库根没有 `.gitignore`，因此它们全部出现在 `git status` 里，并让 `check-assets.mjs` 的 v1 形态检查（`packages/` 目录）误报。确认这五个目录 **0 个受控文件**（`git ls-files` 为空）后整体删除，`git status` 恢复只剩预期项 |
| 教训 | — | C 链的每条编辑器批处理命令都必须显式指向 `client/`（`-createProject client` 或 `-projectPath client`）；不得在无工程上下文时启动引擎 |

## 4. 依赖白名单收窄（C01 §4 任务 3）

| 项 | 实测 |
|---|---|
| `-createProject` 默认 manifest | 只有 31 项 `com.unity.modules.*`；**没有** URP、**没有** `com.unity.ugui`、**没有** `cn.tuanjie.*` |
| 收窄后 manifest | 34 项 = `com.unity.render-pipelines.universal: 14.2.0-t1` + `com.unity.ugui: 1.0.0` + 32 项 `com.unity.modules.*`（含 URP 依赖的 `com.unity.modules.tuanjiegi`） |
| 二次打开 | `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -logFile client\Logs\second-open.log` → 退出码 `0`，耗时 98 s，日志无 `error CS` / `Failed to resolve` / `Package Manager Error` |
| 解析锁 | `client/Packages/packages-lock.json` 共 41 个包、0 个未解析；`com.unity.render-pipelines.universal` = `14.2.0-t1`、`source: builtin`、`depth: 0`；间接依赖 `render-pipelines.core` / `shadergraph` / `universal-config` = `14.2.0-t1`（`builtin`），`mathematics` = `1.3.2`、`burst` = `1.8.30-t2`、`searcher` = `4.9.2`（`registry`） |
| 编辑器未回填 | 二次打开后 `manifest.json` 与收窄后的内容逐字一致 |
| 入库范围 | `git status --porcelain client` → 只有 `?? client/Assets/`、`?? client/Packages/`、`?? client/ProjectSettings/`；`Library/`、`Logs/`、`UserSettings/` 均未出现 |

## 5. 门禁与沙箱边界

| 门 | 命令 | 实测 |
|---|---|---|
| 文档门 | `node tools/check-docs.mjs` | 退出码 `0`，`OK：v2 30 份计划（S/C 链） + 10 份前置文档`；扫描 49 个文档、154 条相对链接 |
| 素材门（受限） | `node tools/check-assets.mjs` | 受限沙箱下失败：`spawnSync git` → `errno: -4048, code: 'EPERM'`（禁止管道捕获子进程输出） |
| 素材门（完整访问重跑） | `node tools/check-assets.mjs` | 退出码 `0`：`素材扫描：54 个受控文件，二进制嗅探 54 个，零素材类扩展名`、`Unity 依赖：34 个，全部在官方白名单内`、`C++ 构建清单尚未创建（S01 交付）` |
| 等价素材复核 | `git ls-files`（含 `--others --exclude-standard`）+ 扩展名黑名单正则 + 前 8192 字节 NUL 嗅探 + manifest 白名单正则 | 受控 54 个文件：黑名单 0、含 NUL 0；受控+未忽略未跟踪 82 个文件：黑名单 0、含 NUL 0；manifest 34 项 offenders 0；`package.json`/`packages/` 均不存在 |

沙箱边界（本会话的环境事实，不是仓库缺陷）：

1. `node tools/check-assets.mjs` 用 `child_process` 捕获 `git ls-files` 输出，受限沙箱（含 workspace-write）禁止命名管道 → `EPERM`；`node tools/check-docs.mjs` 不 spawn 子进程，正常通过。
2. 团结引擎批处理在 workspace-write 下**秒退**（退出码 `140063`，且不写 `-logFile`）；提升为完整访问后正常（退出码 `0`）。原因是编辑器启动要写工作区之外的 `%LOCALAPPDATA%\Tuanjie`（许可 IPC 与包缓存）。
3. 写 `HKCU` 注册表（设置 `AC_UNITY`）同样需要完整访问，否则 `SecurityException: Requested registry access is not allowed`。

## 6. 与计划文档的偏差（已同步改写）

| # | 文档 | 原文 | 实测 | 处理 |
|---|---|---|---|---|
| 1 | C01 §2 第 5 行 | 日志出现 `LICENSE SYSTEM` | 无该字样；等价证据是 `[Licensing::Module] Connected to LicensingClient` + `[Licensing::Client] Successfully updated license` + 许可组 `Unity Personal`（ULF） | 已改写 C01 §2 第 5 行 |
| 2 | C01 §4 任务 1、§5.6、§6 第 2 条、§7 第 2 条 | `ProjectVersion.txt` 恰两行；changeset 与 `& $env:AC_UNITY -version` 一致 | 实测 3 行（第三行 `m_TuanjieEditorVersion: 1.10.4`）；编辑器**没有** `-version` 开关，等价来源是 `(Get-Item $env:AC_UNITY).VersionInfo.ProductVersion` | 已改写上述四处 |
| 3 | C01 §4 任务 2 | 「让编辑器首次打开 `client/`」 | `-projectPath` 打不开尚无 `ProjectSettings/` 的目录（退出码 1）；必须 `-createProject client` | 已改写 C01 §4 任务 2 |
| 4 | C01 §4 任务 3、§5.1 渲染行 | URP「编辑器随附」「不单独钉版本」 | manifest 必须逐字写随附版本 `14.2.0-t1` 才解析到 `builtin`；URP 的间接依赖写进 manifest 会被白名单门禁判死 | 已改写 C01 §4 任务 3 与 §5.1 |
| 5 | docs/03-技术选型-v2.md §2/§4 | URP `14.1.x`；编辑器「安装器已下载」；许可「待重新激活」；出包模块「上一版本仅有 mono」 | `14.2.0-t1`；已安装 `2022.3.62t16_ab17684acf1d`；已激活（ULF，到期 2026-09-28）；本安装仅 mono 变体 | 已按实测回写 |
| 6 | C01 §3 交付物 | 未登记 `packages-lock.json` | 编辑器会生成它且应入库 | 已补一行 |

## 7. 遗留观察（不影响本份验收）

- 许可为 `Unity Personal` ULF，日志里的 `expiration` 换算为 **2026-09-28**（距本轮 4 天）；若到期后编辑器拒绝启动，需在 Tuanjie Cowork 内重新登录激活。
- 本安装**没有 IL2CPP 变体**：C15 的 IL2CPP 出包在本机不可行，按 C15 §8 用 Mono 兜底；若后续需要 IL2CPP，须用 Hub 补装 Windows Build Support (IL2CPP) 模块。
- 本次生成的 URP 全局设置资产是 `Assets/UniversalRenderPipelineGlobalSettings.asset`（纯文本 YAML），不是外部素材，符合 ADR-003。
