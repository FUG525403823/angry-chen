# C03 UDP 传输与会话
> 里程碑：MC03 ｜ 预估：1.5 人日 ｜ 上游移交物：HANDOFF-C02

## 1. 目标

客户端能用 .NET `Socket` 建立一条与服务端逐字同规范的 UDP 会话：非阻塞收发、每通道独立序号、ack 位图与去重、RTO 重传、分片重组、500ms 心跳、3s 断线判定、30s 宽限期内的令牌重连；网络面板的数据面能显示 rtt、p99、丢包率与上下行字节。

## 2. 入口条件

**入口条件**：HANDOFF-C02

跨链前置：`S04`（服务端 UDP 传输与可靠性子层）的冻结规范，本份 §5 的常量、包类型与握手时序必须与之逐字相同；权威定义见 [ADR-009](../../00-共识/ADR/ADR-009-UDP传输与协议重构.md)。

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 客户端 codec 与读取器就位 | `Test-Path client/Assets/Scripts/Net/PacketReader.cs, client/Assets/Scripts/Net/SnapshotCodec.cs` | 两行 `True` |
| 2 | 共享角度表已入库 | `Test-Path docs/evidence/fixtures/trig-table.json` | `True` |
| 3 | 断言框架可跑 | `Select-String -Path client/Assets/Scripts/Core/SelfTest.cs -Pattern "SELFTEST OK"` | 命中 |
| 4 | 本机可绑 UDP 回环 | `node -e "const d=require('dgram').createSocket('udp4');d.bind(0,()=>{console.log('bind ok');d.close()})"` | `bind ok` |
| 5 | 协议版本常量在库 | `Select-String -Path docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md -Pattern "v2 = "` | 命中 |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `client/Assets/Scripts/Net/UdpTransport.cs` | 【新建】 | 非阻塞 `Socket` 收发循环：绑定、连接、解复用、发送队列 |
| `client/Assets/Scripts/Net/Reliability.cs` | 【新建】 | 每通道序号、ack 位图与去重、RTO 退避重传、重传超限上报 |
| `client/Assets/Scripts/Net/Fragment.cs` | 【新建】 | 出站分片与入站重组，重组超时丢弃 |
| `client/Assets/Scripts/Net/Handshake.cs` | 【新建】 | `Hello`/`HelloAck`/`Resume`/`Disconnect` 时序与令牌交换 |
| `client/Assets/Scripts/Net/KeepAlive.cs` | 【新建】 | 500ms 心跳与 3s 断线判定，驱动状态机 |
| `client/Assets/Scripts/Net/NetStats.cs` | 【新建】 | §5.4 全部字段的采样与窗口统计 |
| `client/Assets/Scripts/UI/NetworkPanel.cs` | 【新建】 | 网络面板的数据面：把 `NetStats` 渲染成文本行（视觉部分不属本份） |
| `client/Assets/Scripts/Net/PacketWriter.cs` | 【新建】 | 出站包头与载荷的字节写出（§5.2 的布局），供传输层与回环测试共用 |
| `client/Assets/Tests/TransportSuite.cs` | 【新建】 | 回环用例：握手、丢包重传、1200B 分片重组、p99 统计（原计划的 `transport_test.cs`；本仓库自检用例统一放在 `Tests/*Suite.cs`） |

## 4. 任务清单

- [ ] 1. 写 `client/Assets/Scripts/Net/UdpTransport.cs`：`Socket.Blocking = false`，收发缓冲各 64 包，收到包先按 §5.2 校验 `version` 与 `session`。
- [ ] 2. 写 `client/Assets/Scripts/Net/Reliability.cs`：实现 §5.1 的 RTO 与重传预算，ack 位图按 32 位窗口去重，重传超限回调状态机。
- [ ] 3. 写 `client/Assets/Scripts/Net/Fragment.cs`：载荷超过 1200 字节减头部时按 8 片上限切分，入站按 `fragId` 重组，60 tick（3s）未齐即丢弃整组（与 S04 的 `kFragmentTimeoutTicks` 同值）。
- [ ] 4. 写 `client/Assets/Scripts/Net/Handshake.cs`：实现 §5.2 的握手时序，以及 u32 重连令牌（线上编码为 8 位小写十六进制 ASCII）的保存、复用与失效清理。
- [ ] 5. 写 `client/Assets/Scripts/Net/KeepAlive.cs` 与状态机：按 §5.3 的转移表驱动连接状态，超时进入宽限期并尝试 `Resume`。
- [ ] 6. 写 `client/Assets/Scripts/Net/NetStats.cs`：按 §5.4 采集字段，1s 窗口，p99 用环形缓冲的就地插入排序取分位。
- [ ] 7. 写 `client/Assets/Scripts/UI/NetworkPanel.cs` 的数据面：输出 `rtt`、`p99`、`丢包`、`收/发` 四行文本，供面板与调试命令复用。
- [ ] 8. 写 `client/Assets/Tests/TransportSuite.cs`：真套接字回环验握手；内存链路按 §5.7 注入 20% 丢包与 50±10ms 延迟（虚拟时钟，秒级跑完 60 秒），断言 §6 第 2 条的用例全绿。

## 5. 冻结契约

### 5.1 常量表（与服务端传输子层完全相同）
| 常量 | 值 |
|---|---|
| `ProtocolVersion` | `1` |
| `MaxDatagramBytes` | `1200`（含 8B 通用包头与扩展头，超过必须分片） |
| `MaxSnapshotBytes` / 稳态 | `2048` / 不超过 `1228` |
| `MaxBandwidthBytesPerSec` | `40960`（每客户端） |
| `KeepAliveIntervalMs` | `500` |
| `TimeoutMs` | `3000`（3s 未收到任何包即判断线） |
| `GracePeriodMs` | `30000`（宽限期内可用令牌重连） |
| `RtoInitialMs` / `RtoBackoff` / `RtoMaxMs` | `200` / `1.5` / `1000` |
| `MaxRetransmits` | `5`（超限即转入重连） |
| `MaxFragments` | `8` |
| `FragmentTimeoutTicks` | `60`（分片组 3s/60 tick 未收齐即整组丢弃，与服务端 `kFragmentTimeoutTicks` 同值） |
| `SeqModulo` / `AckBitsWidth` | `65536`（每通道独立回绕） / `32` |
| 命令上行速率 / 快照速率 | `30` Hz / 自适应 `10..30` Hz |

`MaxSnapshotBytes`/`MaxBandwidthBytesPerSec` 是 C04 快照层的输入常量，本份只保留数值不落成代码（否则是死常量）；本份实际落地的常量分散在各自语义归属处（`ReliabilityChannel`、`Fragmenter`、`SessionStateMachine`、`UdpTransport`），并复用 `PacketHeader` 的头部尺寸常量。
**速率与 ack 窗口的硬约束**：`AckBitsWidth = 32` 意味着「发送 `msgId` 速率 × RTO 表总时长(2.6s) ≤ 32」才能让全部重传都被确认；命令 30Hz + 心跳 2Hz 时窗口只覆盖约 1s，超出窗口的重传永远收不到 ack，会被误判失联。回环用例因此在 5Hz 命令 + 10Hz 事件下验证零丢失，并把该张力记入交叉链问题（S04/S05 需要给出结论：加宽位图、或把 `ackBase` 定义为「连续收到」）。

### 5.2 包类型与握手时序
```text
type: 1 Hello / 2 HelloAck / 3 Resume / 4 Command / 5 Snapshot / 6 Event / 7 KeepAlive / 8 Disconnect / 9 Fragment
C->S  Hello     { clientNonce u32, reconnectToken u32 }      type=1, session=0, 不可靠（首次连接 reconnectToken 填 0）
S->C  HelloAck  { serverTick u32, salt u32 }                 type=2, 可靠
C->S  Resume    { reconnectToken u32 }                       type=3, 可靠，仅重连
S->C  Disconnect{ reason u8 }                                type=8, 可靠
```
`version`、`type`、`session` 都在 8 字节通用包头里，**不进载荷**；上面四行的载荷字段与 [S04](../server/S04-UDP传输与可靠性子层.md) §5.5 逐字相同。`reconnectToken` 是 u32，线上编码为 8 位小写十六进制 ASCII（8 字节）；`Disconnect.reason` 冻结枚举：1 versionMismatch / 2 tokenInvalid / 3 timeout / 4 serverShutdown / 5 malformedPacket / 6 rateLimited / 7 slowConsumer。

### 5.3 连接状态机
| 状态 | 进入条件 | 离开条件 |
|---|---|---|
| `Disconnected` | 初始态或用户主动断开 | 调用 `Connect(endpoint)` → `Connecting` |
| `Connecting` | 已发送 `Hello` | 收到 `HelloAck` → `Connected`；`Hello` 重传 5 次仍无响应 → `Disconnected` |
| `Connected` | 已收到 `HelloAck` 且会话号非 0 | 3s 未收到任何包 → `Zombie` |
| `Zombie` | 断线判定成立，宽限期计时开始 | 收到任何包含本会话的包 → `Connected`；30s 到期 → `Disconnected` |
| `Reconnecting` | 宽限期内发起 `Resume` 或重传超限 | `Resume` 成功（收到 `HelloAck`，**或收到任何包含本会话的合法包**——S04 §5.5 的服务端在 Resume 成功后只补全量快照、不回 `HelloAck`） → `Connected`；宽限期到期 → `Disconnected` |

### 5.4 NetStats 字段（网络面板与调试面板的唯一数据面）
| 字段 | 类型 | 更新规则 |
|---|---|---|
| `rttMs` | double | 最近一次心跳往返（0.01ms 精度）：`KeepAlive`（ackOnly）发出后，其 `msgId` 被对端 ack 位图确认即得样本。§5.2 的类型表里没有 Ping/Pong，心跳是唯一的周期性往返 |
| `rttP99Ms` | double | 512 槽环形缓冲，就地插入排序后取索引 `min(n-1, max(0, ceil(0.99 * n) - 1))`，保留 2 位小数 |
| `packetLossPermille` | int | 1s 窗口内 `(期望包数 - 实收包数) / 期望包数 * 1000`，期望包数由每通道序号推定 |
| `bytesInPerSec` / `bytesOutPerSec` | double | 1s 窗口内收/发字节数，保留 1 位小数 |
| `retransmits` / `duplicates` / `droppedSnapshots` / `fragmentsReassembled` / `invalidPackets` | int | 单调计数，只在自检或会话重置时清零。本份的 `droppedSnapshots` 只覆盖**出站积压挤掉旧快照**这一条路径；按接收 seq 丢旧快照属于快照层，记在 C04 |
| `rtoMs` | double | 当前通道的 RTO 估计值 |
| `state` | 枚举 | §5.3 的状态之一 |

### 5.5 套接字与分片规则
- 套接字必须非阻塞：`socket.Blocking = false`，禁止 `SendTimeout`、`ReceiveTimeout`、同步阻塞读；每帧最多处理 64 个入站包以防饥饿渲染循环。
- 分片：`fragCount` 不超过 8，`fragIndex` 从 0 起连续；同一 `fragId` 60 tick（3s）未收齐即整组丢弃并计 `invalidPackets`（与服务端分片组超时同值）。超时从**组首片**起算（S04 的 `Reassembler::add` 记 `firstTick`），后续切片不得刷新截止时刻——否则对端每 3s 送一片就能让组永生。
- 去重：可靠包按 `msgId` 去重，重复到达只更新 ack 位图并计 `duplicates`，不得二次投递。片段本身不按 `msgId` 去重（同一条消息的所有片共用一个 `msgId`），靠 `fragIndex` 幂等落位；收齐后的**整条逻辑消息**再按 `msgId` 去重，重复副本计 `duplicates`，只有真正投递的那次计 `fragmentsReassembled`。
- 分片线格式（与 `server/src/net/fragment.cpp` 的 `splitMessage` 逐字相同）：每片 `type = 9 (Fragment)`，`flags = (原通道的 reliable 位) | moreFragments`，片头 4 字节 `{fragId u16, fragIndex u8, fragCount u8}`。**逻辑类型不回带**，接收侧只能由 reliable 位推回（可靠 ⇒ 事件通道，不可靠 ⇒ 快照通道）；这是 §5.4 的缺口，已记入交叉链问题。
- 通道口径：一条逻辑流一个对象，同时持有发送序列（`sendMsgId` + 重传表）与接收窗口（`ackBase`/`ackBits`/去重位图），与 S04 §5.3 的 `ReliableState` 一致。控制包（`HelloAck`/`Resume`/`Disconnect`/`KeepAlive`）与 `Command` 共用同一条可靠流：它们必须与本端命令处在同一条单调 `msgId` 序列里，否则对端按类型去重时会互相顶掉。
- 收到包里的 `ackBase`/`ackBits` 只描述**该包自己那条通道**（S04 §5.3），所以客户端对服务端事件通道的确认只能由事件通道的包回带；本份不含客户端→服务端的事件通道，这一缺口同样记入交叉链问题。
- ack 位图跳变（`k = msgId - ackBase`）：`k == 32` 时先清零再置 bit31；`k > 32` 时**只清零、不置位**（`reliability.cpp` 的 `k >= 33` 显式清零）。C# 的 `uint` 移位按 `&31` 取模，照抄伪码会让 `k = 33` 落到 bit0，凭空捏造「已收到」。
- `msgId == 0` 不是合法值（S04 的 `ackOnReceive` 先拒 0）：收到即忽略，不得进 ack 窗口，否则畸形包能污染去重位图。
- 令牌寿命：令牌只存本地、本身不带过期时刻；30s 宽限期是**从断线时刻起算**的会话属性（S04 §5.6），会话存活多久都不影响。只有另起一次握手、`Resume` 被拒或宽限期到期才作废令牌。
- 序号比较必须用**有符号距离**（`(long)msgId - ackBase`）。S04 §5.3 的伪码用 `uint` 相减后判 `k >= 1`，在 `msgId < ackBase` 时回绕成巨大正数，「更旧」永远落不进 else 分支，去重与 ack 位图都会失效；本份按该段的**意图**实现，并记入交叉链问题请服务端同步。
- 重连口径：`Reconnecting` 期间保留重传表（Resume 恢复同一会话，`msgId` 继续单调，回来照旧重传）；只有进入 `Connecting`（另起一次握手）或 `Disconnected` 才丢弃在途消息。半截分片组在任何离开 `Connected` 的时刻都作废。

## 6. 验证

1. 分组自检：`& $env:AC_UNITY -batchmode -quit -nographics -projectPath client -executeMethod Ac.Tests.SuiteRegistry.RunAll -logFile -`（`AC_UNITY` 见 [C01](C01-Unity工程基线与构建.md) §5） → 期望含 `PASS net.handshake`、`PASS net.retransmit_rto`、`PASS net.fragment_1200`、`PASS net.stats_p99` 与末行 `SELFTEST OK`；本用例同时覆盖「长会话（> 30s）断线仍能 Resume」与「Resume 成功由本会话数据包确认（S04 §5.5 不回 HelloAck）」。失败意味着 UDP 层与服务端传输规范不一致，联机必然握不上手。
2. 丢包回环（同时覆盖位图跳变与分片超时锚点）：`PASS net.retransmit_rto` 里断言 `k = 32` 置 bit31、`k = 33` 只清零；`PASS net.fragment_1200` 里断言分片组超时以首片起算、重组结果逐字节相同。在 20% 丢包、50±10ms 延迟下跑 `PASS net.retransmit_rto` → 期望每条消息最多重传 5 次且消息不丢；失败意味着 ack 位图或 RTO 退避有误，丢包会升级成掉线。链路用内存适配器 + 虚拟时钟（60 秒虚拟时间、秒级跑完），命令 5Hz + 事件 10Hz——速率必须满足 §5.1 的窗口约束，否则失败反映的是规范张力而不是实现缺陷。
3. 非阻塞断言：`Select-String -Path client/Assets/Scripts/Net/UdpTransport.cs -Pattern "Blocking = false"` → 命中；`Select-String -Path client/Assets/Scripts/Net/*.cs -Pattern "SendTimeout|ReceiveTimeout"` → 无输出；失败意味着某个调用会阻塞主线程，帧时间出现尖刺。
4. 依赖面：`Select-String -Path client/Packages/manifest.json -Pattern "transports|netcode"` → 无输出；失败意味着引入了第三方网络包，双端规范无法同名。
5. 仓库质量门：`node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。

## 7. DoD（验收标准）

- [ ] §6 的 5 条验证全部通过，输出与期望逐字匹配。
- [ ] 常量表与服务端传输子层逐条一致，且 §6 第 3 条证明套接字非阻塞。
- [ ] 状态机五态齐全，`Zombie` 进入与离开都有用例，宽限期以 30s 计时**且从断线时刻起算**（长会话断线后仍能 Resume）。
- [ ] 20% 丢包、50±10ms 延迟下 60 秒回环会话零丢消息、零次失联判定（速率满足 §5.1 的窗口约束时），`retransmits` 与 `duplicates` 计数与注入的丢包比例同量级。
- [ ] 单包 1200 字节上限被断言：超过即分片，重组后字节与原消息逐字节相同。
- [ ] `NetStats` 字段与 §5.4 逐条对应，p99 在注入固定延迟分布时与解析值之差不超过 1ms。
- [ ] 无第三方网络包（§6 第 4 条扫描为空）。
- [ ] `node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 主线程被套接字阻塞 | 帧时间出现 10ms 以上尖刺 | 改回非阻塞并加每帧包数上限；用 §6 第 3 条扫描做回归门禁 |
| 与服务端传输子层常量漂移 | 握手超时或 ack 始终不匹配 | 以本文件 §5 为单一来源逐条比对服务端传输子层的常量表，差异即改一侧并重跑回环用例 |
| 分片重组内存吃满 | `invalidPackets` 持续增长 | 重组超时按 S04 的 60 tick（3s）执行，并限制同时活跃的 `fragId` 数量不超过 16 |
| 令牌泄露或被复用 | 重连后身份错乱 | 令牌只存本地，`Resume` 失败即清空并回退到新会话握手 |

回滚目标：回到 HANDOFF-C02（只有 codec 与自检，无网络层），删除本份新增的 `Net/` 传输文件、`UI/NetworkPanel.cs` 与 `client/Assets/Tests/TransportSuite.cs`。

## 9. 移交物

**移交物 ID**：HANDOFF-C03

| 接口 | 位置 | 约定 |
|---|---|---|
| 传输缝 | `client/Assets/Scripts/Net/UdpTransport.cs` | `Connect(host, port)`、`Send(channel, payload)`、`Poll(maxPackets)`、`Close()`；非阻塞，不抛网络异常 |
| 会话状态 | `UdpTransport.State` 与 `KeepAlive.cs` | 五态枚举见 §5.3；状态变化通过事件回调上报 |
| 统计 | `client/Assets/Scripts/Net/NetStats.cs` | 字段与 §5.4 同名同义；`Snapshot()` 返回只读快照 |
| 面板数据面 | `client/Assets/Scripts/UI/NetworkPanel.cs` | `BuildLines(NetStatsSnapshot)` 返回 `rtt`、`p99`、`丢包`、`收/发` 四行文本 |
| 回环测试 | `client/Assets/Tests/TransportSuite.cs` | 可在无服务器环境下注入丢包与延迟（内存链路 + 虚拟时钟），供后续计划回归 |

已验证能力清单：非阻塞 UDP 收发；握手与令牌重连；ack 去重与 RTO 重传；分片重组；心跳与断线判定；网络统计字段可读。
