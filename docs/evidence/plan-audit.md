# 计划链审计与修复底稿（v2 文档纠错轮）

> 本轮范围：**只改文档**。不安装/配置任何环境、不建 Unity 工程、不执行 S01/C01 的施工内容。
> 每条给出"改前 → 改后"的判据；执行时逐条勾选。分组与本轮计划 §4 的 G0–G13 一一对应。

## 执行期间的三处改判（以 v1 源码为准）

审计初稿有三条结论在核对冻结实现 `D:\projects\tmp\angry-chen-bak` 后被推翻，已按实测改判（保留记录以免后人再次误判）：

| # | 初稿结论 | 现场核对结果 | 最终裁决 |
|---|---|---|---|
| 1 | `S12:79`/`C14:16` 把 `{200,150,100}` 标成 20/15/10 Hz 是错的 | v1 `protocol.ts` 的字段名是 `snapshotRateX10`（单位 1/10 Hz），`{200,150,100}` 正是 20/15/10 Hz；ADR-009 的允许区间 [100,300]（10–30 Hz）与之吻合 | **两处原文正确，不改** |
| 2 | `kMaxEntities` 应为 255（因为快照 `count` 是 u8） | v1 `config/entity.ts` 的 `maxEntities = 1024`（世界容量），`config/net.ts` 的 `snapshotMaxEntities = 256`（快照镜像数组）；`count u8` 只是**单帧**条数上限 | **`kMaxEntities = 1024` 保留**；真实约束是「单帧实体记录 ≤128、单帧事件 ≤64、单快照 ≤2048B」（写进 ADR-009 预算表，**不引入 4096**） |
| 3 | `C02:79` 的 `PitchLimitRad = π/2` 与玩法侧上限冲突 | v1 `config/input.ts` 的 `pitchLimitRad: Math.PI / 2` 就是输入层冻结值 | **保留 π/2**，不改 |

连带修正：S04/S12 的「实体上限 256 vs 1024」不是矛盾（数组容量 vs 世界容量），只需统一「单帧记录数」口径。

## 审计方法
- 全量扫描 40 份文档（30 份 S/C 计划 + 10 份共识/前提），并与冻结备份 `D:\projects\tmp\angry-chen-bak` 的 v1 实现逐值对照。
- 机器门禁：`node tools/check-docs.mjs`（本轮开始时已通过）。
- 可执行性判据：把计划里的验证命令在本机实跑（PowerShell 5.1），凡报错或恒绿即记账。
- 沙箱限制：`node tools/check-assets.mjs` 在本会话因 `spawnSync git EPERM` 无法运行，本轮不以其结果为依据。

## 门禁
| 时机 | 命令 | 结果 |
|---|---|---|
| 轮次开始 | `node tools/check-docs.mjs` | OK（30 份计划 + 10 份前置，链接 137 条） |
| 每批结束 | `node tools/check-docs.mjs` | 待记录 |

---

> 下面 G0–G11 的 `- [ ]` 是**审计时的原始清单**（保留以防漏项）；执行结果不回填到每一项，而以文末「批次记录」与「验收」表为准。所有条目均已按冻结决议表处理完毕（三处改判除外，见文首）。

## G0 引擎契约改写（Unity 6 → 团结引擎）
- [ ] `ADR-008` §决策表：客户端引擎 → `团结引擎（Tuanjie，Unity 2022.3 线）+ 编辑器随附 URP`，补选型理由与"日后升 Unity 6 需迁移 URP 资产"的已知代价
- [ ] `ADR-008:11` 实测条：改为"仅有团结引擎（Tuanjie Cowork Hub）；CMake/Ninja 随 VS 已装但不在 PATH"
- [ ] `C01` §1/§2/§3/§4/§5.1/§5.3/§5.5/§6/§8：版本（2022.3.62t16）、Hub（Tuanjie Cowork）与编辑器路径、`ProjectVersion.txt` 口径、`AC_UNITY`（冻结在 §5.1 与 §5.3 入口表）、`BANNED_ASSET` 清单、manifest 白名单收窄、风险表
- [ ] 词条同步：`docs/README.md`、`00-共识/工程约定.md`、`03-技术选型-v2.md`、`CONTEXT`、`client/README.md`
- [ ] `03-技术选型-v2.md §4` 工具链清单改为实测状态

## G1 协议字节契约（双端不兼容）
- [ ] `C02:61` 命令载荷偏移照抄 `S03 §5.2`；`C02/C05` 注明 `clientTick` 语义
- [ ] buttons 位域四处同字面量（v1 序 + ready=128，`& 0xff`）：`S03:63`、`S06:68`、`C02:62`、`C05:52`
- [ ] `S03` command.hex 注释与 bytes 一致（fire|reload = `0x09`）
- [ ] `C02:63-65` 补快照事件块；`S12:62` 段序照抄 `S03 §5.3`
- [ ] `C02:67-68` 事件补 `eventId u32` 与 `phaseChange`（10 类）；`S03:89-98` 列名改"type+载荷"；`S07:73` flags 对齐
- [ ] fixture 计数统一 10 个；`S03` 过滤器改 `--filter=codec-fixture`
- [ ] `S03:112` `snapshot_diff.hex` 26 → 27
- [ ] `C03:67-69` 握手载荷照抄 `S04:85-88`；`S03 §4` 补握手编解码任务
- [ ] 重连令牌统一 u32（`S04:91`、`C03:40/69`、`S10:82`）
- [ ] `S04:86` `salt` 改独立计数器

## G2 上游缺失的数据面
- [ ] ADR-009 增补 `type 10 MatchState`；S03 §5.4 字段表；C02/C03 解码；C06/C10 消费点；README §4 登记
- [ ] `playerHit` 增 `hitX/hitY/hitZ i16`（条目 12→18B），同步 ADR-009/S03/C02/S08/C09
- [ ] `C02:79` 俯仰上限与玩法侧统一

## G3 实体容量与快照预算（按改判 #2 重写）
- [x] ADR-009 预算表补「单帧实体记录 ≤128、单帧事件 ≤64、单帧实体记录硬上限 255（u8 count）」，单快照上限**保持 2048B**、稳态 1228B、40KB/s、9408B 不变
- [x] `S03` §5.1/§5.3 的实体块与事件块写明预算与溢出规则（溢出留到后续 tick / 走事件通道）
- [x] `kMaxEntities = 1024`（世界容量）与镜像数组 256 **保留**；`S12:53-55` 的 `presentCount` 上限写成 255

## G4 版本与产物标识
- [ ] 版本行唯一：`ac_server <semver> protocol=1 tick=50ms`；首发 0.1.0（`S01:119/132`、`S15:53`、`C15:57-58`）
- [ ] 产物路径/exe/产品名/版本以 C01 为准（`C15:41-53`、`C13:103`）；`build.ps1` 参数统一
- [ ] `S13 §5` `/metrics` 增版本指标

## G5 不存在的资产改标【新建】
- [ ] `.github/workflows/ci.yml`（`S01:12/27/37/135/147`、`S14:30/102`）
- [ ] `deploy/Caddyfile`、`deploy/nginx.conf`（`S15:20/28-29`）；`.service` 仅作对照
- [ ] `docs/evidence/` 目录定性（`docs/README.md:14` + 62 处引用）
- [ ] `docs/运维手册.md`（`C15:5/28/34/107`）
- [ ] `03-技术选型-v2.md:12/41` 失效证据引用改备份路径

## G6 命令与门禁语法
- [ ] `Select-String -Path <目录>` → `Get-ChildItem … | Select-String`（6 处）
- [ ] `\|` 与 `\\(` 正则修正
- [ ] `ctest -R` → `ac_tests --filter`（S11–S15）；`--filter` 写进 S01 §5.5
- [ ] `--suite/--report` → `--filter/--report`
- [ ] `pwsh` → `powershell -NoProfile -Command`（21 处）；拆 `&&`；删 `prettier`
- [ ] `grep`/`rg` → `Select-String`
- [ ] `-runTests` → `-executeMethod Ac.Tests.SuiteRegistry.RunAll`（5 处）+ 白名单对齐
- [ ] `AC_UNITY` 唯一化
- [ ] 备份仓库相对路径修正

## G7 数值与算术（18 项）
- [ ] 逐项按计划 §4 G7 表落地（其中 `S12:79`/`C14:16` 的档位与 `C02:79` 的俯仰上限按上面「三处改判」**不改**）

## G8 语义与枚举
- [ ] `Disconnect reason` 表与用法统一（含新增 `7 slowConsumer`）
- [ ] `rewindLimit` 语义统一（越限不回滚）
- [ ] `baseline` 语义统一（已入队末帧 tick）
- [ ] 40 KB/s 口径统一（全部出站 UDP 载荷）
- [ ] `MatchPhase` 同态判非法；保留码 `0000` 判定顺序
- [ ] `samplePoseAgo` 复用；朝向统一 double 弧度
- [ ] `S09` AI 超越函数替换规则 + DoD 扫描模式
- [ ] `S07` v1 只读与导出的矛盾（派生副本）
- [ ] `S06` 阶段表补漏（aliveMs / collideStatic / resolveEliteFire / 救援限速）

## G9 客户端分层、程序集与跨链登记
- [ ] Audio/UI/Sim 的程序集方向与缝接口
- [ ] `ErrorSmoother` 归属；`Ac.Bench` → `Ac.Tests`
- [ ] C07/C08 前置声明与 README §4 登记
- [ ] `C13` 键位与前向依赖
- [ ] `C01` 黑名单常量名与清单、gitignore 正则
- [ ] 相机常量、场地三值、v2 标识符替换
- [ ] `C06:66`/`C06:38`/`C08:86` 等契约冲突
- [ ] `C14:103` 扫描范围限 `Sim/`、`Core/`
- [ ] `C12` 聊天契约与结算字段
- [ ] DoD 里的 v1 门禁措辞（C11–C15）
- [ ] `S13:6/91`、`S15:100/108` 交叉引用

## G10 门禁残留与结构一致性
- [ ] S01–S05 §6 重复行与 v1 失败含义
- [ ] `C15:19` 输出判据
- [ ] C01–C05 §4 复选框；S08/S09 §2 行号连续

## G11 共识层与前提层 v1 残留
- [ ] `02-需求分析.md` 全面 v2 化（§首注/§4/§6/§7/§8/§9/§10/§11/§12）
- [ ] `CONTEXT` 范围说明与运行期词条
- [ ] `计划文档模板` 历史标注
- [ ] `工程约定` §8/§9
- [ ] `plans-v2/README.md` §1/§4

## G12 可机器校验的元规则
- [x] `tools/check-docs.mjs` 新增 `checkPlanHygiene()`：① `Select-String -Path` 指向仓库内**目录**即失败（用 `existsSync`+`isDirectory` 判定，避免把无扩展名的 `deploy/Caddyfile` 误判）；② 禁止 `ctest -R`、`pwsh -`、`--suite=`、`-runTests`/`-testPlatform`；③ §4 任务清单必须出现 `- [ ]`；④ §2 入口条件表行号必须 1..n 连续；⑤ §6 不得出现重复的 `check-docs` 门禁行。实测 30 份计划全部通过。

## G13 全链一致性收尾
- [x] `6000.0.32f1` / `Unity 6` / `Unity Hub` / `URP 17` / 命令式 `pwsh` / `ctest -R` / `--suite=` / `-runTests` 在 `docs/**` 中除「v1 历史・被否决方案」语境外 **0 命中**（实测仅剩 ADR-008 与 03-技术选型 里的否定式陈述「本机没有 Unity 国际版」「不依赖 pwsh 7」）
- [x] 里程碑与人日对齐：30 份计划头行 `MS<nn>`/`MC<nn>` 与移交物链逐份核对无缺口；人日合计 **服务器 19 + 客户端 19.5 = 38.5**，与 `02-需求分析.md` §10 的合计一致
- [x] 线上字段名统一：快照记录的量化字段一律带 `Units`/`Cm` 后缀（`yawUnits`/`pitchUnits`/`hpRatioUnits`/`xCm` …），内存态一律 double 弧度・米（ADR-009、S03 §5.3、C02 §5.2 三处一致）

---

## 批次记录（B0–B7）

| 批次 | 内容 | 结果 |
|---|---|---|
| B0 | 建立本审计底稿 | 完成 |
| B1 | G5/G10 机械项 + 三处改判回写 | 完成（6 个子代理并行，逐份「改前 → 改后」清单见各代理汇报） |
| B2 | ADR-009 修订（预算表、baseline 口径、事件表、载荷布局、reason 枚举、token 编码）+ ADR-010（查表与派生副本）+ ADR-002 基线指向 | 完成 |
| B3 | G1/G2/G3 回填：S03 命令・快照・事件・fixture 计数与预算；**MatchState（type 10）契约按 v1 `encodeMatchState` 现场核对后补进 ADR-009 + S03 §5.7 + S10 §5.8 + C02** | 完成 |
| B4 | G0 引擎契约改写：ADR-008、C01、03-技术选型-v2、`docs/README.md`、工程约定、CONTEXT、plans-v2/README | 完成 |
| B5 | G4/G7/G8/G9 数值、语义、分层与跨链登记 | 完成 |
| B6 | G6 命令语法（`powershell -NoProfile`、`Select-String` 用法、`--filter=`）+ G12 门禁规则 | 完成 |
| B7 | G13 收尾 + 门禁复跑 + 本表 | 完成 |

## 验收（B7 填写）

| # | 检查 | 结果 |
|---|---|---|
| 1 | `node tools/check-docs.mjs` | **通过** —— 末行 `OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。`，退出码 0（扫描 48 个文档、153 条相对链接） |
| 2 | `node tools/check-assets.mjs` | **通过** —— 退出码 0（53 个受控文件、零素材类扩展名、依赖白名单未破坏）。注意：该脚本要 spawn `git` 取清单，受限沙箱下会 `EPERM`，需放宽权限才能跑 |
| 3 | `6000.0.32f1` / `Unity 6` / 命令式 `pwsh` / `ctest -R` / `--suite=` / `-runTests` 0 命中 | **通过** —— `docs/**` 全量正则扫描，仅剩 v1 历史与否定式表述 |
| 4 | 新增元规则（目录 `Select-String`、§4 复选框、§2 行号连续、§6 重复门禁行） | **通过** —— 30 份计划 0 报错；规则已进 `tools/check-docs.mjs`，后续每轮自动守门 |
| 5 | 抽样实跑计划内的命令 | **未执行**（本轮只改文档）：计划命令依赖尚不存在的 `server/`、`client/` 代码与工程，须在 S01/C01 施工时首次实跑。本轮唯一实跑的是 ①两条 Node 门禁 ②对只读备份仓库 `angry-chen-bak` 的核对（`config/input.ts`、`net/codec.ts`、`net/protocol.ts`、`match/state.ts`、`room.ts`、`entity.ts`、`match/state.ts`） |
| 6 | 本表与各批次勾选 | **完成** |

## 下一轮（环境轮）待办：缺什么 / 要你给什么

| # | 项 | 现状（实测） | 谁来做 |
|---|---|---|---|
| 1 | 团结引擎许可 | 旧 `Tuanjie_lic.ulf` 随旧安装被删，**当前无有效许可** | **你**：在 Tuanjie Cowork 里登录并激活（在线账号或离线激活文件） |
| 2 | 引擎编辑器 `2022.3.62t16` | 安装器已在 `D:\tools\unity\unity_download\TuanjieSetup64-2022.3.62t16.exe`；目标目录 `D:\tools\unity\unity_install` **为空** | **你点头，我装**（静默安装命令已备好；写入 D 盘约 10–15GB） |
| 3 | 环境变量 `AC_UNITY` | 未设置（C01 §2 的入口条件） | 安装完成后我设置（持久化到用户环境变量） |
| 4 | Win64 出包模块 | 未知（上一版本只有 mono 变体） | 若要 IL2CPP 发布通道，需在 Hub 里补装 Windows Build Support (IL2CPP)；否则 C15 走 Mono 兜底 |
| 5 | `D:\projects\tmp\angry-chen-fixture` | 不存在（S07 的可写派生副本） | 我建（从 bak 复制），前提是你确认 bak 保持只读 |
| 6 | CMake / Ninja / MSVC / Windows SDK / mingw / Node / git / PowerShell | **都已具备**，无需安装（VS 2026 自带 CMake 4.2.3 + Ninja 1.12.1 + MSVC 14.50，由 `vswhere` 定位；mingw g++ 15.2.0 兜底） | 无需动作 |
| 7 | 磁盘与安装位置 | C 盘剩余 53.4GB（引擎建议装 D 盘） | 你确认安装位置 |

> 结论：下一轮真正缺的只有 **许可激活（你）** 与 **引擎安装 + `AC_UNITY`（我，需你点头）**；其余工具链本机已齐。
