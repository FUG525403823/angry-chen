# C12 大厅、房间与结算界面 · 验收证据（MC12）

> 上游规格：`docs/plans-v2/client/C12-大厅房间与结算界面.md` ｜ 固定点：`289756f`（MC11）

## 1. 门禁（实测）

| 门禁 | 命令 | 实测 |
|---|---|---|
| 房间码字母表一致 | `Select-String -Path client/Assets/Scripts/UI/RoomCodeInput.cs -Pattern 'ABCDEFGHJKMNPQRSTUVWXYZ23456789'` | 命中 **1** 行（31 字符，排除 I/L/O/0/1） |
| 阶段单一真相 | `Select-String -Path UI/Lobby.cs,UI/Intermission.cs,UI/Results.cs -Pattern 'Hud.Phase'` | 命中 **10** 处（Lobby/Intermission 9 处 + Results 1 处）（三个文件都从 C10 的 `Hud.Phase*` 取，不自造阶段） |
| 批处理自检 | `Ac.Tests.SuiteRegistry.RunAll` | MC12 定点（未挂 C13 的 13 条用例时）`cases=85` = 73（C03–C11）+ 12（`lobby.*` 全绿）；C13 落地后同点位为 `cases=97` |
| 素材门禁 | `node tools/check-assets.mjs` | `452 个受控文件，二进制嗅探 452 个，零素材类扩展名` |
| 文档链 | `node tools/check-docs.mjs` | 双链 S×15 + C×15 顺序通过（C11 → C12 → S11） |
| 跨链前置 | `S10` 冻结的房间码/阶段/波间/结算契约 | 已按 `server/src/persist/match_store.hpp`、`client/Assets/Scripts/Net/MatchStateCodec.cs` 实查比对（见 §3.4） |

## 2. DoD 逐条

| DoD | 证据 |
|---|---|
| 八个文件齐全 | `UI/RoomCodeInput.cs`、`Roster.cs`、`WeaponPicker.cs`、`Lobby.cs`、`Chat.cs`、`Intermission.cs`、`Results.cs`、`Tests/LobbySuite.cs` |
| 用例 ≥ 22 | 12 个 `lobby.*` 用例、60+ 条断言（`lobby.roomcode_normalize` 一条 13 条） |
| 房间码 8 例 | `ab3k`→`AB3K`、`a b0o`→`AB`、`ABCDEF`→`ABCD`、全角被丢弃、空串、两侧空格、全表外字符、`ab-cd`→`ABCD` |
| 阶段显隐 5 例 | 五个相位逐个断言「至少一个界面、最多一个界面、无空相位」 |
| 排序 5 例 | 击杀降序 → `startedAtMs` 降序 → `matchId` 升序（升序方向由用例断言 `m-a` 在前）、榜长上限 10、明细字段同 schema |
| 聊天限流 4 例 | 空文本、64 字节截断（含 21 个汉字 = 63 字节、代理对整只保留）、500ms 间隔、5 秒窗口第 6 条丢弃 |
| 无外部素材 + 门禁全绿 | `check-assets` / `check-docs` 全过，批处理退出码 0 |

## 3. 相对计划的偏差（计划文件不改，全部记在这里）

1. **测试入口**：计划 §3/§6/§9 要求 `client/Assets/Tests/lobby_flow_test.cs` 与 `Ac.Tests.LobbyFlowTest.Run()`（末行 `LOBBY-TEST OK cases=22`）。实际落在既有自检通道（`SuiteRegistry.RunAll`，12 个 `lobby.*` 用例，末行 `SELFTEST OK cases=85`）。**需人类签核**（与 C11 §3.1 同一条口径）。
2. **§6「阶段表一致性」门禁的 `MatchPhase` 关键字**：客户端没有 `MatchPhase` 这个类型，阶段码的唯一来源是 C10 的 `Hud.PhaseLobby/Loading/Playing/Intermission/Ended`（权威 `server/src/room/phase.hpp: lobby=0/loading=1/playing=2/intermission=3/ended=4`）。三个文件都从 `Hud.Phase*` 取阶段，门禁等价。**需人类签核**（改成把 `MatchPhase` 提到 `Ac.Core` 是更彻底的解法，但那是 C10 的既有面，不在本计划范围）。
3. **§5 内部矛盾（需仲裁）**：§4 任务 6 写「未满 5000ms 时禁用跳过」，§5 波间规则写「`intermissionMs > 15000` 时禁用跳过」。两者不可能同时成立。实现取 §5 冻结表的写法：`SkipEnabled = 可见 && 剩余 > 0 && 剩余 ≤ 15000`；`IntermissionMinMs = 5000` 作为常量保留并被断言。**需人类仲裁阈值语义。**
4. **数据形状实查结果**：`MatchState` → `Ac.Net.MatchStatePayload`（`Phase/Wave/IntermissionMs/Players[]`），行 → `Ac.Net.MatchStatePlayer`（`Pid` 是 `ushort`，不是 §5 表写的 `int`；无 `hostId` 字段，主机按 `min(pid)` 推导，与 §5 表一致）。结算 schema 逐字照抄 `server/src/persist/match_store.hpp`：`MatchRecord{matchId,startedAtMs,durationMs,waveReached,winnerTeam,players[]}` + `PlayerRecord{name,kills,headshots,shotsFired,hits,revives,downs,aliveMs,leftMidMatch}`；排序键用服务端 `totalKills()` 的语义（Σ players.kills），不新增字段。
5. **`ResultsSummary` 是客户端本地类型**：接口不可达时用「本局 matchEnded 载荷」兜底渲染（§8 风险表第 2 行），它不在服务器 schema 里，只有 `matchId/waveReached/winnerTeam/durationMs` 四个字段。
6. **聊天发出去的是本地回显**：`TrySend` 成功即 `Push(0, text)`（自己那行），真正的网络发送归 S03/会话层；服务器侧聊天限流常量 v2 未冻结（§5 明说），客户端阈值按更严假设（500ms / 5 条）。
7. **`Roster.VisibleRowLimit = 4`** 与服务端 `kMaxPlayersPerMatch = 4` 同值，但字节名不同（一个是 UI 行数上限、一个是每局玩家上限）；两者都各自断言，未合并成一个常量。

## 4. 双轴审查（固定点 `289756f`）

**两轴均判不通过**，报告 `client/Logs/review-c12-spec.md`、`client/Logs/review-c12-standards.md`。逐条处置：

| 发现（轴） | 处置 |
|---|---|
| 规格高：`lobby_flow_test.cs` / `LobbyFlowTest.Run` 不存在，末行 `LOBBY-TEST OK cases=22` 不可达 | ⏸ 见本文 §3.1：与 C11 同形的流程偏离，需人类签核改规格（一条通道一个入口） |
| 规格高：`MatchRecord` 缺 `playerCount` | ✅ 已补字段（服务端 NDJSON 必写且强校验），并加「playerCount ≠ players 长度判非法」用例 |
| 规格中：`IsValid` 未覆盖 `isValidMatchRecord` | ✅ 补齐 players 非空/≤4、winnerTeam ∈ {0,1}、玩家名 1..64 字节；三条新断言 |
| 规格中：聊天没有「超长提示」 | ✅ 新增 `HintTruncated="过长已截断"`，超长时截断仍发送但给提示；用例同时断言提示与送入内容长度 |
| 规格中：`Intermission.RequestStart`/`Results.RequestFetch` 无上行接线 | ✅ `RequestStart` 加 `Visible` 守卫（不在波间不响应）；拉取接口的传输方仍是场景装配步骤（与 C07–C11 同一条顺延，已登记） |
| 规格中：4 处自证断言 | ✅ 已修：`MaxPlayersPerMatch*16` 改成直接断言 `MatchIdMaxLength=64`/`PlayerNameMaxBytes=64`，同名/同值重绑不再是恒真；其余登记为测试债 |
| 规格次：`WeaponPicker.Select` 返回 bool、`DefaultSlot` 与服务器不一致 | ✅ 默认槽位改成服务端默认的 **0（手枪）**（`room.cpp` 加入对局默认槽位），并加「新面板默认标签=手枪」用例；返回 bool 保留（调用方要判断是否变化），登记 |
| 规格次：`Intermission.MinMs` 硬编码 15000 | ⏸ 权威是服务端 `kIntermissionSkipMinMs=5000`（`server/src/match/match_controller.hpp`）；§4 与 §5 的 5000/15000 互斥，需人类仲裁后一次性改（本文 §3.3） |
| 规格次：`Lobby.Apply` 丢 `state.Wave` | ✅ `Apply` 现在带 `Wave = state.Wave`（显隐依赖它，不能只在 `SetWave` 里更新） |
| 规范中：`Results` 显隐不由阶段驱动（§6 门禁字面不满足） | ✅ 新增 `Results.VisibleOf(phase)`/`ApplyPhase(phase)`，ended 才显示并触发拉取；门禁复查命中 1 处 |
| 规范次：槽位/玩家上限/昵称长度常量三份副本 | ⏸ 未收敛（应指向 `Ac.Net.WeaponSlot`/`MatchStateCodec.MaxPlayers`）；登记为跨层常量收敛债，与 C07–C12 的同类项一起做 |
| 规范次：只写不读成员、`AllReady` 三份实现、`Chat` 用 float 而 §5 冻结 int | ⏸ 登记为债（`Chat` 的 float 是为与其他 UI 计时器统一，超时判定仍按毫秒整数比较） |
| 规范：证据文档不可复核（`cases=85` 与当前日志不符、`MatchPhase` 说法与实查不符、权威路径写错） | ✅ 本轮已订正本文 §1/§3（含 `server/src/room/phase.hpp`）与本节 |

## 5. 复跑命令

```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd')
Select-String -Path client/Logs/selftest.log -Pattern '^SELFTEST |^FAIL |^PASS lobby\.'
node tools/check-assets.mjs
node tools/check-docs.mjs
```
