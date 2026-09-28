# 上游计划 ↔ 实际实现对照（B6）

> 登记日期：2026-09-28 ｜ 守门用例：`Ac.Tests.PlanAliasSuite.plan.alias_table_covers_plans`
> 规矩：**不改上游计划**（`docs/plans-v2/client/*.md` 是对意图的冻结记录）。计划里点名的测试入口与最终实现不一致时，
> 一律登记在此表，并由上面那条用例机器守门——计划提到的每个测试文件/入口，必须**要么真实存在，要么在本表里**；
> 表里每一条的目标文件必须真实存在、映射的用例前缀必须真的有已注册用例。

## 1. 计划写了什么

大部分计划（C01/C02/C04/C05/C06/C08/C09/C10/C14/C15）从一开始就写的是统一入口
`Ac.Tests.SuiteRegistry.RunAll` + 末行契约 `SELFTEST OK`，与实现一致，无需对照。

**只有 4 份计划**点名了独立文件与独立入口，实现改成了统一 suite：

| 计划与出处的原文 | 实际实现 |
|---|---|
| C11 `client/Assets/Tests/audio_test.cs`【新建】 | `client/Assets/Tests/AudioSuite.cs`（`audio.*` 用例） |
| C11 §验收 `AudioTest`（批处理入口）：`Ac.Tests.AudioTest.Run()`，成功时 stdout 末行 `AUDIO-TEST OK r…` | `Ac.Tests.SuiteRegistry.RunAll`，末行 `SELFTEST OK cases=<n>`；条目数由用例数表达 |
| C12 `client/Assets/Tests/lobby_flow_test.cs`【新建】 | `client/Assets/Tests/LobbySuite.cs`（`lobby.*` 用例） |
| C12 §验收 `LobbyFlowTest`（批处理入口）：`Ac.Tests.LobbyFlowTest.Run()`，末行 `LOBBY-T…` | `Ac.Tests.SuiteRegistry.RunAll`，末行 `SELFTEST OK cases=<n>` |
| C13 `client/Assets/Tests/settings_test.cs`【新建】 | `client/Assets/Tests/SettingsSuite.cs`（`settings.*` 用例） |
| C13 §验收 `settings_test.cs` 断言数 ≥ 18 | `settings.*` 用例数（`SELFTEST OK cases=<n>` 计数 `cases` 覆盖） |
| C07 §回滚 `arena_mesh_test.cs` | `client/Assets/Tests/ArenaSuite.cs`（`arena.*` 用例） |
| C04 §回滚（:132）`interpolation_test.cs` | `client/Assets/Tests/InterpolationSuite.cs`（`view.*` 用例） |

## 2. 为什么这样处理

1. **不重写计划**：计划是"当时要什么"的冻结记录，回填实现名会让 `docs/evidence/plan-audit.md` 那类审计失去参照。
2. **不为了对齐名字造空壳**：造 `audio_test.cs`/`AudioTest.Run()` 这种只为名字存在的壳，等于给门禁造假绿——名字对上了，行为反而多一层转手。
3. **把漂移变成可检测**：真正的问题是"以后没人知道哪些名字对不上"。现在只要有人在计划里新写一个不存在的测试文件或入口，
   `plan.alias_table_covers_plans` 立刻变红，逼着要么改名、要么补进本表（补表就必须写出真实目标与用例前缀，而目标不存在也会红）。

## 3. 契约替换（一句话）

计划里的每份独立入口都以 `AUDIO-TEST OK`/`LOBBY-T…`/`SETTINGS-T…` 这类专属末行为契约；实现统一为
**单一入口 `Ac.Tests.SuiteRegistry.RunAll` + 单一末行 `SELFTEST OK cases=<n>`**（退出码 0/1 语义不变，
失败行 `FAIL <caseId> expected=… actual=…` 与 `SelfTest` 的冻结格式一致）。发布链与运维手册引用的也都是这一条统一入口。
