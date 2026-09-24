# C01 验收证据（Unity 工程基线与构建）

> 记录 2026-09-24 本机（DESKTOP-OCL09JJ，Windows 11 10.0.26200，团结引擎 `2022.3.62t16`）实测
> `docs/plans-v2/client/C01-Unity工程基线与构建.md` 的 §6 六条验证、§7 DoD 附加项、施工偏差与两轴审查结论。
> 命令按 Windows PowerShell 5.1 执行，工作目录是仓库根；带路径的命令照抄可复跑。

## 1. §6 六条验证

| # | 命令 | 实测输出 | 判定 |
|---|---|---|---|
| 1 | `powershell -NoProfile -File client/tools/verify-assets.ps1` | `OK：client 零外部素材`，退出码 `0` | PASS |
| 2a | `Get-Content client/ProjectSettings/ProjectVersion.txt` | 3 行：`m_EditorVersion: 2022.3.62t16`、`m_EditorVersionWithRevision: 2022.3.62t16 (ab17684acf1d)`、`m_TuanjieEditorVersion: 1.10.4` | PASS |
| 2b | `(Get-Item $env:AC_UNITY).VersionInfo.ProductVersion` | `2022.3.62t16_ab17684acf1d`（下划线前与 2a 首行一致，changeset 与 2a 第二行括号内一致） | PASS |
| 2c | `Select-String -Path client/ProjectSettings/EditorSettings.asset -Pattern 'm_SerializationMode: 2'`，同法查 `VersionControlSettings.asset` 的 `m_Mode` | 命中 `  m_SerializationMode: 2` 与 `  m_Mode: Visible Meta Files` | PASS |
| 2d | `client/Packages/manifest.json` 的 `dependencies` 逐项过 `tools/check-assets.mjs` 的 `UNITY_ALLOWED` | `deps=34 白名单外=[]` | PASS |
| 3 | `Get-ChildItem client/Assets -Recurse -Filter *.asmdef` | 8 份 asmdef，`references` 见下表，全部是 §5.2 允许集合的子集 | PASS |
| 4 | 先删 `client/Build` 与 `client/Logs`，再 `powershell -NoProfile -File client/build.ps1 -Target Windows64` | 末行 `Build succeeded`，退出码 `0`；`client/Build/Windows64/angry-chen.exe` 464384 B，引擎日志 `client/Logs/build.log` | PASS |
| 5 | `git status --porcelain -uall client` | 69 条，全部落在 §3：`Assets/**`（含 `.meta`）、`Packages/manifest.json`、`Packages/packages-lock.json`、`ProjectSettings/`（23 份）、`tools/verify-assets.ps1`、`build.ps1`，加 `README.md` 一处 `M`；无 `Library/`、`Temp/`、`Logs/`、`Build/`、`UserSettings/` | PASS |
| 6 | `node tools/check-docs.mjs`、`node tools/check-assets.mjs` | 两条均 `OK`、退出码 `0`；实测扫描 50 个文档 / 154 条相对链接、144 个受控文件（入库前该门只能看到 74 个）、34 个依赖 | PASS |

第 3 条的实测程序集图（`name refs=[...] platforms=[...]`）：

```
Ac.Core    refs=[]                                                              platforms=[]
Ac.Sim     refs=[Ac.Core]                                                       platforms=[]
Ac.Net     refs=[Ac.Core, Ac.Sim]                                               platforms=[]
Ac.View    refs=[Ac.Core, Ac.Sim, Ac.Net]                                       platforms=[]
Ac.UI      refs=[Ac.Core, Ac.Sim, Ac.Net, Ac.View]                              platforms=[]
Ac.Audio   refs=[Ac.Core]                                                       platforms=[]
Ac.Editor  refs=[Ac.Core, Ac.Sim, Ac.Net, Ac.View, Ac.UI, Ac.Audio,
                 Unity.RenderPipelines.Universal.Runtime,
                 Unity.RenderPipelines.Core.Runtime]                            platforms=[Editor]
Ac.Tests   refs=[Ac.Core, Ac.Sim, Ac.Net, Ac.View, Ac.UI, Ac.Audio]             platforms=[Editor]
```

## 2. §7 DoD 附加项

| 项 | 命令 | 实测输出 | 判定 |
|---|---|---|---|
| 占位素材判死 | `Set-Content client/Assets/tmp.png` 后跑素材门 | `BANNED client/Assets/tmp.png`，退出码 `1`；删除占位文件后复跑得 `OK：client 零外部素材`、退出码 `0` | PASS |
| 自检入口 | `& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Tests.SuiteRegistry.RunAll -logFile -` | `PASS c01.assemblies.references`、`PASS c01.project.identity`、`PASS c01.project.serialization`、`PASS c01.build.backend`、`PASS c01.render.pipeline`、末行 `SELFTEST OK`，退出码 `0`，`error CS` 0 条 | PASS |
| 临时引导场景不留痕 | 构建后 `Test-Path client/Assets/__BuildEntryTemp` | `False` | PASS |
| 产物构成 | `Get-ChildItem client/Build/Windows64` | `angry-chen.exe`（464384 B）、`angry-chen_Data/`、`TuanjiePlayer.dll`、`MonoBleedingEdge/`、`TuanjieCrashHandler64.exe` | PASS |

## 3. 施工偏差（计划文档 vs 实测）

计划文档按施工纪律同步改写；下表是「改了什么、为什么」的账。

| # | 实测现象 | 处置 |
|---|---|---|
| 1 | 测试程序集按计划字面放在 `client/Tests/` 时，批处理报 `executeMethod class 'SuiteRegistry' could not be found.` + `Aborting batchmode due to failure` + `return code 1`——引擎**只编译 `Assets/` 与 `Packages/` 下的程序集**，工程根下的 `.cs`/`.asmdef` 不进 AssetDatabase | 物理位置改为 `client/Assets/Tests/`（`Ac.Tests.asmdef`、`SuiteRegistry.cs`、`ContractSuite.cs`），并把 C01–C14 里所有 `client/Tests` 引用同步改写（现共 42 处 `client/Assets/Tests`）；改完后自检 `SELFTEST OK` |
| 2 | 首次构建报 `BuildFailedException: [URP] No URP Asset collected for the current build target`（URP 14 `ShaderBuildPreprocessor.cs:543`）——URP 在库且 Global Settings 的 `m_StripUnusedVariants: 1` 时，构建目标收集不到 URP Asset 会直接顶掉出包 | 新增 `client/Assets/Editor/RenderPipelineSetup.cs`（幂等 `Ensure()`，入口 `Ac.Editor.RenderPipelineSetup.Run`），生成并指派 `client/Assets/Settings/UniversalRenderPipeline.asset` 与 `UniversalRenderer.asset`，把 `GraphicsSettings` 的默认管线指向前者；`BuildEntry` 出包前调用 `Ensure()`。C01 §3/§5.1/§5.3/§8/§9 同步补记 |
| 3 | 管线资产引导要用 URP 类型 | `Ac.Editor.asmdef` 的 `references` 增补 `Unity.RenderPipelines.Universal.Runtime` 与 `Unity.RenderPipelines.Core.Runtime`（仅 Editor 程序集），C01 §5.2 与 `ContractSuite` 的允许集合同步 |
| 4 | `-createProject` 生成的 `ProjectSettings.asset` 是 `productName: client`、`companyName: DefaultCompany`、`bundleVersion: 1.0`，与 §5.1 冻结的产物标识不符 | 改成 `angry-chen` / `AngryChen` / `0.1.0`（`c01.project.identity` 用例守住） |
| 5 | C01 尚无场景资产，`BuildPipeline.BuildPlayer` 需要至少一个场景 | `BuildEntry` 在 `EditorBuildSettings` 无启用场景时临时生成 `Assets/__BuildEntryTemp/Boot.unity` 充当引导场景，`finally` 里 `AssetDatabase.DeleteAsset` 删除；下一次构建开场先清残留。已写进 C01 §5.1 与 §9；构建后实测该目录不存在 |
| 6 | `Tuanjie.exe` 是 GUI 子系统程序：PowerShell 的 `&` 立即返回（`$LASTEXITCODE` 为空），`Start-Process -Wait` 会连后代进程一起等而挂死，`Start-Process -PassThru` 的 `ExitCode` 实测取不到值 | `client/build.ps1` 生成 `client/Logs/build.cmd` 交给 `cmd /c` 代跑（cmd 会等 GUI 程序结束并原样传回退出码），stdout/stderr 直接重定向到 `client/Logs/build.log` 与 `build.stderr.log` |
| 7 | 六个运行时 asmdef 本份没有脚本，引擎逐个报 `Assembly for Assembly Definition File 'Assets/Scripts/Core/Ac.Core.asmdef' will not be compiled, because it has no scripts associated with it.`（**警告**，不是错误；`Ac.Editor`/`Ac.Tests` 照常编译并引用它们） | 不塞占位脚本；C01 §9 的能力清单按实测改写（见 §4 的显式接受项） |
| 8 | 计划里的 `& (Get-Item $env:AC_UNITY).VersionInfo.ProductVersion` 在 PowerShell 5.1 下报 `CommandNotFoundException`（把版本字符串当命令执行），且期望值 `2022.3.62t16` 与实测 `2022.3.62t16_ab17684acf1d` 不符 | C01 §2/§4/§5.6/§6 与 C02 §2、C05 §2 改为不带 `&` 的形式，期望值写成实测值 |
| 9 | 两个 `.ps1` 含中文输出（`OK：client 零外部素材` 等），无 BOM 的 UTF-8 会被 PowerShell 5.1 按 ANSI 解码 | `client/tools/verify-assets.ps1` 与 `client/build.ps1` 一律存为 **UTF-8 with BOM** |

## 4. 两轴审查结论与处置

基准点 `HEAD = e0fb3c6`；范围是 C01 的改动（不含服务端链既有的未提交文件）。

**Standards 轴**（成文标准 = `docs/00-共识/工程约定.md` §6/§10/§11 + ADR 系列）

| 发现 | 处置 |
|---|---|
| 硬违规：`SuiteRegistry.cs` 的私有字段 `Names`/`Bodies`、`ContractSuite.cs` 的 `AllowedReferences` 用了 PascalCase，应为 `_camelCase` | 改为 `_cases`（并收成一个 `Case` 结构，消掉两条并行 List 的 Data Clump）与 `_allowedReferences` |
| 硬违规：README 记 `build.ps1` 用 `Start-Process -PassThru` + `WaitForExit(1800000)`，与实现（`cmd /c` 代跑）相反 | README 改为实现口径 |
| 硬违规：`Assets/UniversalRenderPipelineGlobalSettings.asset` 未登记进 §3；§6.5 的 `git status --porcelain` 不加 `-uall` 无法发现漏登记 | §3 补该行与「其余引擎生成资产」行＋状态列说明；§6.5 改 `-uall`，明细记在本文件 §1 第 5 条 |
| 判断：`build.ps1` 的 `.exe` 判定用 `-match '.exe$'`（点号未转义） | 改 `-like '*.exe'` |
| 判断：程序集引用图有三份拷贝（8 份 asmdef、`ContractSuite` 的表、C01 §5.2）；素材黑名单有两份 | **保留**：门禁必须独立持有契约副本，两份黑名单「逐字相同」本身就是 §5.5 的要求；合并会让门禁失去独立性 |
| 判断：`check-assets.mjs` 只扫 `git ls-files`，文件未入库期间看不到 `client/Assets` | 入库后复跑两门：受控文件从 74 涨到 144，仍全绿（见 §1 第 6 条） |

**Spec 轴**（判据 = 当前 C01 计划文本）

判定：§3 的 9 条偏差里 8 条是「实测逼出的必要订正」（每条都能指到可复现的引擎/宿主行为，改写方向是把期望值改成实测值，没有删掉任何 §6/§7 检查项），**不是**洗规格。两处例外已按下表裁决：

| 发现 | 处置 |
|---|---|
| §9 原写「八个程序集编译通过」，与实测不符（六个运行时 asmdef 无脚本、被引擎跳过，编译零覆盖） | **显式接受**该覆盖缺口：§9 改写为「两个 Editor 程序集编译通过；六个运行时 asmdef 已注册，C02 落地首份代码后自动纳入编译」。不塞占位脚本——为凑一句声明而写死代码是本末倒置；运行时程序集的引用方向由 `c01.assemblies.references` 静态守住 |
| §3 把 4 份 `ProjectSettings` 文件标【修改】，而回滚目标 HANDOFF-C00 里 `client/` 只有 `README.md` | 补状态列说明：【新建】= 本份新写；【修改】= 引擎生成后由本份改写内容；【引擎生成】= 生成后原样入库 |
| README 未写全 §6 的命令（§4 第 9 条要求） | README 补「验证（C01 §6 六条命令）」小节，六条与 §6 逐字一致 |
| §6.5「只出现 §3 列出的文件」不成立（19 份引擎生成的 `ProjectSettings` 资产未点名） | §3 补「其余引擎生成资产」行；§6.5 改 `-uall` 并给出可判定口径 |
| `RenderPipelineSetup.Ensure()` 只判「默认管线是 URP 类型」，不校验是否指向 §5.1 冻结资产，且资产已在库时会 `CreateAsset` 覆盖 | `Ensure()` 改为：已入库资产只重新指派、绝不覆盖；并新增 `c01.render.pipeline` 用例断言 `GraphicsSettings.defaultRenderPipeline` 就是 `Assets/Settings/UniversalRenderPipeline.asset` |
| 临时引导场景未写进冻结契约，构建被打断会残留 | 写进 §5.1 与 §9；`CreateBootScene()` 开场先删除残留目录 |

Spec 轴另交叉复核通过：8 份 asmdef 的 `references` 与 §5.2 逐条一致、`includePlatforms` 平台限制正确；manifest 34 项全在白名单内、URP 为 `14.2.0-t1` 且 `packages-lock.json` 的 `source=builtin`；`GraphicsSettings` 的 `m_CustomRenderPipeline` guid 与构建日志里 `importing Assets/Settings/UniversalRenderPipeline.asset using Guid(09b520d126f3a6c40ad7ae0b822249e0)` 一致（引用不悬空）；两份素材黑名单扩展名逐项相同（36 项）。

## 5. 复跑命令（按顺序）

```powershell
# 素材门与仓库门禁
powershell -NoProfile -File client/tools/verify-assets.ps1
node tools/check-docs.mjs
node tools/check-assets.mjs

# 分组自检（编辑器批处理，末行 SELFTEST OK）
& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Tests.SuiteRegistry.RunAll -logFile -

# 出包（末行 Build succeeded，退出码 0；产物 client/Build/Windows64/angry-chen.exe）
powershell -NoProfile -File client/build.ps1 -Target Windows64
```
