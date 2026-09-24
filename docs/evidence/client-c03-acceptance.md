# 客户端 C03 验收记录（HANDOFF-C03）

- 计划：[C03-UDP传输与会话](../plans-v2/client/C03-UDP传输与会话.md)（§5 的常量与线格式逐字对齐 [S04](../plans-v2/server/S04-UDP传输与可靠性子层.md)，权威定义见 [ADR-009](../00-共识/ADR/ADR-009-UDP传输与协议重构.md)）
- 里程碑：MC03（tag `MC03`）｜固定点：MC02（`e654eaa`）
- 代码面：`Net/{UdpTransport,Reliability,Fragment,Handshake,KeepAlive,NetStats,PacketWriter}.cs`、`UI/NetworkPanel.cs`、`Tests/TransportSuite.cs`；`PacketReader.cs`（`IsFlagsValidForType` 改 public）、`Tests/SuiteRegistry.cs`、`Core/SelfTest.cs`（默认静默的诊断行）
- 本轮结论：`client/Logs/selftest.cmd` → `SELFTEST OK cases=15`（含 4 个 `net.*` 用例）；`selftest-core.cmd` → `SELFTEST OK cases=2`

## 1. §6 五条验证（实测）

### 1.1 §6.1 分组自检
```text
SELFTEST START cases=15
PASS net.handshake
PASS net.retransmit_rto
PASS net.fragment_1200
PASS net.stats_p99
SELFTEST OK cases=15
```
4 个用例覆盖：真套接字回环握手/令牌/心跳/Zombie/长会话重连（`net.handshake`）、20% 丢包下的重传与去重（`net.retransmit_rto`）、1200B 分片与重组（`net.fragment_1200`）、`NetStats` 与 p99（`net.stats_p99`）。

### 1.2 §6.2 丢包回环
链路用**内存适配器 + 虚拟时钟**（`MemoryLink`，确定性 LCG 抽样丢包、50±10ms 抖动），60 秒虚拟时间秒级跑完。注入与断言（全部通过）：

| 断言 | 阈值 |
|---|---|
| 命令受理数 `commands` | `> 275`（5Hz × 60s） |
| 零丢失 `peer.CommandsSeen.Count == commands` | 逐条相等 |
| 事件恰好投递一次 `eventsSeen.Count == eventsExpected` | 逐条相等（重传副本被去重吃掉） |
| 注入丢包确实发生 `link.Dropped > 0` | — |
| 实测丢包比例 | `0.15 < ratio < 0.25` |
| 失联判定次数 `reconnects` | `== 0` |
| `Duplicates > 0`、`OutstandingMessages <= 8` | 重传表不堆积 |

**为什么是 5Hz 命令 + 10Hz 事件**：`AckBitsWidth = 32` 意味着「每秒 `msgId` 数 × RTO 表总时长(2.6s) ≤ 32」才能让全部重传都被确认；命令 30Hz + 心跳 2Hz 时窗口只覆盖约 1s，超出窗口的那次重传永远收不到 ack，会被误判失联。这是冻结规范（S04 §5.3 + §5.5）的固有张力，已记入 §3.3。用例同时断言位图跳变语义（`k = 32` 置 bit31、`k = 33` 只清零）与分片组超时锚点（见 1.3）。

### 1.3 §6.3 非阻塞断言
```text
Select-String -Path client/Assets/Scripts/Net/UdpTransport.cs -Pattern "Blocking = false"  -> 2 处命中
Select-String -Path client/Assets/Scripts/Net/*.cs -Pattern "SendTimeout|ReceiveTimeout"  -> 0 处
```
第一处在注释里说明硬要求，第二处是 `_socket.Blocking = false` 本体。分片用例另断言：分片组超时以**首片**起算（迟到的片不刷新截止时刻，3s 到点整组丢弃）、重组结果与原消息**逐字节相同**。

### 1.4 §6.4 依赖面
`Select-String -Path client/Packages/manifest.json -Pattern "transports|netcode"` → 0 处。客户端只用 .NET `Socket`，无第三方网络包。

### 1.5 §6.5 仓库质量门
```text
node tools/check-docs.mjs   -> OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。
node tools/check-assets.mjs -> OK：仓库零外部素材，依赖白名单未被破坏。
```

## 2. §7 DoD 逐项

| # | 验收标准 | 结论与证据 |
|---|---|---|
| 1 | §6 的 5 条验证全部通过 | ✅ §1.1–§1.5 |
| 2 | 常量表与服务端一致，套接字非阻塞 | ✅ `RtoTableMs{200,300,450,675,1000}`/`MaxRetransmits 5`/`MaxDatagramBytes 1200`/`MaxFragmentPayload 1176`/`MaxFragments 8`/`FragmentTimeoutTicks 60`/`KeepAliveIntervalMs 500`/`TimeoutMs 3000`/`GracePeriodMs 30000`/`SeqModulo 65536`/`AckBitsWidth 32` 与 S04 §5.1 逐条相同；§1.3 |
| 3 | 五态齐全，Zombie 进入与离开都有用例，宽限期 30s 且从断线时刻起算 | ✅ `net.handshake`：3s 静默→Zombie→收到本会话包→Connected；再静默→Reconnecting→（数据包确认）→Connected；40s 长会话断线仍能 Resume；宽限期到期→Disconnected 且令牌作废 |
| 4 | 20% 丢包下零丢消息、零次失联判定 | ✅ §1.2（速率满足 §5.1 窗口约束的前提已写明） |
| 5 | 1200B 上限被断言，重组逐字节相同 | ✅ `net.fragment_1200`：`<=1200` 逐片断言、4 片 4000B 命令端到端重组、全字节比对 |
| 6 | `NetStats` 与 §5.4 逐条对应，p99 误差 ≤ 1ms | ✅ `net.stats_p99`：12 个字段、环形缓冲、`ceil(0.99n)-1` 索引、丢包率 200‰、字节速率、面板四行文本 |
| 7 | 无第三方网络包 | ✅ §1.4 |
| 8 | `check-docs`/`check-assets` 全绿 | ✅ §1.5 |

补充：`droppedSnapshots` 本份只覆盖「出站积压挤掉旧快照」这一条路径；按接收 seq 丢旧快照属快照层，已记入 C04（计划 §5.4 已标注）。

## 3. 施工偏差与跨链待办

### 3.1 计划本轮的补充（都是「把已实现的事实写进冻结契约」）
- §3 交付物补 `PacketWriter.cs`（写侧，与 `PacketHeader.Read` 共用偏移规则）；测试文件名订正为 `TransportSuite.cs`（原计划写 `transport_test.cs`）。
- §5.3：`Reconnecting` 的离开条件放宽为「收到 `HelloAck` **或任何包含本会话的合法包**」。
- §5.4：`rttMs` 的来源订正为心跳往返（§5.2 的类型表里没有 Ping/Pong）。
- §5.5 补：分片组超时以首片起算；ack 位图 `k = 32` 先清零再置 bit31、`k > 32` 只清零；`msgId == 0` 拒收；令牌不带寿命、30s 从断线时刻起算。
- §6/§7：写明丢包回环用虚拟时钟与 5Hz/10Hz 的速率前提。

### 3.2 解释性偏离（原计划没写、但实现必须选一种）
- 控制包（`HelloAck`/`Resume`/`Disconnect`/`KeepAlive`）与 `Command` 共用同一条可靠流：不共用的话，对端按类型去重时会互相顶掉 `msgId`。
- 重组后的逻辑类型只能由 reliable 位推回（可靠 ⇒ 事件通道，不可靠 ⇒ 快照通道），因为片头 `type` 恒为 9（与 `fragment.cpp` 的 `splitMessage` 一致）。
- `Reconnecting` 期间保留重传表（Resume 恢复同一会话）；只有另起握手或 `Disconnected` 才丢弃在途消息。
- 重传超限只做状态机转移，不主动发 `Disconnect(reason=3)`（服务端才是判定权威）。

### 3.3 跨链问题（交给服务端会话 / 后续计划）
1. **S04 §5.3 的伪码有 bug**：`k = m - ackBase; if (k >= 1)` 用无符号相减，`m < ackBase` 时回绕成巨大正数，「更旧」分支永远不可达，去重与 ack 置位都会失效。客户端按该段**意图**用有符号距离实现，服务端需同步。
2. **ack 窗口与 RTO 区间不匹配**：32 位位图 × 每秒 `msgId` 数 必须 ≥ RTO 表总时长 2.6s，命令 30Hz 时窗口只有约 1s，超出窗口的重传无法被确认 → 偶发误判失联。建议加宽位图、或把 `ackBase` 定义为「连续收到的最大前缀」。
3. **分片丢逻辑类型**：片头只有 reliable 位，客户端只能按「可靠=事件、不可靠=快照」推回；需要 S04 给出显式结论。
4. **客户端→服务端的事件通道没有回程 ack**：S04 §5.3 的 ack 字段只描述该包自己的通道，客户端不产生事件通道的包，所以服务端事件的重传永远等不到确认（回环用例里的事件流因此只靠 5 次重传兜底）。需要一条「跨通道 ack」或确认位图。
5. **`Resume` 成功不回 `HelloAck`**：`handshake.cpp` 在 Resume 成功后只补全量快照；C03 §5.3 的口径已放宽为「两种都认」，但双端最好统一成一种可观测的成功信号。
6. **`S04 §5.5` 的令牌表写 `u32`，线上是 8 字节小写十六进制**（`kTokenBytes 8`）；按线上实现，文档需订正。
7. C02 记录过的跨链项（`short_payload_command.hex`、`matchstate.hex`、事件类型缺夹具等）仍开放，见 [client-c02-acceptance](client-c02-acceptance.md) §3。

## 4. 双轴审查（本轮 · 基准 `e654eaa`）

### 4.1 Spec 轴
| 级别 | 发现 | 处置 |
|---|---|---|
| Blocker | Resume 成功后客户端回不到 `Connected`（服务端不回 `HelloAck`），会反复发 Resume 并被判 token 失效 | ✅ 已修：`Reconnecting` 下收到本会话任何合法包即回 `Connected`；计划 §5.3 修订；用例覆盖「数据包确认」 |
| Blocker | 令牌寿命从 `HelloAck` 起算 30s，存活超过 30s 的会话一断线就放弃重连 | ✅ 已修：令牌不带寿命，宽限期从断线时刻起算；新增「40s 长会话断线仍能 Resume」用例 |
| Major | ack 位图 `k >= 33` 时与服务端分叉（C# 移位按 `&31` 取模） | ✅ 已修：`k > 32` 只清零不置位；`net.retransmit_rto` 断言 `k=32`/`k=33` |
| Major | 分片组超时是滑动窗，对端每 3s 送一片即可让组永生 | ✅ 已修：截止时刻锚在组首片；用例断言 3900ms 迟到片不刷新、4001ms 整组丢弃 |
| Major | `droppedSnapshots` 结构性恒为 0 | ✅ 已记录：本份只覆盖出站积压路径，入站按 seq 丢旧归 C04（计划 §5.4 已标注） |
| Major | `fragmentsReassembled` 把未分片事件也计进重组 | ✅ 已修：只有「重组后真正投递」才计数，重复副本计 `duplicates` |
| Major | 只绑回环、主机名不可用 | ✅ 已修：`Bind(IPAddress.Any)` + `TryParse`/`Dns.GetHostAddresses` 回退 |
| Major | 验证强度：只比对首末字节、Zombie 离开路径无用例、桩件 Resume 语义与服务端相反 | ✅ 均已修：全字节比对、Zombie→Connected 用例、桩件改为回全量快照 |
| Minor | `msgId == 0` 未拒收（S04 的 `ackOnReceive` 先拒 0） | ✅ 已修 |
| Minor | RTT 采样未限定通道（违反 §5.1 的「ack 只描述该包自己的通道」） | ✅ 已修：只有命令流的包才确认心跳 `msgId` |
| Minor | 重复包不刷新存活时间，噪音可导致误判失联 | ✅ 已修：去重分支也刷新 `LastRecvMs` |
| Minor | 号序/序号分流口径、出站分片不限通道 | ⏸ 有意保留：S04 §5.5 只冻结了 `msgId` 共流，`seq` 按包类型各自计数；分片丢逻辑类型见 §3.3 第 3 条 |

### 4.2 Standards 轴
| 级别 | 发现 | 处置 |
|---|---|---|
| Major | 失败时新增的 `[selftest] stack` 行可能违反 C02 §5.7 的输出契约 | ✅ 已修：改为 `AC_SELFTEST_STACK=1` 才输出（默认静默），并在 C02 §5.7 登记这条可选行 |
| Major | 计划 §9 的移交缝写成 `BuildLines(NetStats)` | ✅ 已修：计划改为 `BuildLines(NetStatsSnapshot)` |
| Major | `UdpTransport.cs` 职责过多（Divergent Change） | 🟡 部分修：`DeriveNonce` 移入 `HandshakeCodec`、通道映射公开为 `UdpTransport.StreamOf`、测试复用生产套接字；`RealUdpSocket` 独立成文件与积压队列的拆分留作后续清理 |
| Major | 新增公开 API 无调用方 | ✅ 已修：`ServerTick`/`Salt`/`ResumeAttempts`/`GraceStartMs`/`BacklogBytes`/`Count` 收回 `internal`，`LastDisconnectReason` 删除 |
| Major | 重复实现：`PutU32` 两份、通道映射两份、套接字桩一份、分片阈值两份 | ✅ 均已修：`PacketWriter.PutU32` 复用、`UdpTransport.StreamOf` 双端共用、删除测试桩件、`Fragmenter.NeedsFragmentation` 双端共用 |
| Minor | 死字段/恒真断言/无意义三元式 | ✅ 已修：`_real`/`MessagesSeen`/`HasPending`/`_endpointCount`/`_outbound` 删除、`ResumeCount >= 1`、去掉恒等三元式 |
| Minor | `ReliabilityChannel` 的公开可变字段、`MakeHeader` 形参遮蔽、面板标签混用中英 | ⏸ 有意保留：属状态缝的读写面与显示文案，后续计划（C13 调试面板）统一处理 |

## 5. 复跑命令
```powershell
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd')        # SELFTEST OK cases=15
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest-core.cmd')   # SELFTEST OK cases=2
node tools/check-docs.mjs ; node tools/check-assets.mjs
Select-String -Path client/Assets/Scripts/Net/UdpTransport.cs -Pattern 'Blocking = false'
Select-String -Path client/Assets/Scripts/Net/*.cs -Pattern 'SendTimeout|ReceiveTimeout'
```
