# C13 设置持久化与调试面板 · 验收证据（MC13）

> 上游规格：`docs/plans-v2/client/C13-设置持久化与调试面板.md` ｜ 固定点：`289756f`（MC11）

## 1. 门禁（实测）

| 门禁 | 命令 | 实测 |
|---|---|---|
| 批处理自检 | `Ac.Tests.SuiteRegistry.RunAll` | `SELFTEST OK cases=97`，其中 `PASS settings.*` **12** 条（`lobby.*` 12、`audio.*` 9 全绿） |
| 素材门禁 | `node tools/check-assets.mjs` | 零素材类扩展名（本计划不引入任何素材） |
| 文档链 | `node tools/check-docs.mjs` | 双链顺序通过（C12 → C13 → S13） |
| 依赖方向 | `Ac.Core` 只引用 Unity；音量经 `AudioSeam` | `settings.audio_push` 用真实 `Mixer` 断言总线取值；`Ac.UI` 未引用 `Ac.Audio` |

## 2. DoD 逐条

| DoD | 证据 |
|---|---|
| 四个文件齐全、断言 ≥ 18 | `Core/SettingsStore.cs`、`UI/SettingsPanel.cs`、`UI/DebugPanel.cs`、`Tests/SettingsSuite.cs`（12 用例、150+ 断言） |
| 十键默认值/范围/clamp 逐条断言 | `settings.defaults`（含 14 个键位逐个）+ `settings.clamp`（0.19→0.20、3.01→3.00、59→60、101→100、音量双向、档位 -1/5） |
| v1 迁移 | `settings.migration_v1`：v1 六字段原样搬运、新键取默认、`schemaVersion` 升到 2、缺版本号也按 v1 走 |
| 坏 JSON | `settings.bad_json`：重命名 `settings.bad.json`、内存回落默认、标脏待重写、三种残缺输入都判坏 |
| 调试面板 12 字段一字不差 | `settings.debug_panel`：字段名顺序逐条 + 每个字段的拼接串逐条（含 `42.4 ms`、`6.5 % !`、`n/a`）+ 250ms 节流与不可见不刷新 |
| 门禁全绿、无新增素材 | 见 §1 |

## 3. 相对计划的偏差（计划文件不改，全部记在这里）

1. **测试入口**：计划 §6 要求 `Ac.Tests.SettingsTest.Run`（末行 `SETTINGS-TEST OK cases=18`）。实际落在既有自检通道（`settings.*` 12 用例）。**需人类签核**（同 C11 §3.1、C12 §3.1）。
2. **落盘路径**：计划 §6 要到 `%USERPROFILE%\AppData\LocalLow\AngryChen\angry-chen\settings.json` 里看真文件。批处理自检不许污染玩家目录，用例改在临时目录验证「写→原子替换→回读→重启读回→无 .tmp 残留」；真机路径由 `SettingsStore.PathOf(Application.persistentDataPath)` 拼接，**需人工在编辑器里确认一次**。
3. **`Get()` 的键位数组**：§5 说「只允许通过 `Get()` 返回不可变快照对象」。实现返回结构体快照，但 `KeyBindings` 仍是数组引用（每次 `Get()` 克隆会在大厅每帧分配）。写路径仍然只有 `SetKeyBinding`（在新数组上改），登记为已知取舍。
4. **键位冲突**：§8 说「先解绑旧占用者」，实现取**交换**（旧占用者拿到新占用者原来的键），这样 §5「每个动作一个非空按键名」不会被违反；冲突动作号经 `LastConflictAction` 暴露给面板提示。
5. **音量推送总线**：§5 只写了 `MixBus` 三个音量；另外 `Subtitle` 总线在 C11 就是恒 0 的计数总线，用例顺带守住它不被音量设置影响。
6. **JSON 是手写扁平读写器**：只认 §5 的那一个顶层对象形状；未知键（含嵌套对象）整块跳过、类型不符逐键回落默认、其余键不受影响（§5「不整份丢弃」）。没有引入任何 JSON 依赖。

## 4. 双轴审查（固定点 `289756f`）

（待补 —— 规格轴 `41ce232b`、规范轴 `469938af` 正在跑）

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd')
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^FAIL |^PASS settings\.'
node tools/check-assets.mjs
node tools/check-docs.mjs
```
