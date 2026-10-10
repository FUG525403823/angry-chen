# S16：波间升级与地图弹药补给箱

用户需求：羊群波次下弹药不够用，且角色若不随波次成长会很快无聊——需要在**地图上放置换弹/弹药补给位置**，并在**每波结束后升级角色**（移速、伤害、换弹速度、备弹量）。

本轮的完成方式受用户硬约束限制：**不得启动 Unity / Tuanjie（团结引擎）或任何客户端程序**。因此客户端侧的证据全部来自「离线 Roslyn 编译真实客户端源码 + 纯逻辑断言」与静态推演，服务器侧照常编译运行（C++，非 Unity）。

## 1. 玩法改动

### 1.1 波间升级（升级点）

- `server/src/config/upgrades.hpp`：4 项升级 × 最高 5 级，每波清波 +1 点（`kPointsPerWaveClear=1`）。
  - id 0 伤害：每级 +15%（`1.0 + 0.15×lv`，满级 1.75）
  - id 1 移速：每级 +8%（`1.0 + 0.08×lv`，满级 1.4）
  - id 2 换弹：每级 −12%（`max(0.4, 1.0 − 0.12×lv)`，满级 0.4，下限不为 0）
  - id 3 备弹：每级 +40（`40×lv`，满级 +200）
- 等级 0 的乘数严格等于 1.0（`x*1.0==x`，位精确不受影响）；新增玩法常量刻意**不进 configHash**（`19a978ea` 不变，真值来源是冻结的 v1 实现，见 `docs/evidence/fixtures/README.md §1`）。
- 服务端在清波进波间时给存活玩家 +1 点并**重置全员 ready**（波间必须重按准备）；波间 `MatchState` 每 1Hz 下发，客户端据此刷新升级点与等级。

### 1.2 弹药补给箱

- `server/src/config/pickup.hpp`：4 个固定箱位 `{(10,0,10),(-10,0,10),(10,0,-10),(-10,0,-10)}`（谷仓 ±4 之外、出生点 z=7 附近可达、与羊群出生环 r=38 保持距离）。
- 实体 kind = pickup(3)，比赛开始（loading→playing）时生成，计入快照与击杀/实体统计，但无碰撞（纯交互点）。
- 交互半径 `kAmmoCrateRangeM=1.8`（玩家中心到箱中心），`resolveAmmoCrates` 每 tick 检查：范围内玩家且「弹匣或备弹未满」则累计蓄力 `kAmmoCrateRefillMs=1500`，蓄满后把弹匣补到当前武器弹匣满、备弹补到 `120 + 40×升级备弹等级`；蓄力在离开范围 / 弹药已满 / 补给完成时重置。
- 客户端提示半径 2.2 m（`AmmoCrateHintRangeM = 1.8 + 玩家半径 0.4` 略放宽，提示比交互早一拍出现）；弹匣与备弹都到顶时不提示（服务端对满仓交互也是空转）。

### 1.3 升级购买与上行

- 新报文 **type 12 UpgradeSelect**（可靠 C→S，载荷 1 字节 upgradeId）：`wire.hpp` 枚举 + `kMaxPacketType=12u`；`security::isClientToServerType` 白名单放行。
- `runtime.cpp handleUpgradeSelect`：校验「intermission 相位 / id<4 / 本行 points>0 / 等级<5」，任何一项不过即拒绝并计入 `kDroppedFrames`（与服务端一致的口径）。
- 客户端乐观购买（`UpgradeModel.TryBuy` → `GameLoop.BuyUpgrade` → type 12 上行）；载荷越界 / 未连接 / 发送失败都返回 false 并计 `UpgradeSendFailures`，权威裁决由 MatchState 1Hz 拉回。

## 2. 协议扩展（冻结 UDP，位精确不变）

- `MatchState`（type 10）玩家固定段 16 → **21 字节**，追加 5 字节升级段：`points u8 + damage/speed/reload/reserve 各 ≤5 u8`。载荷预算 `kMatchStateMaxBytes=139`（不含 20 字节包头）；客户端 `MatchStateCodec.FixedRecordBytes=21`、升级段等级 >5 判 `BadValue`、尾部余量判 `BadLength`。
- 客户端侧校验 `upgradeId` 越界即拒绝编码，不发出。

## 3. 客户端实现（无头架构内）

- `Sim/UpgradeTable.cs`：与服务端 `upgrades.hpp` 逐条镜像（跨语言常量断言测试钉死）。
- `UI/UpgradeModel.cs`：波间面板模型（纯逻辑，不引用 UnityEngine）——点数、4 卡片、等级/MAX、乐观购买。
- `Net/UpgradeCodec.cs`：type 12 编码（`TryEncode` 越界返回 false）。
- `Net/MatchStateCodec.cs`：21 字节固定记录解码（+5 升级段）。
- `Boot/GameLoop.cs`：`ApplyUpgradeLevels`（移速→`Predictor.SetConfig`、换弹→`LocalWeapon.SetReloadTimeMultiplier`、备弹→补给判据）；`BuyUpgrade` 全路径失败计数；`SampleAmmoCrateProximity`（playing 相位 + 本地玩家 + 弹药未满 + 2.2 m 内 kind=3 才提示）；playing→intermission 一跳清本地 `ReadyHeld`（与服务端「必须重按准备」同拍）；身份解绑（localPid=0 或换行）不再把 HUD 弹药/怒气打回 0。
- `UI/Hud.cs`（采样 `NearAmmoCrate`）、`UI/OverlayModel.cs` 升级面板（标题「波次升级 · 剩余 N 点」、卡片行「名称 Lv N（MAX）」、可买卡顶条 + 暗色衬底）、`View/CrateVisuals.cs`（kind 3 可视化）。

## 4. 测试修复（本会话针对上次 selftest 的 10 个失败）

| 用例 | 失败原因 | 修复 |
|---|---|---|
| `codec.roundtrip` | 旧日志残留（当时测试修复未全部生效） | 离线 harness 实证当前 codec 正确：matchstate p0.Pid=3、内嵌事件 subjectId=3 |
| `upgrade.matchstate_decode` | `EncodeMatchState` 的 rageLeft100Ms 多写 1 字节（u16 而非 u8）→ BadLength | 改 1 字节（harness 实证通过） |
| `upgrade.screenflow_feed` | 测试未设 `state.Wave`（Intermission.Visible 要求 wave≥1） | `state.Wave = 3`（harness 实证通过） |
| `upgrade.*` 其余 4 项 | 无 | harness 实证 6/6 通过 |
| `net.join_wire` | joinFrame/unknownFrame 版本字节硬编码 1 ≠ `ProtocolVersion=2` | 改 `PacketHeader.ProtocolVersion`；unknownFrame type 13 |
| `net.send_reject_no_retransmit` | `IsCommandDatagram` 检查 `datagram[0]==1` 而线上版本=2 | 改 `datagram[0] == PacketHeader.ProtocolVersion` |
| `boot.hud_ammo_rage_from_match_state` | 第二个 MatchState localPid=0 触发身份解绑 → 武器/弹药被重置 | 仅当新 pid 非零才重置（`previousPid != LocalPlayerId && LocalPlayerId != 0`） |
| `boot.upgrade_buy_sends_type12` | `BuyUpgrade` 的编码失败路径未计数 | 编码失败 / 未连接 / 发送失败都计 `UpgradeSendFailures` |
| `boot.upgrade_levels_drive_local_sim` | 采样器 30ms 间隔 + 预热不足致子步错位；外推速度新旧不同会量偏 | 预热 4 帧 +「2 帧 = 1 子步」测量；速度切换后先跑 2 帧让 State.Vz 与累加器换到新相位再量 |
| `boot.near_crate_hint` | 渲染时钟落后 100ms，插值 alpha<1 使 6m 箱子显示在提示范围内 | 每个快照后推 10 帧（1000/60）等插值收敛 |
| `render.upgrade_panel_items` | `_upgradeLevelCache` 初值 0，0 级卡首帧「0==0 没变化」→ 卡片行缺失 | `EmptyUpgradeLevelCache()` 以 −1 起步（首帧强制重建文案） |

## 5. 验证证据

- 服务器（C++，允许运行）：`server/build.ps1 -Config Debug` 构建通过；`server/build/ac_tests.exe` → **TESTS 555/555**（exit 0）；`node tools/export-fixtures.mjs --check` → **check ok：14/14 与盘上逐字节一致**。
- 客户端纯逻辑层（离线 Roslyn harness，编译**真实仓库源码**，不加载 UnityEngine、不启动引擎）：`UpgradeSuite` 6/6（table_mirror / model_buy / codec_bounds / matchstate_decode / screenflow_feed / schema_consistency）+ codec 反例断言（`[matchstate] p0.Pid=3`、`[snapshot-embedded] subjectId=3 targetId=17`）全部通过，exit 0。
- `tools/check-docs.mjs` 链（S×15 + C×15）未增删任何 plans-v2 计划文件，文档结构不受影响。

## 6. 偏差与说明

- **ready 重按语义**：服务端清波时重置全员 ready；客户端在 playing→intermission 那一跳同步清本地 `ReadyHeld`（否则下一条命令会把服务端又顶回 true）。
- **乐观购买 + 服务端裁决**：客户端买升级先本地生效并上行 type 12，服务端权威校验失败会拒绝并计入 `kDroppedFrames`，MatchState 1Hz 拉回。
- **交互 1.8 m / 提示 2.2 m**：提示半径比交互半径略放宽，让「看到提示」早于「能交互」一拍。
- **MatchState 固定段 16 → 21**：5 字节升级段（points + 4 级）追加在 revive 之后、localPid 之前；`rageLeft100Ms` 保持 u8（1 字节）。

## 7. 尚未验收（受「禁用 Unity/客户端程序」约束）

- 未运行 `client/tools/selftest.ps1`（Unity batchmode 属于被禁范围）；10 个修复中只有纯逻辑层（codec / upgrade.*）经离线 harness 实测，`boot.*` / `net.*` / `render.*` 修复为静态推演，待用户允许后跑全量 245 用例复核并更新本文件。
- 未做游戏内手测（补给箱可视化、升级面板实机手感、波间重按准备的联机体验）。
