# C13 设置持久化与调试面板 · 验收证据（MC13）

> 上游规格：`docs/plans-v2/client/C13-设置持久化与调试面板.md` ｜ 固定点：`289756f`（MC11）

## 1. 门禁（实测）

| 门禁 | 命令 | 实测 |
|---|---|---|
| 批处理自检 | `Ac.Tests.SuiteRegistry.RunAll` | 修订后 `SELFTEST OK cases=101`，其中 `PASS settings.*` **16** 条（`lobby.*` 12、`audio.*` 9 全绿）；另 7 条 `perf.*` 属未提交的 C14 |
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

**两轴均判不通过**，报告 `client/Logs/review-c13-spec.md`、`client/Logs/review-c13-standards.md`。两个审查都用 Tuanjie 自带 Mono + Unity 桩真编译真执行过这四个文件（12 用例全绿），所以下面这些都是"用例测不到"的真缺陷，不是变红。逐条处置：

| 发现（轴） | 处置 |
|---|---|
| 高：**NaN 穿透 clamp 并写出 `"masterVolume": NaN`** | ✅ 三个浮点 clamp 先判 NaN 一律回落默认值；新增用例（NaN 三键 + 正无穷夹到上界 + 写出的 JSON 不含 NaN） |
| 高：**合法的 `2.0`/`2e0` 版本号被判成 v1**，静默丢 `qualityTier`/`musicVolume`/`crosshairColor`/`keyBindings` 并回写 | ✅ 版本号改按浮点解析（缺键/非数/NaN/负数才当 v1）；新增用例断言 `2.0`/`2e0` 下这些键不丢 |
| 高：**写盘不转义字符串** → 文件非法 → 下次启动整份改名 `settings.bad.json` | ✅ `Escape`/`Unescape` 一对，读写两侧都还原；新增用例用 `A"B\C` 做键名走完整往返 |
| 高：**`Get()` 与内部共享 `KeyBindings` 数组**（外部改数组即改内部且不标脏），违反 §5「只读快照」 | ✅ `Get()` 克隆数组再返回；本文 §3.3 的"取舍"作废，改记为本轮已修 |
| 高：**`Reset()` 清掉只读标记** → `schemaVersion > 2` 的文件被 v2 默认值覆盖 | ✅ `Reset` 不再碰 `ReadOnlyFile`；新增用例：装 v3 → `Reset` → 强制 flush 仍不写盘、文件里仍是 v3 |
| 高：**坏 JSON 抛异常逃逸 `Load`**（`{"keyBindings": [` → `ArgumentOutOfRangeException`），违反 §5「不崩」 | ✅ 数组/值截断一律判坏 JSON；新增用例（4 种截断输入 + 真落盘文件走一次 `Load` 不抛异常） |
| 高：**迁移从不写回**（`TryParse` 出口已把版本改成 2，比较分支是死代码，实测磁盘仍是 v1） | ✅ 新增 `TryParse(json, out snapshot, out sourceVersion)` 出口，`Load` 用**文件里原本的版本号**判迁移并标脏 |
| 中：写盘无兜异常；`.tmp` 路径三处硬拼 | ✅ `Save` 兜 `IOException`/`UnauthorizedAccessException`（设置仍在内存生效、不崩）；统一走 `TempName`/`TempPathOf` |
| 中：用例恒真 `:355`、自证 `:264`、阈值无等值边界 | ✅ 恒真断言换成 `frameTimeMaxMs: n/a`；自证断言改成字面量 `"E"`；等值边界（正好等于 120/5/40960/8192/20/33/10 时**不许**标红）加入 `debug_panel` |
| 中：JSON 边界（转义解码、BOM、重复键、宽容逗号）；与已交付 `Core/MiniJson.cs` 重复实现 | ⏸ BOM 已由 `File.ReadAllText(..., Encoding.UTF8)` 兜住；转义解码已补；重复实现登记为债（收敛到 `MiniJson` 会改动读写两侧语义，单独排期） |
| 中：§9 API 形状（`Set(string,object)`、`static FilePath`、`Reset` 返回值、`Open/Close/IsOpen`、`Update(in DebugSample)`、`WithinBudget`） | ⏸ 与 §3.1 同一条流程问题：接口名/形状需人类签核后统一（功能都在，名字不同） |
| 中：§4 任务 7 接线（三类零引用、无 `persistentDataPath`、无 `BeginFrame/FlushIfDirty` 调用者） | ⏸ 与 §3.2 同一条顺延：装配点 `BeginFrame/FlushIfDirty(Application.persistentDataPath)` 已提供，调用方是场景装配步骤（C07–C13 同一条口径） |
| 次：范围常量三源、键表散在 8 处、`Float("0.00")` 精度、只写不读成员 | ⏸ 登记为债（`0.00` 是 §5 要求的落盘形态；精度损失只在人为手改文件时可见，读入侧一律 clamp） |

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd')
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^FAIL |^PASS settings\.'
node tools/check-assets.mjs
node tools/check-docs.mjs
```