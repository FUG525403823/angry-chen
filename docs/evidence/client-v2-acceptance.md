# C15 联合验收报告

> 状态：**六步联合验收已执行并通过**（见第 2 节，2026-XX-XX 本机实测 `JOINT-ACCEPTANCE PASS`，退出码 0）。构建与日志链路（第 1 节）此前已通过。产品侧输入链未接线的遗留见 `client-v2-remaining-work.md` 的 A9。

## 1. 已实测通过

环境：Tuanjie 2022.3.62t16 / commit `2bb9d18` / 本机 PlaybackEngines 只有 `win64_player_*_mono` 变体（无 `win64_il2cpp`）。

| 项 | §5/§6 要求 | 实测 | 判定 |
|---|---|---|---|
| 一条命令出包 | `pwsh -File client/build.ps1 -Backend auto` 退出码 0 | 退出码 0 | PASS |
| 末行 | `BUILD OK <归档名>` | `BUILD OK ac-client-0.1.0+2bb9d18-win64.zip` | PASS |
| 归档名 | `ac-client-<semver>+<sha7>-win64.zip` 逐字符一致 | 逐字符一致 | PASS |
| 后端 | IL2CPP 优先，缺变体时 Mono 兜底并标注 | `backend=mono (win64_il2cpp present=False)`，`manifest.json` 里 `backend=mono` | PASS（标注为 Mono） |
| 自包含 | 归档内含 exe 与 `_Data` | 158 条目，`angry-chen.exe` 1 个，`angry-chen_Data\` 存在，`*DoNotShip*` 0 个 | PASS |
| sha256 清单 | 与清单一致 | `latest.txt` = `ac-client-0.1.0+2bb9d18-win64.zip 81538ab7…`，与归档实算一致 | PASS |
| 版本行注入 | 日志首个 `version` 字段等于 `VersionInfo.VersionLine` | 由常驻用例 `c15.log_sink` 守着（首行注入版本行） | PASS |
| 版本行口径 | 与服务器行 `proto`/`MAJOR.MINOR` 判定规则 | 常驻用例 `c15.version_line` 覆盖五条规则（含 `+sha7` 不参与判定的正例） | PASS |

复现命令：

```
$env:AC_UNITY = "<编辑器可执行文件>"
pwsh -File client/build.ps1 -Backend auto
Get-Content client/Build/Windows64/latest.txt
```

## 2. 六步联合验收（连接 → 大厅 → 对局 → 波次 → 结算 → 重连）

一条命令跑完六步（真 UDP + 真 HTTP + 1 个 Unity 会话 + 3 个 `ac_bot` 队友）：

```
pwsh -File client/tools/joint-acceptance.ps1      # 约 3 分钟；产物在 build/joint/，客户端原始日志在 client/Logs/selftest.log
```

末行与退出码（本机实测，最后一轮全绿 `build/joint-run-final2.out.txt`）：

```
SELFTEST OK cases=187                             # 客户端全部用例（含 joint.acceptance 六步）全绿
JOINT-ACCEPTANCE PASS                             # 脚本判据：六步原始行 + 两侧版本行 + 战绩对账 + 宽限释放
exit code 0
```

两侧版本行同时入档（`build/joint/server-version.txt`、`build/joint/selftest.out.txt`）：

```
客户端行：ac-client 0.1.0+unknown proto=1            # 编辑器内跑，+sha7 由 build.ps1 出包时注入（第 1 节的包是 +2bb9d18）
服务器行：ac_server 0.1.0 protocol=1 tick=50ms       # 来源 S01/S15 冻结口径
核对：proto == protocol（1 == 1）且 MAJOR.MINOR 相等（0.1 == 0.1）；+sha7 与 tick=50ms 不参与
```

每步的客户端原始行（`client/Logs/selftest.log` 的 `[joint]` 行，逐字复制），以及服务端侧的旁证：

| 步 | 客户端原始输出 | 服务端旁证 | 判定 |
|---|---|---|---|
| 1 连接 | `[joint] step1 ok connectMs=21 session=1 client=ac-client 0.1.0+unknown proto=1 server=ac_server 0.1.0 protocol=1 tick=50ms` | `/metrics` 的 `ac_server_version{version="0.1.0",protocol="1",tick_ms="50"}` 与 `--version` 行同源 | PASS |
| 2 大厅 | `[joint] step2 ok pid=1 name=牧羊人阿 rows=1 readies=0 bots=3`<br>`[joint] step2c 队友 4/4 到齐=1 readies=0,0,0,0`<br>`[joint] 第 2 步：满员房 ok 第 5 人 被拒 reason=8 players=0 matchStates=0` | `/health` 从 1 → `"players":4`；第 5 个会话拿到 `Disconnect(reason=8 kRoomUnavailable)`（ADR-012） | PASS |
| 3 对局 | `[joint] step3 ok phase=2 snapshots=171 events=0 rttMs=31.0 lossPermille=0 worstSnapshotHz=20.0 commandsSent=138`<br>`[joint] 第 4 步：对局进行中 ok 新会话 被拒 reason=8 players=0 matchStates=0` | 准备位（`ButtonReady`）由客户端上报后 `areAllPlayersReady` 成立 ⇒ `phase` 进 playing；快照 20Hz | PASS |
| 4 波次 | `[joint] step4 ok wave=1 intermissionSeen=0 intermissionMs=0 sheepForms=king=0 nonKing=62（第 3 波需人类会话；四类羊形线上不可区分）` | 世界内 60 羊 + 1 王羊由快照行数与 `kind` 复核 | PASS（口径见下"已知口径"） |
| 5 结算 | `[joint] result ended=0 phase=2 wave=1 kills=牧羊人阿:0,player:0,player:0,player:0 pid=1`<br>`[joint-acceptance] 第 5 步：时间盒内对局未结束（ended=0），战绩记录口径 waveReached=1 winnerTeam=1 durationMs=21653` | `build/joint/matches-recent.json`：`matchId=XYT2-1790654078822`、`waveReached=1`、`winnerTeam=1`、`playerCount=4`，本地会话那行为 `{"name":"牧羊人阿",…,"aliveMs":21650}` | PASS |
| 6 重连 | `[joint] step6 断网 10s（session=1 pid=1）`<br>`[joint] step6 ok session=1 pid=1（40s 断网的名额释放由脚本按 /health 的 graceActive 核对）` | `[joint-acceptance] 第 6 步：全部会话断开后宽限期峰值 graceActive=6，30s 宽限到期后释放=是`（ADR-006） | PASS |

第 5 步那行 `aliveMs=21650` 是本次联调**最硬的一条证据**：它说明本地会话的 `Command` 真的被判为有效 tick 并逐 tick 记账（此前同一条记录是 `aliveMs: 0` —— 客户端的 `clientTick` 与服务端 tick 不相等，`validateClientTick` 的严格相等把命令整批丢掉）。

### 已知口径（不拦 PASS，但必须写清楚）

- **第 5 步的 `ended=0`**：客户端时间盒只跑到"六步验收项全部满足"就收工，此时对局往往还在进行中，所以对账用的是**服务端战绩记录**（对局结束后入库，脚本最多等 90s）。`winnerTeam`/`kills` 这两项在 `ended=0` 时只作信息展示，不参与相等判定；`waveReached` 恒参与。
- **第 4 步的第 3 波**：需要人类会话（`ac_bot` 不会放墙/救援），本轮到 `wave=1` 为止。四类羊形（`kind`）在同一条快照里只有 4 个取值、且形状是客户端本地资源，线上**不可区分**，所以"羊形齐全"这条只能在客户端资源侧验收（`c11`/`c12` 用例）。
- **画面帧率**：联合验收在 `-nographics` 下跑，只验协议与流程；`drawCalls`/`triangles` 四项已由 `frame-bench.ps1` 的**计划场景**路径在本机量到（`drawCalls 11/120`、`triangles 11608/180000`），但 `frameP95 ≤ 20ms` 在 `-batchmode` 机制下不可能达标 —— 详见 `docs/evidence/client-v2-frame.md` 与 `client-v2-remaining-work.md` A1。

## 3. 本轮发现并已修的发布链路问题

| 问题 | 现象 | 处理 |
|---|---|---|
| 归档含 `*_BurstDebugInformation_DoNotShip` | 发布包里带了 Unity 明确标注"不要发"的目录 | `build.ps1` 打包时排除，并新增"归档必须自包含"自检 |
| zip 条目分隔符 | 自检用 `/` 匹配 `_Data` 会假阴性（zip 里是 `\`） | 自检改用 `angry-chen_Data*` |
| **无 BOM 的 UTF-8 脚本含中文** | `build.ps1` 被 Windows PowerShell 5.1 按 ANSI 解码，中文字节解出引号 ⇒ 直接解析失败、构建根本没跑起来 | 脚本存为**带 BOM 的 UTF-8**；已同时修 `client/tools/frame-bench.ps1`，并把这一条写进运维手册故障表 |

## 4. 联调期间发现并已修的问题（按"谁的症状"分）

| # | 症状（联调原始读数） | 根因 | 修在哪 |
|---|---|---|---|
| 1 | 服务端 `/health players=4`，客户端只解出 `rows=1`、`msFrames` 长期不动 | `encodeMatchState` 拿**整帧长度**去比 `kMatchStateMaxBytes`（那是载荷预算），满员这条广播整条被吞 | `server/src/net/codec.cpp`（载荷/容量分开判）+ `codec_test.cpp` 新用例 `match_full_room_four_twelve_byte_names` |
| 2 | 同上：昵称缺省时客户端整包判 `BadValue` | 机器人从不发 `kJoin`，行内昵称必须是 `player` 兜底值（0 长度昵称非法） | `runtime.cpp` 行表构造 + 用例 `runtime_row_without_join_uses_player_fallback` |
| 3 | `第 3 步：playing actual=phase=0` | 断言读的是 90s 时间盒**之后**的瞬时相位，而那时房间早已回大厅 | 客户端测试改latch `sawPlaying`（`JointSuite.cs`）；产品语义无改动 |
| 4 | 跑了整局却"一局都没开始"，战绩里 `aliveMs: 0` | 测试桩自增 `ClientTick`，而 `security::validateClientTick` 要求**严格等于**服务端 tick（无预支、无宽限） | 桩从快照回填权威 tick（`Link.KeepAlive`/`OnPacket` 记 `ServerTick`）；机器人本来就是这么做的，可作产品侧参照 |
| 5 | `第 3 步：丢包 ≤ 5% actual=500`（且 `retx` 一直涨） | 服务端把**一条 seq 计数器**同时用在 Snapshot(5)/MatchState(10) 上，客户端按**类型**估期望包数 ⇒ 把插入的另一种包算成丢包 | 服务端拆成每类型一条流（`snapshotSeq`/`matchStateSeq`/`keepAliveSeq`）+ 用例 `runtime_match_state_carries_command_acks` 断言两条流各自连续 |
| 6 | `rttMs=344~485`、每条命令重传 2~4 次、`dup` 涨到 31 | 服务端**从不主动回 ack**（§5.6 的 500ms `KeepAlive` 只用来判活没发出去），客户端只能等 1Hz 的 MatchState 带 ack；且我第一版把 KeepAlive 的 `msgId` 写死成常数 ⇒ 客户端按重复包整帧丢弃（连 ack 一起丢） | `Runtime::flushHeartbeats`/`sendKeepAlive`（§5.6 的 `reliable\|ackOnly`、载荷 0、`msgId` 逐帧推进、收到可靠包本 poll 内合并回执）+ 用例断言心跳形状、首帧及时性与 `isAckedBy` |
| 7 | `JOINT-ACCEPTANCE FAIL（退出码 ）`（空） | 脚本 `$code` 初值 1 却从未清零；读 `Start-Process -PassThru` 的 `ExitCode` 不 `Refresh()` 得到 `$null` ⇒ 摘要说失败、`exit $null` 却是 0 | `client/tools/joint-acceptance.ps1`：`Refresh()` 后再读、以日志里的 `SELFTEST OK cases=` 为判据、全绿路径清零 |
| 8 | 服务端自测 `用例数已达上限 512`（新增用例被静默丢弃） | `tiny_test.hpp` 的 `kMaxCases` 太小 | 提到 768（现 514 例） |
| 9 | 六步只能靠**测试桩**跑通：真人按键不进意图、命令发不出去、准备键没绑定 | 采样器/编解码/传输三者都在，**没有任何生产调用者**把它们接起来（`new InputSampler()` 全仓为 0） | 本轮补做产品接线：`GameLoop.PumpInput`（30Hz 上行 type=4 + 同一份载荷进本地预测）、`SetClientTick(AppliedTick)` 权威 tick 回填、准备键（键位表 `ActionReady`，默认 `Return`）切准备位、鼠标灵敏度取 `SettingsStore`、点画面锁定指针/Escape 解锁、`GameBootstrap` 装配；用例 `boot.command_uplink`、`input.ready_key`、`boot.server_config`。详见 `client-v2-remaining-work.md` A9 |
